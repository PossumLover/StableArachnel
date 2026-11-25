#include "plugin_catalog_service.h"

#include "plugin_api.h"
#include "plugin_urls.h"
#include "plugin_version.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <memory>

namespace arachnel::core {

namespace {

const char* kDefaultCatalogUrl =
    "https://raw.githubusercontent.com/BadKikoSecond/arachnel_plugins_sourcelist/main/plugins.json";

/**
 * Plugins Sprout takes from a fork's GitHub Releases instead of the official sourcelist.
 *
 * steamidra: upstream keeps depots whose manifest platform hint is "unknown", which a
 * Linux or macOS depot is, so games with native ports got every OS's depot laid over the
 * Windows one (Core Keeper, Valheim: DllNotFoundException System.Native under Proton).
 * The fix was never merged upstream, and every Sprout update force-reinstalls official
 * plugins, which silently put the broken build back. The fork carries the fix and
 * otherwise tracks upstream.
 *
 * The latest release's asset replaces the official row's version, url, sha256 and size.
 * If the fork can't be reached the row is dropped for this refresh rather than falling
 * back to the official build: the installed plugin keeps working, it just isn't updated.
 */
struct PluginSourceOverride {
    const char* pluginId;
    const char* githubRepo;
    const char* assetName;
    const char* feedName;
};
constexpr PluginSourceOverride kPluginSourceOverrides[] = {
    {"steamidra", "PossumLover/steamidra_linux_fix", "steam.arach", "steamidra-feed.json"},
};

QVariantMap releaseFeedPatch(const QJsonObject& feed, const QString& pluginId,
                            const QString& repository, const QString& assetName)
{
    // Match the release workflow's tag format without the leading v.
    static const QRegularExpression versionPattern(
        QStringLiteral("\\A[0-9]+\\.[0-9]+\\.[0-9]+(?:\\.[0-9]+)?(?:-[A-Za-z0-9._]+)?\\z"));
    static const QRegularExpression digestPattern(QStringLiteral("\\A[0-9a-fA-F]{64}\\z"));

    const QString version = feed.value(QStringLiteral("version")).toString();
    const QString url = feed.value(QStringLiteral("url")).toString();
    const QString digest = feed.value(QStringLiteral("sha256")).toString();
    const qint64 size = feed.value(QStringLiteral("size")).toInteger();
    if (feed.value(QStringLiteral("schemaVersion")).toInt() != 1
        || feed.value(QStringLiteral("id")).toString() != pluginId
        || feed.value(QStringLiteral("apiVersion")).toInt() != 4
        || feed.value(QStringLiteral("abiToken")).toString() != QStringLiteral("api=4")
        || !versionPattern.match(version).hasMatch()
        || url != QStringLiteral("%1/releases/download/v%2/%3").arg(repository, version, assetName)
        || !digestPattern.match(digest).hasMatch() || size <= 0)
        return {};

    return {{QStringLiteral("version"), version},
            {QStringLiteral("url"), url},
            {QStringLiteral("sha256"), digest.toLower()},
            {QStringLiteral("size"), size}};
}

QNetworkRequest releaseRequest(const QString& url)
{
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Arachnel/%1").arg(QCoreApplication::applicationVersion()));
    return request;
}

QStringList platformsFromJson(const QJsonValue& value)
{
    QStringList platforms;
    if (value.isArray()) {
        const QJsonArray plats = value.toArray();
        for (const QJsonValue& p : plats)
            platforms.append(p.toString());
    }
    return platforms;
}

bool platformsSupported(const QStringList& platforms, const QString& platform)
{
    return platforms.isEmpty() || platforms.contains(platform)
           || platforms.contains(QStringLiteral("all"));
}

bool isHydraLauncherPlugin(const QJsonObject& obj)
{
    const QString id = obj.value(QStringLiteral("id")).toString().trimmed().toLower();
    const QString name = obj.value(QStringLiteral("name")).toString().toLower();
    const QString repo = obj.value(QStringLiteral("repository")).toString().toLower();
    const QString url = obj.value(QStringLiteral("url")).toString().toLower();
    const auto hit = [](const QString& text) {
        return text.contains(QStringLiteral("hydralauncher"))
               || text.contains(QStringLiteral("/hydra-plugin"))
               || text == QStringLiteral("hydra") || text.startsWith(QStringLiteral("hydra-"));
    };
    return hit(id) || hit(repo) || hit(url) || name.contains(QStringLiteral("hydra launcher"));
}

/** Pick newest build compatible with this Arachnel + platform. */
QJsonObject pickCompatibleBuild(const QJsonObject& pluginObj, const QString& appVersion,
                                const QString& platform)
{
    const QJsonArray builds = pluginObj.value(QStringLiteral("builds")).toArray();
    if (builds.isEmpty()) {
        // Schema v1 flat entry.
        QJsonObject flat;
        flat.insert(QStringLiteral("version"), pluginObj.value(QStringLiteral("version")));
        flat.insert(QStringLiteral("apiVersion"), pluginObj.value(QStringLiteral("apiVersion")));
        flat.insert(QStringLiteral("minArachnel"),
                    pluginObj.value(QStringLiteral("minArachnel")).toString(QStringLiteral("0.0.0")));
        flat.insert(QStringLiteral("maxArachnel"),
                    pluginObj.value(QStringLiteral("maxArachnel")).toString());
        flat.insert(QStringLiteral("url"), pluginObj.value(QStringLiteral("url")));
        flat.insert(QStringLiteral("sha256"), pluginObj.value(QStringLiteral("sha256")));
        flat.insert(QStringLiteral("size"), pluginObj.value(QStringLiteral("size")));
        flat.insert(QStringLiteral("platforms"), pluginObj.value(QStringLiteral("platforms")));
        flat.insert(QStringLiteral("abiToken"), pluginObj.value(QStringLiteral("abiToken")));
        flat.insert(QStringLiteral("file"), pluginObj.value(QStringLiteral("file")));
        if (!platformsSupported(platformsFromJson(flat.value(QStringLiteral("platforms"))),
                                platform))
            return {};
        if (!appVersionInRange(appVersion,
                               flat.value(QStringLiteral("minArachnel")).toString(),
                               flat.value(QStringLiteral("maxArachnel")).toString()))
            return {};
        const int api = flat.value(QStringLiteral("apiVersion")).toInt(0);
        if (api > ARACHNEL_PLUGIN_API_VERSION || api < ARACHNEL_PLUGIN_API_VERSION_MIN)
            return {};
        return flat;
    }

    QJsonObject best;
    QString bestVersion;
    for (const QJsonValue& value : builds) {
        if (!value.isObject())
            continue;
        const QJsonObject build = value.toObject();
        if (!platformsSupported(platformsFromJson(build.value(QStringLiteral("platforms"))),
                                platform))
            continue;
        if (!appVersionInRange(appVersion,
                               build.value(QStringLiteral("minArachnel"))
                                   .toString(QStringLiteral("0.0.0")),
                               build.value(QStringLiteral("maxArachnel")).toString()))
            continue;
        const int api = build.value(QStringLiteral("apiVersion")).toInt(0);
        if (api > ARACHNEL_PLUGIN_API_VERSION || api < ARACHNEL_PLUGIN_API_VERSION_MIN)
            continue;
        const QString ver = build.value(QStringLiteral("version")).toString();
        if (best.isEmpty() || comparePluginVersions(ver, bestVersion) > 0) {
            best = build;
            bestVersion = ver;
        }
    }
    return best;
}

} // namespace

PluginCatalogService::PluginCatalogService(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QString PluginCatalogService::catalogUrl() const
{
    return QString::fromUtf8(kDefaultCatalogUrl);
}

QString PluginCatalogService::currentPlatformId() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("unknown");
#endif
}

void PluginCatalogService::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void PluginCatalogService::setInstalling(const QString& pluginId)
{
    const bool next = !pluginId.isEmpty();
    if (m_installing == next && m_installingPluginId == pluginId)
        return;
    m_installing = next;
    m_installingPluginId = pluginId;
    if (next)
        setDownloadProgress(0);
    emit installingChanged();
}

void PluginCatalogService::setDownloadProgress(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (m_downloadProgress == clamped)
        return;
    m_downloadProgress = clamped;
    emit downloadProgressChanged();
}

void PluginCatalogService::setError(const QString& message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void PluginCatalogService::refresh()
{
    if (m_catalogReply) {
        // Capture-by-reply below; disconnect so an aborted reply cannot steal the new one.
        QObject::disconnect(m_catalogReply, nullptr, this, nullptr);
        m_catalogReply->abort();
        m_catalogReply->deleteLater();
        m_catalogReply = nullptr;
    }

    setError({});
    setLoading(true);
    const int generation = ++m_catalogGeneration;

    QNetworkRequest request{QUrl(catalogUrl())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Arachnel/%1").arg(QCoreApplication::applicationVersion()));

    QNetworkReply* reply = m_network->get(request);
    m_catalogReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (m_catalogReply == reply)
            m_catalogReply = nullptr;
        reply->deleteLater();
        // `loading` stays on until the list is published (after the fork lookups in
        // applySourceOverrides) or this refresh fails; an aborted reply belongs to a
        // refresh that was superseded, and the newer one owns the flag.

        if (reply->error() != QNetworkReply::NoError) {
            // Superseded/aborted refresh — keep current list, don't wipe UI.
            if (reply->error() == QNetworkReply::OperationCanceledError)
                return;
            setLoading(false);
            setError(QCoreApplication::translate("Core", "Could not load plugin list: %1")
                         .arg(reply->errorString()));
            m_plugins.clear();
            emit pluginsChanged();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            setLoading(false);
            setError(QCoreApplication::translate("Core", "Plugin list is invalid"));
            m_plugins.clear();
            emit pluginsChanged();
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray arr = root.value(QStringLiteral("plugins")).toArray();
        QVariantList next;
        next.reserve(arr.size());
        const QString platform = currentPlatformId();
        const QString appVersion = QCoreApplication::applicationVersion();
        for (const QJsonValue& value : arr) {
            if (!value.isObject())
                continue;
            const QJsonObject obj = value.toObject();
            const QString pluginId = obj.value(QStringLiteral("id")).toString().trimmed();
            if (pluginId.isEmpty() || isHydraLauncherPlugin(obj))
                continue;

            const QJsonObject build = pickCompatibleBuild(obj, appVersion, platform);
            if (build.isEmpty())
                continue;

            QVariantMap row;
            row.insert(QStringLiteral("id"), pluginId);
            row.insert(QStringLiteral("name"), obj.value(QStringLiteral("name")).toString());
            row.insert(QStringLiteral("description"),
                       obj.value(QStringLiteral("description")).toString());
            row.insert(QStringLiteral("version"),
                       build.value(QStringLiteral("version")).toString());
            row.insert(QStringLiteral("apiVersion"),
                       build.value(QStringLiteral("apiVersion")).toInt(0));
            row.insert(QStringLiteral("iconName"),
                       obj.value(QStringLiteral("iconName")).toString(QStringLiteral("extension")));
            row.insert(QStringLiteral("url"), build.value(QStringLiteral("url")).toString());
            row.insert(QStringLiteral("sha256"), build.value(QStringLiteral("sha256")).toString());
            row.insert(QStringLiteral("size"), build.value(QStringLiteral("size")).toVariant());
            row.insert(QStringLiteral("abiToken"),
                       build.value(QStringLiteral("abiToken")).toString());
            row.insert(QStringLiteral("minArachnel"),
                       build.value(QStringLiteral("minArachnel")).toString());
            row.insert(QStringLiteral("maxArachnel"),
                       build.value(QStringLiteral("maxArachnel")).toString());
            row.insert(QStringLiteral("repository"), resolvePluginRepository(pluginId, obj));
            row.insert(QStringLiteral("recommended"),
                       obj.value(QStringLiteral("recommended")).toBool(false));

            const QStringList platforms =
                platformsFromJson(build.value(QStringLiteral("platforms")));
            row.insert(QStringLiteral("platforms"), platforms);
            row.insert(QStringLiteral("supported"), true);
            next.append(row);
        }

        std::sort(next.begin(), next.end(), [](const QVariant& a, const QVariant& b) {
            const QVariantMap left = a.toMap();
            const QVariantMap right = b.toMap();
            const bool leftRec = left.value(QStringLiteral("recommended")).toBool();
            const bool rightRec = right.value(QStringLiteral("recommended")).toBool();
            if (leftRec != rightRec)
                return leftRec;
            return QString::localeAwareCompare(left.value(QStringLiteral("name")).toString(),
                                               right.value(QStringLiteral("name")).toString())
                   < 0;
        });

        applySourceOverrides(next, generation);
    });
}

void PluginCatalogService::applySourceOverrides(const QVariantList& rows, int generation)
{
    struct Pending {
        int remaining = 0;
        QVariantList rows;
    };
    auto pending = std::make_shared<Pending>();
    pending->rows = rows;

    const auto publish = [this, pending, generation]() {
        if (generation != m_catalogGeneration)
            return; // a newer refresh() owns the list now
        m_plugins = pending->rows;
        setLoading(false);
        emit pluginsChanged();
    };

    for (const PluginSourceOverride& source : kPluginSourceOverrides) {
        const QString pluginId = QString::fromLatin1(source.pluginId);
        const bool listed = std::any_of(rows.cbegin(), rows.cend(), [&](const QVariant& row) {
            return row.toMap().value(QStringLiteral("id")).toString() == pluginId;
        });
        if (!listed)
            continue;

        const QString repo = QString::fromLatin1(source.githubRepo);
        const QString repository = QStringLiteral("https://github.com/%1").arg(repo);
        const QString assetName = QString::fromLatin1(source.assetName);
        const auto finish = [pending, publish, pluginId, repo, repository](
                                QVariantMap patch, const QString& problem) {
            if (problem.isEmpty())
                patch.insert(QStringLiteral("repository"), repository);
            for (qsizetype i = pending->rows.size() - 1; i >= 0; --i) {
                QVariantMap row = pending->rows.at(i).toMap();
                if (row.value(QStringLiteral("id")).toString() != pluginId)
                    continue;
                if (!problem.isEmpty()) {
                    pending->rows.removeAt(i);
                    continue;
                }
                for (auto it = patch.cbegin(); it != patch.cend(); ++it)
                    row.insert(it.key(), it.value());
                pending->rows[i] = row;
            }
            if (problem.isEmpty()) {
                qInfo().noquote() << "[plugin-catalog]" << pluginId << "from" << repo
                                  << patch.value(QStringLiteral("version")).toString();
            } else {
                qWarning().noquote() << "[plugin-catalog]" << pluginId << "fork" << repo
                                     << "unavailable (" << problem
                                     << ") - not offering an update this time";
            }

            if (--pending->remaining == 0)
                publish();
        };

        const auto fetchLegacyRelease = [this, generation, finish, repo, assetName]() {
            QNetworkReply* reply = m_network->get(releaseRequest(
                QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo)));
            connect(reply, &QNetworkReply::finished, this, [this, reply, generation, finish, assetName]() {
                reply->deleteLater();
                if (generation != m_catalogGeneration)
                    return;

                QVariantMap patch;
                QString problem;
                if (reply->error() != QNetworkReply::NoError) {
                    problem = reply->errorString();
                } else {
                    const QJsonObject release =
                        QJsonDocument::fromJson(reply->readAll()).object();
                    QString version = release.value(QStringLiteral("tag_name")).toString();
                    if (version.startsWith(QLatin1Char('v')))
                        version.remove(0, 1);
                    for (const QJsonValue& value :
                         release.value(QStringLiteral("assets")).toArray()) {
                        const QJsonObject asset = value.toObject();
                        if (asset.value(QStringLiteral("name")).toString() != assetName)
                            continue;
                        QString digest = asset.value(QStringLiteral("digest")).toString();
                        if (digest.startsWith(QStringLiteral("sha256:")))
                            digest.remove(0, 7);
                        patch.insert(QStringLiteral("version"), version);
                        patch.insert(
                            QStringLiteral("url"),
                            asset.value(QStringLiteral("browser_download_url")).toString());
                        patch.insert(QStringLiteral("sha256"), digest.toLower());
                        patch.insert(QStringLiteral("size"),
                                     asset.value(QStringLiteral("size")).toVariant());
                        break;
                    }
                    if (version.isEmpty()
                        || patch.value(QStringLiteral("url")).toString().isEmpty())
                        problem = QStringLiteral("latest release has no %1").arg(assetName);
                    else if (patch.value(QStringLiteral("sha256")).toString().size() != 64)
                        problem = QStringLiteral("latest release has no sha256 digest");
                }
                finish(patch, problem);
            });
        };

        ++pending->remaining;
        const QString feedName = QString::fromLatin1(source.feedName);
        QNetworkReply* reply = m_network->get(releaseRequest(
            QStringLiteral("%1/releases/latest/download/%2").arg(repository, feedName)));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, generation, finish, fetchLegacyRelease, pluginId, repository,
                 assetName]() {
                    reply->deleteLater();
                    if (generation != m_catalogGeneration)
                        return;

                    const int status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status == 404) {
                        fetchLegacyRelease();
                        return;
                    }
                    if (reply->error() != QNetworkReply::NoError) {
                        finish({}, reply->errorString());
                        return;
                    }

                    const QVariantMap patch = releaseFeedPatch(
                        QJsonDocument::fromJson(reply->readAll()).object(), pluginId, repository,
                        assetName);
                    // Older releases have no usable feed. Keep the API lookup for those releases.
                    if (patch.isEmpty())
                        fetchLegacyRelease();
                    else
                        finish(patch, {});
                });
    }

    if (pending->remaining == 0)
        publish();
}

QString PluginCatalogService::downloadUrlForEntry(const QVariantMap& entry) const
{
    return entry.value(QStringLiteral("url")).toString().trimmed();
}

void PluginCatalogService::installPlugin(const QString& pluginId)
{
    const QString id = pluginId.trimmed();
    if (id.isEmpty())
        return;

    if (id != m_installingPluginId && !m_installQueue.contains(id))
        m_installQueue.append(id);

    if (!m_installing)
        processInstallQueue();
}

bool PluginCatalogService::beginInstall(const QString& pluginId)
{
    QVariantMap found;
    for (const QVariant& item : m_plugins) {
        const QVariantMap row = item.toMap();
        if (row.value(QStringLiteral("id")).toString() == pluginId) {
            found = row;
            break;
        }
    }
    if (found.isEmpty()) {
        setError(QCoreApplication::translate("Core", "Plugin not found in the official list"));
        emit installFinished(pluginId, false, m_error);
        return false;
    }

    const QString url = downloadUrlForEntry(found);
    if (url.isEmpty()) {
        setError(QCoreApplication::translate("Core", "No download link for this plugin"));
        emit installFinished(pluginId, false, m_error);
        return false;
    }

    m_pendingInstallId = pluginId;
    setError({});
    setInstalling(pluginId);
    downloadAndInstall(found);
    return true;
}

void PluginCatalogService::finishInstallAttempt(const QString& pluginId, bool ok,
                                                const QString& pathOrError)
{
    emit installFinished(pluginId, ok, pathOrError);
    if (!ok)
        completeInstallAttempt();
}

void PluginCatalogService::completeInstallAttempt()
{
    setInstalling({});
    processInstallQueue();
}

void PluginCatalogService::processInstallQueue()
{
    while (!m_installQueue.isEmpty()) {
        const QString next = m_installQueue.takeFirst();
        if (beginInstall(next))
            return;
    }
    emit installQueueDrained();
}

void PluginCatalogService::downloadAndInstall(const QVariantMap& entry)
{
    if (m_downloadReply) {
        QObject::disconnect(m_downloadReply, nullptr, this, nullptr);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    const QString url = downloadUrlForEntry(entry);
    const QString expectedSha = entry.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    const QString pluginId = entry.value(QStringLiteral("id")).toString();

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/arachnel-plugins");
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + pluginId + QStringLiteral(".arach");
    QFile::remove(path);

    auto* outFile = new QFile(path, this);
    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = QCoreApplication::translate("Core", "Could not save plugin file");
        setError(err);
        outFile->deleteLater();
        finishInstallAttempt(pluginId, false, err);
        return;
    }

    auto* hasher = new QCryptographicHash(QCryptographicHash::Sha256);
    setDownloadProgress(0);

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Arachnel/%1").arg(QCoreApplication::applicationVersion()));

    QNetworkReply* reply = m_network->get(request);
    m_downloadReply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
                if (m_downloadReply != reply)
                    return;
                if (total > 0)
                    setDownloadProgress(static_cast<int>((received * 100) / total));
            });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, outFile, hasher]() {
        if (m_downloadReply != reply || !outFile)
            return;
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty())
            return;
        hasher->addData(chunk);
        outFile->write(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, expectedSha, pluginId, outFile, hasher, path]() {
                if (m_downloadReply == reply)
                    m_downloadReply = nullptr;
                const auto cleanup = qScopeGuard([outFile, hasher, reply]() {
                    if (outFile) {
                        outFile->close();
                        outFile->deleteLater();
                    }
                    delete hasher;
                    reply->deleteLater();
                });

                // Flush any remaining buffered bytes.
                const QByteArray rest = reply->readAll();
                if (!rest.isEmpty()) {
                    hasher->addData(rest);
                    outFile->write(rest);
                }
                outFile->close();

                if (reply->error() != QNetworkReply::NoError) {
                    QFile::remove(path);
                    if (reply->error() == QNetworkReply::OperationCanceledError) {
                        finishInstallAttempt(pluginId, false,
                                             QCoreApplication::translate("Core", "Download failed"));
                        return;
                    }
                    const QString err =
                        QCoreApplication::translate("Core", "Download failed: %1")
                            .arg(reply->errorString());
                    setError(err);
                    finishInstallAttempt(pluginId, false, err);
                    return;
                }

                if (outFile->size() <= 0) {
                    QFile::remove(path);
                    const QString err =
                        QCoreApplication::translate("Core", "Downloaded plugin file is empty");
                    setError(err);
                    finishInstallAttempt(pluginId, false, err);
                    return;
                }

                if (!expectedSha.isEmpty()) {
                    const QString actual =
                        QString::fromLatin1(hasher->result().toHex()).toLower();
                    if (actual != expectedSha) {
                        QFile::remove(path);
                        const QString err = QCoreApplication::translate(
                            "Core", "Plugin file checksum mismatch");
                        setError(err);
                        finishInstallAttempt(pluginId, false, err);
                        return;
                    }
                }

                setDownloadProgress(100);
                finishInstallAttempt(pluginId, true, path);
            });
}
} // namespace arachnel::core
