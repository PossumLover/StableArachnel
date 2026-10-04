#include "catalog_feed_loader.h"

#include "catalog_disk_cache.h"
#include "catalog_parser.h"
#include "hydra_catalog_client.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>
#include <QtConcurrent>

namespace arachnel::core {

namespace {

/** Plain words for the refusals a catalog site gives on purpose; empty for anything else. */
QString refusedFeedMessage(const QNetworkReply* reply, int httpStatus)
{
    // Cloudflare marks a challenged request with this header (status 403 or 503). Only a
    // web browser can pass the check, so retrying from here never helps.
    if (reply->rawHeader("cf-mitigated").trimmed().compare("challenge", Qt::CaseInsensitive) == 0) {
        return QCoreApplication::translate(
            "Core", "This site only lets web browsers in (a Cloudflare check), so Sprout "
                    "can't load the catalog from it.");
    }
    if (httpStatus == 451) {
        return QCoreApplication::translate(
            "Core", "This catalog was taken down for legal reasons (HTTP 451).");
    }
    return {};
}

} // namespace

CatalogFeedLoader::CatalogFeedLoader(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_hydra(new HydraCatalogClient(this))
{
    connect(m_hydra, &HydraCatalogClient::loaded, this, [this](const QByteArray& payload) {
        parsePayload(m_hydraSourceId, payload, {}, m_requestSerial);
    });
    connect(m_hydra, &HydraCatalogClient::failed, this, [this](const QString& error) {
        emit feedFailed(m_hydraSourceId, error);
    });
}

void CatalogFeedLoader::cancelActive()
{
    ++m_requestSerial;
    m_hydra->cancel();
    if (!m_activeReply)
        return;
    QNetworkReply* reply = m_activeReply.data();
    m_activeReply.clear();
    if (!reply)
        return;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void CatalogFeedLoader::loadFeed(const QUrl& url, const QString& sourceId, const QByteArray& etag)
{
    cancelActive();

    const quint64 serial = ++m_requestSerial;
    if (HydraCatalogClient::isHydraSource(url)) {
        m_hydraSourceId = sourceId;
        m_hydra->load(url);
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Arachnel/0.1"));
    request.setTransferTimeout(30000);
    if (!etag.isEmpty())
        request.setRawHeader("If-None-Match", etag);
    QNetworkReply* reply = m_network->get(request);
    reply->setProperty("sourceId", sourceId);
    reply->setProperty("requestSerial", QVariant::fromValue(serial));
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleFinished(reply); });
}

void CatalogFeedLoader::handleFinished(QNetworkReply* reply)
{
    const QString sourceId = reply->property("sourceId").toString();
    const quint64 serial = reply->property("requestSerial").toULongLong();
    const bool isActive = (m_activeReply == reply);
    if (isActive)
        m_activeReply.clear();

    if (serial != m_requestSerial || !isActive) {
        reply->deleteLater();
        return;
    }

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        reply->deleteLater();
        return;
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus == 304) {
        emit feedNotModified(sourceId);
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString refused = refusedFeedMessage(reply, httpStatus);
        emit feedFailed(sourceId, refused.isEmpty() ? reply->errorString() : refused);
        reply->deleteLater();
        return;
    }

    const QByteArray payload = reply->readAll();
    const QByteArray etag = reply->rawHeader("ETag");
    reply->deleteLater();

    parsePayload(sourceId, payload, etag, serial);
}

void CatalogFeedLoader::parsePayload(const QString& sourceId, const QByteArray& payload,
                                    const QByteArray& etag, quint64 serial)
{
    const bool countOnly = sourceId.startsWith(QStringLiteral("count:"));
    const quint64 capturedSerial = serial;

    (void)QtConcurrent::run([this, sourceId, payload, etag, countOnly, capturedSerial]() {
        if (countOnly) {
            const int count = catalogFeedQuickCount(payload);
            QTimer::singleShot(0, this, [this, sourceId, count, capturedSerial]() {
                if (capturedSerial != m_requestSerial)
                    return;
                if (count < 0) {
                    emit feedFailed(sourceId,
                                    QCoreApplication::translate(
                                        "Core", "Catalog is empty or format not recognized"));
                    return;
                }
                emit feedCountLoaded(sourceId, count);
            });
            return;
        }

        const QString validationError = catalogFeedValidationError(payload);
        if (!validationError.isEmpty()) {
            QTimer::singleShot(0, this, [this, sourceId, validationError, capturedSerial]() {
                if (capturedSerial != m_requestSerial)
                    return;
                emit feedFailed(sourceId, validationError);
            });
            return;
        }

        QString parseSourceId = sourceId;
        if (parseSourceId.startsWith(QStringLiteral("count:")))
            parseSourceId = parseSourceId.mid(6);

        QVector<CatalogEntry> entries = parseCatalogFeed(payload, parseSourceId);
        const QByteArray sha = CatalogDiskCache::payloadSha256(payload);
        if (!entries.isEmpty() && !sourceId.startsWith(QStringLiteral("validate:")))
            CatalogDiskCache::savePayload(parseSourceId, payload, etag);

        QTimer::singleShot(0, this,
                           [this, sourceId, entries = std::move(entries), sha,
                            capturedSerial]() mutable {
                               if (capturedSerial != m_requestSerial)
                                   return;
                               if (entries.isEmpty()) {
                                   emit feedFailed(sourceId,
                                                   QCoreApplication::translate(
                                                       "Core",
                                                       "Catalog is empty or format not recognized"));
                                   return;
                               }
                               emit feedLoaded(sourceId, std::move(entries), sha);
                           });
    });
}

} // namespace arachnel::core
