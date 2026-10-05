#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
namespace arachnel::core {
class SettingsStore;

class TorBoxCacheService : public QObject {
    Q_OBJECT
public:
    explicit TorBoxCacheService(SettingsStore* settings, QObject* parent = nullptr,
        QNetworkAccessManager* network = nullptr,
        const QUrl& baseUrl = QUrl(QStringLiteral("https://api.torbox.app/v1/api")));
    static QString magnetHash(const QString& magnet);
    void check(const QStringList& hashes);
    QString status(const QString& hash) const;
signals:
    void changed();
private:
    struct Entry { QString status; qint64 expiresAt = 0; };
    void sendBatch();
    void reset();
    bool enabled() const;
    SettingsStore* m_settings;
    QNetworkAccessManager* m_network;
    QNetworkReply* m_reply = nullptr;
    QTimer* m_timer;
    QUrl m_baseUrl;
    QHash<QString, Entry> m_cache;
    QSet<QString> m_pending;
    qint64 m_retryAt = 0;
};
} // namespace arachnel::core
