#include "achievement_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>
#include <algorithm>

namespace arachnel::core {

AchievementService::AchievementService(QObject* parent, QNetworkAccessManager* network,
                                     const QString& cacheDirectory)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)),
      m_cacheDirectory(cacheDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/achievements")
          : cacheDirectory)
{}

QString AchievementService::cachePath(const QString& gameId) const
{
    return m_cacheDirectory + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(gameId.toUtf8(), QCryptographicHash::Sha256).toHex())
        + QStringLiteral(".json");
}

void AchievementService::load(const QString& gameId)
{
    State state;
    QFile file(cachePath(gameId));
    if (file.open(QIODevice::ReadOnly) && file.size() < 4 * 1024 * 1024) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        state.appId = object.value(QStringLiteral("appId")).toString();
        state.language = object.value(QStringLiteral("language")).toString();
        state.metadata = object.value(QStringLiteral("metadata")).toArray();
        state.fetchedAt = object.value(QStringLiteral("fetchedAt")).toInteger();
        const auto unlocks = object.value(QStringLiteral("unlocks")).toObject();
        // Older caches used the save's capitalization. Merge aliases without
        // replacing a known unlock date with a missing one.
        for (auto it = unlocks.begin(); it != unlocks.end(); ++it) {
            const QString name = it.key().toUpper();
            const qint64 timestamp = it.value().toInteger();
            if (!state.unlocks.contains(name) || (state.unlocks.value(name) == 0 && timestamp > 0))
                state.unlocks.insert(name, timestamp);
        }
    }
    m_states.insert(gameId, state);
}

void AchievementService::save(const QString& gameId)
{
    const auto& state = m_states[gameId];
    QJsonObject unlocks;
    for (auto it = state.unlocks.begin(); it != state.unlocks.end(); ++it)
        unlocks.insert(it.key(), it.value());
    const QJsonObject object{{QStringLiteral("appId"), state.appId},
        {QStringLiteral("language"), state.language}, {QStringLiteral("metadata"), state.metadata},
        {QStringLiteral("fetchedAt"), state.fetchedAt}, {QStringLiteral("unlocks"), unlocks}};
    QDir().mkpath(m_cacheDirectory);
    QSaveFile file(cachePath(gameId));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void AchievementService::refresh(const QString& gameId, const QString& appId, const QString& language,
                                 const AchievementLocations& locations, bool force, bool notifyUnlocks)
{
    static const QRegularExpression numericId(QStringLiteral("^[0-9]+$"));
    if (gameId.isEmpty() || !numericId.match(appId).hasMatch())
        return;
    if (!m_states.contains(gameId))
        load(gameId);
    auto& state = m_states[gameId];
    const QString lang = language.startsWith(QStringLiteral("ru")) ? QStringLiteral("ru") : QStringLiteral("en");
    if (state.appId != appId) {
        state = State{};
        state.appId = appId;
    }
    if (state.language != lang) {
        state.loading = false;
        state.fetchedAt = 0;
        state.generation = ++m_generation;
    }
    bool unlocksChanged = false;
    QStringList newUnlocks;
    state.localFileFound = false;
    for (const auto& path : achievementFileCandidates(appId, locations)) {
        bool valid = false;
        const auto unlocks = readAchievementUnlocks(path, &valid);
        state.localFileFound |= valid;
        for (auto it = unlocks.begin(); it != unlocks.end(); ++it) {
            const QString name = it.key().toUpper();
            if (!state.unlocks.contains(name) || (state.unlocks.value(name) == 0 && it.value() > 0)) {
                if (!state.unlocks.contains(name) && state.scanned && notifyUnlocks)
                    newUnlocks.append(name);
                state.unlocks.insert(name, it.value());
                unlocksChanged = true;
            }
        }
    }
    state.scanned = true;
    if (unlocksChanged)
        save(gameId);
    for (const auto& name : newUnlocks) {
        QString title = name;
        for (const auto& value : state.metadata) {
            const auto object = value.toObject();
            if (object.value(QStringLiteral("name")).toString().toUpper() == name) {
                title = object.value(QStringLiteral("displayName")).toString(name);
                break;
            }
        }
        emit unlocked(gameId, name, title);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (state.loading || (!force && state.language == lang
        && (now < state.retryAt || (state.fetchedAt > 0 && now - state.fetchedAt < 24 * 60 * 60 * 1000)))) {
        emit changed(gameId);
        return;
    }
    state.loading = true;
    state.language = lang;
    state.error.clear();
    const quint64 generation = state.generation = ++m_generation;
    QUrl url(QStringLiteral("https://hydra-api-us-east-1.losbroxas.org/games/steam/%1/achievements").arg(appId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("language"), lang);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    auto* reply = m_network->get(request);
    emit changed(gameId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, generation]() {
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const bool success = reply->error() == QNetworkReply::NoError && doc.isArray();
        reply->deleteLater();
        auto& state = m_states[gameId];
        if (state.generation != generation)
            return;
        state.loading = false;
        if (success) {
            state.metadata = doc.array();
            state.fetchedAt = QDateTime::currentMSecsSinceEpoch();
            state.retryAt = 0;
            save(gameId);
        } else {
            state.retryAt = QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000;
            state.error = QCoreApplication::translate("Core", "Could not load achievement details.");
        }
        emit changed(gameId);
    });
}

QVariantMap AchievementService::info(const QString& gameId) const
{
    const auto it = m_states.constFind(gameId);
    if (it == m_states.cend())
        return {};
    const auto& state = it.value();
    QVariantList rows;
    int unlockedCount = 0;
    for (const auto& value : state.metadata) {
        const auto object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QString unlockName = name.toUpper();
        const bool unlocked = state.unlocks.contains(unlockName);
        const bool hidden = object.value(QStringLiteral("hidden")).toBool() && !unlocked;
        const QString icon = object.value(unlocked ? QStringLiteral("icon") : QStringLiteral("icongray")).toString();
        rows.append(QVariantMap{{QStringLiteral("name"), name},
            {QStringLiteral("title"), hidden ? QCoreApplication::translate("Core", "Hidden achievement")
                : object.value(QStringLiteral("displayName")).toString(name)},
            {QStringLiteral("description"), hidden ? QString() : object.value(QStringLiteral("description")).toString()},
            {QStringLiteral("icon"), QUrl(icon).scheme() == QStringLiteral("https") ? icon : QString()},
            {QStringLiteral("unlocked"), unlocked}, {QStringLiteral("unlockedAt"), state.unlocks.value(unlockName)}});
        unlockedCount += unlocked;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant& a, const QVariant& b) {
        const auto left = a.toMap();
        const auto right = b.toMap();
        if (left.value(QStringLiteral("unlocked")).toBool() != right.value(QStringLiteral("unlocked")).toBool())
            return left.value(QStringLiteral("unlocked")).toBool();
        return left.value(QStringLiteral("unlockedAt")).toLongLong() > right.value(QStringLiteral("unlockedAt")).toLongLong();
    });
    return {{QStringLiteral("rows"), rows}, {QStringLiteral("total"), rows.size()},
        {QStringLiteral("unlocked"), unlockedCount}, {QStringLiteral("loading"), state.loading},
        {QStringLiteral("localFileFound"), state.localFileFound}, {QStringLiteral("error"), state.error}};
}

} // namespace arachnel::core
