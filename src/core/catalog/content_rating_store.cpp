#include "content_rating_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace arachnel::core {

namespace {

// GetItems rejects much more than ~250 ids (URL length); 200 leaves headroom.
constexpr int kBatchSize = 200;
// Steam answers 429 after ~150 back-to-back requests; one a second stays under it.
// A full 125k-app catalog takes ~10 minutes once, then only new apps are fetched.
constexpr int kPaceMs = 1000;
constexpr int kMaxBackoffMs = 5 * 60 * 1000;
constexpr int kFlushEveryBatches = 20;
constexpr quint8 kAdultMask = (1u << 3) | (1u << 4);

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/content-descriptors.json");
}

} // namespace

ContentRatingStore::ContentRatingStore(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_pacer(new QTimer(this))
{
    m_pacer->setSingleShot(true);
    connect(m_pacer, &QTimer::timeout, this, &ContentRatingStore::fetchNext);
    load();
}

bool ContentRatingStore::isAdult(const QString& steamAppId) const
{
    if (m_adult.isEmpty() || steamAppId.isEmpty())
        return false;
    bool ok = false;
    const quint32 appId = steamAppId.toUInt(&ok);
    return ok && m_adult.contains(appId);
}

void ContentRatingStore::requestMissing(const QVector<CatalogEntry>& entries)
{
    const int before = m_queue.size();
    for (const CatalogEntry& entry : entries) {
        bool ok = false;
        const quint32 appId = entry.steamAppId.toUInt(&ok);
        if (!ok || appId == 0 || m_checked.contains(appId) || m_queued.contains(appId))
            continue;
        m_queued.insert(appId);
        m_queue.append(appId);
    }
    if (m_queue.size() != before && !m_inFlight && !m_pacer->isActive())
        m_pacer->start(0);
}

void ContentRatingStore::fetchNext()
{
    if (m_inFlight)
        return;
    if (m_queue.isEmpty()) {
        flush();
        return;
    }

    const int n = std::min<int>(kBatchSize, m_queue.size());
    const QVector<quint32> batch = m_queue.mid(0, n);

    QJsonArray ids;
    for (quint32 appId : batch)
        ids.append(QJsonObject{{QStringLiteral("appid"), static_cast<qint64>(appId)}});
    const QJsonObject payload{
        {QStringLiteral("ids"), ids},
        {QStringLiteral("context"),
         QJsonObject{{QStringLiteral("language"), QStringLiteral("english")},
                     {QStringLiteral("country_code"), QStringLiteral("US")}}},
        {QStringLiteral("data_request"), QJsonObject{}},
    };
    QUrl url(QStringLiteral("https://api.steampowered.com/IStoreBrowseService/GetItems/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("input_json"),
                       QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Arachnel/0.1"));
    request.setTransferTimeout(60000);
    m_inFlight = true;
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, batch]() { handleReply(reply, batch); });
}

void ContentRatingStore::handleReply(QNetworkReply* reply, const QVector<quint32>& batch)
{
    reply->deleteLater();
    m_inFlight = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        // Rate limited or offline: keep the batch queued and back off.
        m_backoffMs = m_backoffMs == 0 ? 30000 : std::min(m_backoffMs * 2, kMaxBackoffMs);
        flush();
        m_pacer->start(m_backoffMs);
        return;
    }
    m_backoffMs = 0;

    const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                                 .object()
                                 .value(QStringLiteral("response"))
                                 .toObject()
                                 .value(QStringLiteral("store_items"))
                                 .toArray();
    bool adultChanged = false;
    for (const QJsonValue& value : items) {
        const QJsonObject item = value.toObject();
        const quint32 appId = static_cast<quint32>(item.value(QStringLiteral("appid")).toInteger());
        if (appId == 0)
            continue;
        quint8 mask = 0;
        for (const QJsonValue& id : item.value(QStringLiteral("content_descriptorids")).toArray()) {
            const int descriptor = id.toInt();
            if (descriptor > 0 && descriptor < 8)
                mask |= static_cast<quint8>(1u << descriptor);
        }
        if (mask)
            m_descriptorMasks.insert(appId, mask);
        else
            m_descriptorMasks.remove(appId);
        const bool adult = (mask & kAdultMask) != 0;
        if (adult != m_adult.contains(appId)) {
            adultChanged = true;
            if (adult)
                m_adult.insert(appId);
            else
                m_adult.remove(appId);
        }
    }
    // Apps Steam no longer lists come back empty; they're checked all the same.
    for (quint32 appId : batch) {
        m_checked.insert(appId);
        m_queued.remove(appId);
    }
    m_queue.remove(0, std::min<int>(batch.size(), m_queue.size()));
    m_dirty = true;
    if (adultChanged)
        m_adultDirty = true;

    if (++m_batchesSinceFlush >= kFlushEveryBatches || m_queue.isEmpty())
        flush();
    if (!m_queue.isEmpty())
        m_pacer->start(kPaceMs);
}

void ContentRatingStore::flush()
{
    m_batchesSinceFlush = 0;
    if (m_dirty) {
        save();
        m_dirty = false;
    }
    if (m_adultDirty) {
        m_adultDirty = false;
        emit ratingsChanged();
    }
}

void ContentRatingStore::load()
{
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return;
    for (const QJsonValue& value : root.value(QStringLiteral("checked")).toArray())
        m_checked.insert(static_cast<quint32>(value.toInteger()));
    const QJsonObject masks = root.value(QStringLiteral("descriptors")).toObject();
    for (auto it = masks.begin(); it != masks.end(); ++it) {
        const quint32 appId = it.key().toUInt();
        const quint8 mask = static_cast<quint8>(it.value().toInt());
        if (appId == 0 || mask == 0)
            continue;
        m_descriptorMasks.insert(appId, mask);
        if (mask & kAdultMask)
            m_adult.insert(appId);
    }
}

void ContentRatingStore::save() const
{
    QVector<quint32> checked(m_checked.cbegin(), m_checked.cend());
    std::sort(checked.begin(), checked.end());
    QJsonArray checkedJson;
    for (quint32 appId : checked)
        checkedJson.append(static_cast<qint64>(appId));
    QJsonObject masks;
    for (auto it = m_descriptorMasks.cbegin(); it != m_descriptorMasks.cend(); ++it)
        masks.insert(QString::number(it.key()), it.value());
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("note"),
         QStringLiteral("Steam content descriptor bitmask per app (bit n = descriptor n)")},
        {QStringLiteral("descriptors"), masks},
        {QStringLiteral("checked"), checkedJson},
    };
    QSaveFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace arachnel::core
