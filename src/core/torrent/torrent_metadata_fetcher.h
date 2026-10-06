#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTimer;

namespace arachnel::core {

QString magnetInfoHashKey(const QString& magnetUri);

/**
 * Reads a magnet link's file list without downloading the files, to guess how a catalog
 * entry installs. One link at a time, on one libtorrent session that lives while there is
 * work, driven from the owning thread by alerts.
 *
 * The old probe built and tore down a whole session per link on a worker thread and polled
 * torrent_handle::status(), which blocks on libtorrent's network thread. On Windows that
 * wait crashed with an invalid handle; nothing here waits on the network thread at all.
 */
class MagnetMetadataProbe : public QObject
{
    Q_OBJECT

public:
    explicit MagnetMetadataProbe(QObject* parent = nullptr);
    ~MagnetMetadataProbe() override;

    bool busy() const { return m_busy; }
    /** Start reading the file list. False when a probe is running or the link can't be parsed. */
    bool start(const QString& magnetUri, int timeoutMs = 12000);
    /** Drop the running probe and the session without a finished() signal. Never waits. */
    void cancel();
    /** How long the session outlives the last probe. */
    void setIdleTimeout(int ms);

signals:
    /** fileNames is empty when the list could not be read in time. */
    void finished(const QString& magnetUri, const QStringList& fileNames);

private:
    struct Session;

    void poll();
    void finish(const QStringList& fileNames);
    void dropIdleSession();
    void releaseSession();

    std::unique_ptr<Session> m_session;
    QTimer* m_pollTimer = nullptr;
    QTimer* m_idleTimer = nullptr;
    QString m_magnetUri;
    qint64 m_deadlineMs = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
};

} // namespace arachnel::core
