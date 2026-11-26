#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QLocalServer;
class QLocalSocket;

namespace arachnel {

/** Register arachnel:// URL protocol (Windows HKCU; Linux best-effort desktop mime). */
void registerGameDeepLinkProtocol();

/** First argv entry that looks like arachnel://… (empty if none). */
QString findDeepLinkArgument(const QStringList& arguments);

/** Parse game entry id from arachnel://game/<id>, https://…/open/game/<id>, or bare id. */
QString parseDeepLinkGameId(const QString& rawOrUrl);

/** Bring Arachnel top-level windows to the foreground (deep-link / second instance). */
void forceActivateApplicationWindows();

/**
 * Single-instance gate via QLocalServer.
 * Secondary processes forward an optional deep-link payload and exit.
 */
class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QObject* parent = nullptr);
    ~SingleInstanceGuard() override;

    bool tryBecomePrimary();
    /**
     * Hand the payload to the primary. Returns false when the primary answered that it is
     * shutting down: the caller should wait for it to exit and start in its place.
     */
    bool forwardToPrimary(const QString& payload);
    /**
     * Wait (pumping nothing) until the shutting-down primary is gone, then take over.
     * Returns true once this process is the primary.
     */
    bool waitToBecomePrimary(int timeoutMs);

    /** While this returns true, the primary refuses messages and tells the sender so. */
    void setShuttingDownCheck(std::function<bool()> check) { m_shuttingDown = std::move(check); }

signals:
    void messageReceived(const QString& payload);

private:
    void onNewConnection();
    void readSocket(QLocalSocket* socket);

    QString m_serverName;
    QLocalServer* m_server = nullptr;
    std::function<bool()> m_shuttingDown;
};

} // namespace arachnel
