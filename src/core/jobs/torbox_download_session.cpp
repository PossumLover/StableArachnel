#include "torbox_download_session.h"

#include "catalog_parser.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

namespace arachnel::core {

struct TorBoxDownloadSession::Transfer {
    struct File { qint64 id; QString path; qint64 size; };
    QString id;
    QString magnet;
    QString key;
    QString savePath;
    QString root;
    qint64 torrentId = -1;
    QVector<File> files;
    int index = 0;
    qint64 completed = 0;
    qint64 total = 0;
    qint64 offset = 0;
    bool paused = false;
    bool started = false;
    bool headersChecked = false;
    QPointer<QNetworkReply> reply;
    QFile output;
};

TorBoxDownloadSession::TorBoxDownloadSession(SettingsStore* settings, QObject* parent, const QUrl& api,
                                           QNetworkAccessManager* network)
    : QObject(parent), m_settings(settings), m_api(api), m_network(network ? network : new QNetworkAccessManager(this))
{
    connect(settings, &SettingsStore::maxConcurrentDownloadsChanged, this, &TorBoxDownloadSession::pump);
}

TorBoxDownloadSession::~TorBoxDownloadSession()
{
    shutdown();
}

bool TorBoxDownloadSession::current(const TransferPtr& t) const
{
    return m_transfers.value(t->id) == t && !t->paused;
}

void TorBoxDownloadSession::stop(const TransferPtr& t)
{
    if (t->reply) {
        t->reply->disconnect(this);
        t->reply->abort();
        t->reply->deleteLater();
        t->reply.clear();
    }
    t->output.close();
}

void TorBoxDownloadSession::shutdown()
{
    const auto transfers = m_transfers;
    m_transfers.clear();
    for (const auto& t : transfers)
        stop(t);
}

void TorBoxDownloadSession::cancel(const QString& id)
{
    const auto t = m_transfers.take(id);
    if (t)
        stop(t);
    pump();
}

void TorBoxDownloadSession::setPaused(const QString& id, bool paused)
{
    const auto t = m_transfers.value(id);
    if (!t)
        return;
    if (paused) {
        t->paused = true;
        t->started = false;
        stop(t);
        pump();
    } else if (t->paused) {
        addJob(id, t->magnet, t->savePath, t->torrentId);
    }
}

void TorBoxDownloadSession::addJob(const QString& id, const QString& magnet,
                                 const QString& savePath, qint64 torrentId)
{
    const auto previous = m_transfers.take(id);
    if (previous)
        stop(previous);
    auto t = std::make_shared<Transfer>();
    t->id = id;
    t->magnet = magnet;
    t->key = m_settings->torboxApiKey();
    t->savePath = savePath;
    t->torrentId = torrentId;
    m_transfers.insert(id, t);
    QTimer::singleShot(0, this, &TorBoxDownloadSession::pump);
}

void TorBoxDownloadSession::pump()
{
    int active = 0;
    for (const auto& t : std::as_const(m_transfers)) {
        if (t->started && !t->paused)
            ++active;
    }
    const auto transfers = m_transfers;
    for (const auto& t : transfers) {
        if (!current(t) || t->started)
            continue;
        if (active >= m_settings->maxConcurrentDownloads()) {
            emit phase(t->id, QStringLiteral("queued"), QCoreApplication::translate("Core", "Waiting for a download slot"));
            continue;
        }
        t->started = true;
        ++active;
        const QString hash = catalogMagnetInfoHash(t->magnet);
        if (t->key.isEmpty() || !QRegularExpression(QStringLiteral("^[a-f0-9]{40}$")).match(hash).hasMatch()) {
            fail(t, QCoreApplication::translate("Core", "TorBox needs an API key and a valid magnet link"));
            continue;
        }
        t->root = t->savePath + QStringLiteral("/torbox-") + hash;
        emit phase(t->id, QStringLiteral("starting"), QCoreApplication::translate("Core", "Preparing TorBox download"));
        if (t->torrentId >= 0)
            pollTorrent(t);
        else
            createTorrent(t);
    }
}

void TorBoxDownloadSession::fail(const TransferPtr& t, const QString& error)
{
    if (!current(t))
        return;
    m_transfers.remove(t->id);
    stop(t);
    QString message = error;
    if (!t->key.isEmpty())
        message.replace(t->key, QStringLiteral("[redacted]"));
    emit failed(t->id, message);
    QTimer::singleShot(0, this, &TorBoxDownloadSession::pump);
}

void TorBoxDownloadSession::apiRequest(const TransferPtr& t, const QString& path,
                                     std::function<void(const QJsonValue&)> done, const QString& magnet)
{
    if (!current(t))
        return;
    QNetworkRequest req(QUrl(m_api.toString() + path));
    req.setRawHeader("Authorization", "Bearer " + t->key.toUtf8());
    req.setTransferTimeout(30000);
    QNetworkReply* reply;
    if (magnet.isEmpty()) {
        reply = m_network->get(req);
    } else {
        auto* form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(QStringLiteral("form-data; name=\"magnet\"")));
        part.setBody(magnet.toUtf8());
        form->append(part);
        reply = m_network->post(req, form);
        form->setParent(reply);
    }
    t->reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, t, reply, path, done = std::move(done), magnet]() {
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        bool hasRetryAfter = false;
        const int seconds = reply->rawHeader("Retry-After").toInt(&hasRetryAfter);
        const int retryAfter = hasRetryAfter ? qBound(1, seconds, 300) : 30;
        t->reply.clear();
        reply->deleteLater();
        if (!current(t))
            return;
        if (status == 429) {
            emit phase(t->id, QStringLiteral("starting"), QCoreApplication::translate("Core", "TorBox is busy. Retrying shortly."));
            QTimer::singleShot(retryAfter * 1000, this, [this, t, path, done, magnet]() {
                if (current(t))
                    apiRequest(t, path, done, magnet);
            });
            return;
        }
        if (!ok || !obj.value(QStringLiteral("success")).toBool()) {
            const QString detail = obj.value(QStringLiteral("detail")).toString();
            fail(t, detail.isEmpty() ? QCoreApplication::translate("Core", "TorBox request failed (HTTP %1)").arg(status)
                                    : QCoreApplication::translate("Core", "TorBox: %1").arg(detail));
            return;
        }
        done(obj.value(QStringLiteral("data")));
    });
}

void TorBoxDownloadSession::createTorrent(const TransferPtr& t)
{
    apiRequest(t, QStringLiteral("/torrents/createtorrent"), [this, t](const QJsonValue& data) {
        t->torrentId = data.toObject().value(QStringLiteral("torrent_id")).toInteger(-1);
        if (t->torrentId < 0) {
            fail(t, QCoreApplication::translate("Core", "TorBox did not return a torrent ID"));
            return;
        }
        emit torrentRegistered(t->id, t->torrentId);
        pollTorrent(t);
    }, t->magnet);
}

void TorBoxDownloadSession::pollTorrent(const TransferPtr& t)
{
    apiRequest(t, QStringLiteral("/torrents/mylist?id=%1&bypass_cache=true").arg(t->torrentId),
               [this, t](const QJsonValue& data) {
        QJsonObject torrent = data.toObject();
        if (data.isArray()) {
            for (const auto& value : data.toArray()) {
                if (value.toObject().value(QStringLiteral("id")).toInteger(-1) == t->torrentId) {
                    torrent = value.toObject();
                    break;
                }
            }
        }
        if (torrent.isEmpty()) {
            fail(t, QCoreApplication::translate("Core", "This torrent is no longer in your TorBox account. Start a new download."));
            return;
        }
        if (torrent.value(QStringLiteral("download_finished")).toBool()
            && torrent.value(QStringLiteral("download_present")).toBool()) {
            prepareFiles(t, torrent);
            return;
        }
        const QString state = torrent.value(QStringLiteral("download_state")).toString();
        if (state.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
            fail(t, QCoreApplication::translate("Core", "TorBox could not download this torrent"));
            return;
        }
        emit phase(t->id, QStringLiteral("starting"), QCoreApplication::translate("Core", "Waiting for TorBox: %1").arg(state));
        QTimer::singleShot(10000, this, [this, t]() {
            if (current(t))
                pollTorrent(t);
        });
    });
}

void TorBoxDownloadSession::prepareFiles(const TransferPtr& t, const QJsonObject& torrent)
{
    if (QFileInfo(t->root).isSymLink() || !QDir().mkpath(t->root)) {
        fail(t, QCoreApplication::translate("Core", "Could not create the TorBox download folder"));
        return;
    }
    t->root = QFileInfo(t->root).canonicalFilePath();
    QSet<QString> paths;
    for (const auto& value : torrent.value(QStringLiteral("files")).toArray()) {
        const auto file = value.toObject();
        QString name = file.value(QStringLiteral("name")).toString();
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const auto parts = name.split(QLatin1Char('/'));
        const qint64 size = file.value(QStringLiteral("size")).toInteger(-1);
        const qint64 id = file.value(QStringLiteral("id")).toInteger(-1);
        bool safe = !name.isEmpty() && !QDir::isAbsolutePath(name) && !name.contains(QLatin1Char(':')) && size >= 0 && id >= 0;
        QString path = t->root;
        for (const auto& part : parts) {
            if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")
                || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')))
                safe = false;
            path += QLatin1Char('/') + part;
            if (QFileInfo(path).isSymLink() || QFileInfo(path + QStringLiteral(".sprout-part")).isSymLink())
                safe = false;
        }
        QString pathKey = path;
#ifdef Q_OS_WIN
        pathKey = pathKey.toLower();
#endif
        if (!safe || paths.contains(pathKey) || !QDir().mkpath(QFileInfo(path).absolutePath())) {
            fail(t, QCoreApplication::translate("Core", "TorBox returned an unsafe or duplicate file path"));
            return;
        }
        paths.insert(pathKey);
        t->files.append({id, path, size});
        t->total += size;
    }
    if (t->files.isEmpty()) {
        fail(t, QCoreApplication::translate("Core", "TorBox returned no files"));
        return;
    }
    downloadNext(t);
}

void TorBoxDownloadSession::downloadNext(const TransferPtr& t)
{
    if (!current(t))
        return;
    while (t->index < t->files.size()) {
        const auto& file = t->files.at(t->index);
        if (QFileInfo(file.path).isFile() && QFileInfo(file.path).size() == file.size) {
            t->completed += file.size;
            ++t->index;
            continue;
        }
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("token"), t->key);
        query.addQueryItem(QStringLiteral("torrent_id"), QString::number(t->torrentId));
        query.addQueryItem(QStringLiteral("file_id"), QString::number(file.id));
        apiRequest(t, QStringLiteral("/torrents/requestdl?") + query.toString(QUrl::FullyEncoded),
                   [this, t](const QJsonValue& data) {
            const QUrl url(data.toString());
            if (!url.isValid() || url.scheme() != QStringLiteral("https") || !url.userInfo().isEmpty()) {
                fail(t, QCoreApplication::translate("Core", "TorBox did not return a valid download link"));
                return;
            }
            downloadFile(t, url);
        });
        return;
    }
    m_transfers.remove(t->id);
    emit progress(t->id, 100, t->total, t->total);
    emit finished(t->id, t->root);
    QTimer::singleShot(0, this, &TorBoxDownloadSession::pump);
}

void TorBoxDownloadSession::downloadFile(const TransferPtr& t, const QUrl& url)
{
    const auto& file = t->files.at(t->index);
    t->output.setFileName(file.path + QStringLiteral(".sprout-part"));
    if (!t->output.open(QIODevice::ReadWrite)) {
        fail(t, QCoreApplication::translate("Core", "Could not write the TorBox download file"));
        return;
    }
    t->offset = t->output.size();
    if (t->offset == file.size) {
        finishFile(t);
        return;
    }
    if (t->offset > file.size) {
        if (!t->output.resize(0)) {
            fail(t, QCoreApplication::translate("Core", "Could not reset the partial download"));
            return;
        }
        t->offset = 0;
    }
    t->output.seek(t->offset);
    QNetworkRequest req(url);
    req.setTransferTimeout(60000);
    req.setRawHeader("Accept-Encoding", "identity");
    if (t->offset > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(t->offset) + '-');
    auto* reply = m_network->get(req);
    reply->setReadBufferSize(1024 * 1024);
    t->headersChecked = false;
    t->reply = reply;
    auto drain = [this, t, reply]() {
        if (!current(t) || !t->output.isOpen())
            return;
        const auto& file = t->files.at(t->index);
        if (!t->headersChecked) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200 && status != 206)
                return;
            if (status == 206) {
                const auto range = QRegularExpression(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+)$"))
                                       .match(QString::fromLatin1(reply->rawHeader("Content-Range")));
                if (!range.hasMatch() || range.captured(1).toLongLong() != t->offset
                    || range.captured(3).toLongLong() != file.size) {
                    fail(t, QCoreApplication::translate("Core", "The server returned an invalid download range"));
                    return;
                }
            } else if (t->offset > 0) {
                if (!t->output.resize(0) || !t->output.seek(0)) {
                    fail(t, QCoreApplication::translate("Core", "Could not reset the partial download"));
                    return;
                }
                t->offset = 0;
            }
            t->headersChecked = true;
        }
        while (reply->bytesAvailable() > 0) {
            const QByteArray chunk = reply->read(256 * 1024);
            if (t->output.pos() + chunk.size() > file.size || t->output.write(chunk) != chunk.size()) {
                fail(t, QCoreApplication::translate("Core", "Could not write the TorBox download file"));
                return;
            }
        }
        const qint64 downloaded = t->completed + t->output.pos();
        emit progress(t->id, t->total > 0 ? static_cast<int>(downloaded * 100 / t->total) : 0, downloaded, t->total);
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, t, reply, drain]() {
        drain();
        if (!current(t))
            return;
        const auto file = t->files.at(t->index);
        const bool ok = reply->error() == QNetworkReply::NoError && t->headersChecked
            && t->output.size() == file.size && t->output.flush();
        t->output.close();
        t->reply.clear();
        reply->deleteLater();
        if (!ok) {
            fail(t, QCoreApplication::translate("Core", "TorBox download was interrupted. Retry to resume."));
            return;
        }
        finishFile(t);
    });
}

void TorBoxDownloadSession::finishFile(const TransferPtr& t)
{
    const auto file = t->files.at(t->index);
    t->output.close();
    if (QFileInfo::exists(file.path) && !QFile::remove(file.path)) {
        fail(t, QCoreApplication::translate("Core", "Could not replace the download file"));
        return;
    }
    if (!QFile::rename(t->output.fileName(), file.path)) {
        fail(t, QCoreApplication::translate("Core", "Could not finish the download file"));
        return;
    }
    t->completed += file.size;
    ++t->index;
    QTimer::singleShot(0, this, [this, t]() { downloadNext(t); });
}

} // namespace arachnel::core
