#pragma once

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonDocument;

namespace arachnel::core {

class HydraCatalogClient : public QObject
{
    Q_OBJECT
public:
    explicit HydraCatalogClient(QObject* parent = nullptr,
                                const QUrl& api = QUrl(QStringLiteral("https://hydra-api-us-east-1.losbroxas.org")),
                                QNetworkAccessManager* network = nullptr);
    static bool isHydraSource(const QUrl& url);
    void load(const QUrl& source);
    void resolve(const QString& reference);
    void cancel();

signals:
    void loaded(const QByteArray& payload);
    void resolved(const QString& uri);
    void failed(const QString& error);

private:
    void request(const QString& path, const QJsonObject& body,
                 std::function<void(const QJsonDocument&)> done, bool post = false);
    void registerSource(const QUrl& source);
    void loadPage();

    QUrl m_api;
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_sourceId;
    QString m_fingerprint;
    QJsonArray m_entries;
    int m_skip = 0;
    int m_attempts = 0;
    quint64 m_serial = 0;
};

} // namespace arachnel::core
