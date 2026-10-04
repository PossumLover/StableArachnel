#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace arachnel::core {
class SettingsStore;

class TorBoxDownloadSession : public QObject
{
    Q_OBJECT
public:
    explicit TorBoxDownloadSession(SettingsStore* settings, QObject* parent = nullptr,
                                  const QUrl& api = QUrl(QStringLiteral("https://api.torbox.app/v1/api")),
                                  QNetworkAccessManager* network = nullptr);
    ~TorBoxDownloadSession() override;
    void addJob(const QString& id, const QString& magnet, const QString& savePath, qint64 torrentId = -1);
    void cancel(const QString& id);
    void setPaused(const QString& id, bool paused);
    void shutdown();

signals:
    void progress(const QString& id, int percent, qint64 downloaded, qint64 total);
    void phase(const QString& id, const QString& status, const QString& detail);
    void torrentRegistered(const QString& id, qint64 torrentId);
    void finished(const QString& id, const QString& path);
    void failed(const QString& id, const QString& error);

private:
    struct Transfer;
    using TransferPtr = std::shared_ptr<Transfer>;
    bool current(const TransferPtr& transfer) const;
    void pump();
    void stop(const TransferPtr& transfer);
    void fail(const TransferPtr& transfer, const QString& error);
    void apiRequest(const TransferPtr& transfer, const QString& path,
                    std::function<void(const QJsonValue&)> done, const QString& magnet = {});
    void createTorrent(const TransferPtr& transfer);
    void pollTorrent(const TransferPtr& transfer);
    void prepareFiles(const TransferPtr& transfer, const QJsonObject& torrent);
    void downloadNext(const TransferPtr& transfer);
    void downloadFile(const TransferPtr& transfer, const QUrl& url);
    void finishFile(const TransferPtr& transfer);

    SettingsStore* m_settings;
    QUrl m_api;
    QNetworkAccessManager* m_network;
    QHash<QString, TransferPtr> m_transfers;
};

} // namespace arachnel::core
