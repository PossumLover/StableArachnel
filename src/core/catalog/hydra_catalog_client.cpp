#include "hydra_catalog_client.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace arachnel::core {

HydraCatalogClient::HydraCatalogClient(QObject* parent, const QUrl& api, QNetworkAccessManager* network)
    : QObject(parent), m_api(api), m_network(network ? network : new QNetworkAccessManager(this))
{
}

bool HydraCatalogClient::isHydraSource(const QUrl& url)
{
    const QString host = url.host().toLower();
    return url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty()
        && (host == QStringLiteral("hydralinks.cloud")
            || host == QStringLiteral("hydralinks.pages.dev"));
}

void HydraCatalogClient::cancel()
{
    ++m_serial;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
}

void HydraCatalogClient::request(const QString& path, const QJsonObject& body,
                                std::function<void(const QJsonDocument&)> done, bool post)
{
    QNetworkRequest req(QUrl(m_api.toString() + path));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sprout/" ARACHNEL_VERSION));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);
    const quint64 serial = m_serial;
    QNetworkReply* reply = post ? m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact))
                               : m_network->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial, done = std::move(done)]() {
        const QByteArray bytes = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply.clear();
        reply->deleteLater();
        if (serial != m_serial)
            return;
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (!ok || parseError.error != QJsonParseError::NoError) {
            emit failed(QCoreApplication::translate("Core", "Could not load this source through Hydra (HTTP %1)").arg(status));
            return;
        }
        done(doc);
    });
}

void HydraCatalogClient::load(const QUrl& source)
{
    cancel();
    m_entries = {};
    m_skip = 0;
    m_attempts = 0;
    registerSource(source);
}

void HydraCatalogClient::registerSource(const QUrl& source)
{
    ++m_attempts;
    request(QStringLiteral("/download-sources"), {{QStringLiteral("url"), source.toString()}},
            [this, source](const QJsonDocument& doc) {
        const auto obj = doc.object();
        m_sourceId = obj.value(QStringLiteral("id")).toString();
        m_fingerprint = obj.value(QStringLiteral("fingerprint")).toString();
        if (m_sourceId.isEmpty() || m_fingerprint.isEmpty()) {
            if (m_attempts < 6) {
                const quint64 serial = m_serial;
                QTimer::singleShot(10000, this, [this, source, serial]() {
                    if (serial == m_serial)
                        registerSource(source);
                });
            } else {
                emit failed(QCoreApplication::translate("Core", "Hydra is still indexing this source. Try again later."));
            }
            return;
        }
        loadPage();
    }, true);
}

void HydraCatalogClient::loadPage()
{
    const QJsonObject body{{QStringLiteral("take"), 100}, {QStringLiteral("skip"), m_skip},
                          {QStringLiteral("downloadSourceIds"), QJsonArray{m_sourceId}},
                          {QStringLiteral("downloadSourceFingerprints"), QJsonArray{m_fingerprint}}};
    request(QStringLiteral("/catalogue/search"), body, [this](const QJsonDocument& doc) {
        const auto obj = doc.object();
        if (!obj.value(QStringLiteral("edges")).isArray()) {
            emit failed(QCoreApplication::translate("Core", "Hydra returned an invalid catalog"));
            return;
        }
        const auto edges = obj.value(QStringLiteral("edges")).toArray();
        for (const auto& value : edges) {
            const auto game = value.toObject();
            const QString shop = game.value(QStringLiteral("shop")).toString();
            const QString id = game.value(QStringLiteral("objectId")).toString();
            if (shop.isEmpty() || id.isEmpty() || game.value(QStringLiteral("title")).toString().isEmpty())
                continue;
            QUrl reference;
            reference.setScheme(QStringLiteral("hydra"));
            reference.setHost(shop);
            reference.setPath(QLatin1Char('/') + id);
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("source"), m_sourceId);
            reference.setQuery(query);
            QJsonObject entry{{QStringLiteral("id"), QStringLiteral("hydra-%1-%2-%3").arg(m_sourceId, shop, id)},
                              {QStringLiteral("title"), game.value(QStringLiteral("title"))},
                              {QStringLiteral("uris"), QJsonArray{reference.toString()}}};
            if (shop == QStringLiteral("steam"))
                entry.insert(QStringLiteral("steamAppId"), id);
            m_entries.append(entry);
        }
        m_skip += edges.size();
        if (!edges.isEmpty() && m_skip < obj.value(QStringLiteral("count")).toInt() && m_skip < 100000) {
            loadPage();
            return;
        }
        emit loaded(QJsonDocument(QJsonObject{{QStringLiteral("entries"), m_entries}}).toJson(QJsonDocument::Compact));
    }, true);
}

void HydraCatalogClient::resolve(const QString& reference)
{
    cancel();
    const QUrl url(reference);
    const QString source = QUrlQuery(url).queryItemValue(QStringLiteral("source"));
    const QString id = url.path().mid(1);
    if (url.scheme() != QStringLiteral("hydra") || source.isEmpty() || id.isEmpty()
        || id.contains(QLatin1Char('/')) || url.host().isEmpty()) {
        emit failed(QCoreApplication::translate("Core", "Invalid Hydra download reference"));
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("take"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("skip"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("downloadSourceIds[]"), source);
    request(QStringLiteral("/games/%1/%2/download-sources?%3").arg(url.host(), QString::fromLatin1(QUrl::toPercentEncoding(id)), query.toString(QUrl::FullyEncoded)), {},
            [this, source](const QJsonDocument& doc) {
        QList<QJsonObject> options;
        for (const auto& value : doc.array()) {
            const auto option = value.toObject();
            if (option.value(QStringLiteral("downloadSourceId")).toString() == source)
                options.append(option);
        }
        std::stable_sort(options.begin(), options.end(), [](const auto& a, const auto& b) {
            return a.value(QStringLiteral("uploadDate")).toString() > b.value(QStringLiteral("uploadDate")).toString();
        });
        for (const auto& option : options) {
            const auto unavailable = option.value(QStringLiteral("unavailableUris")).toArray();
            for (const auto& value : option.value(QStringLiteral("uris")).toArray()) {
                const QString uri = value.toString();
                if (!unavailable.contains(value) && uri.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) {
                    emit resolved(uri);
                    return;
                }
            }
            for (const auto& value : option.value(QStringLiteral("uris")).toArray()) {
                const QString uri = value.toString();
                const QUrl downloadUrl(uri);
                const bool supported = (downloadUrl.scheme() == QStringLiteral("https") || downloadUrl.scheme() == QStringLiteral("http"))
                    && !downloadUrl.host().isEmpty() && downloadUrl.userInfo().isEmpty();
                if (!unavailable.contains(value) && supported) {
                    emit resolved(uri);
                    return;
                }
            }
        }
        emit failed(QCoreApplication::translate("Core", "No available download link in this Hydra source"));
    });
}

} // namespace arachnel::core
