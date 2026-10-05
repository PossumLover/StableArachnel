#include "torbox_cache_service.h"
#include "settings_store.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace arachnel::core {
TorBoxCacheService::TorBoxCacheService(SettingsStore* settings, QObject* parent,
    QNetworkAccessManager* network, const QUrl& baseUrl)
    : QObject(parent), m_settings(settings),
      m_network(network ? network : new QNetworkAccessManager(this)),
      m_timer(new QTimer(this)), m_baseUrl(baseUrl)
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &TorBoxCacheService::sendBatch);
    connect(settings, &SettingsStore::debridChanged, this, &TorBoxCacheService::reset);
}

bool TorBoxCacheService::enabled() const
{
    return m_settings->torboxEnabled() && !m_settings->torboxApiKey().isEmpty();
}

QString TorBoxCacheService::magnetHash(const QString& magnet)
{
    const QUrl url(magnet);
    if (url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) != 0)
        return {};
    static const QRegularExpression hex(QStringLiteral("^[a-fA-F0-9]{40}$"));
    static const QRegularExpression base32(QStringLiteral("^[A-Z2-7]{32}$"));
    for (const auto& pair : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
        if (pair.first != QStringLiteral("xt") || !pair.second.startsWith(QStringLiteral("urn:btih:"), Qt::CaseInsensitive))
            continue;
        const QString hash = pair.second.mid(9);
        if (hex.match(hash).hasMatch())
            return hash.toLower();
        if (base32.match(hash.toUpper()).hasMatch()) {
            QByteArray bytes;
            quint32 buffer = 0;
            int bits = 0;
            for (const QChar c : hash.toUpper()) {
                buffer = (buffer << 5) | QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").indexOf(c);
                bits += 5;
                if (bits >= 8) {
                    bits -= 8;
                    bytes.append(char((buffer >> bits) & 255));
                }
            }
            return QString::fromLatin1(bytes.toHex());
        }
    }
    return {};
}

QString TorBoxCacheService::status(const QString& hash) const
{
    if (!enabled() || hash.isEmpty())
        return {};
    const auto entry = m_cache.value(hash.toLower());
    if (entry.expiresAt <= QDateTime::currentMSecsSinceEpoch())
        return QStringLiteral("unknown");
    return entry.status;
}

void TorBoxCacheService::reset()
{
    m_timer->stop();
    m_pending.clear();
    m_cache.clear();
    m_retryAt = 0;
    if (m_reply) {
        auto* reply = m_reply;
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    emit changed();
}

void TorBoxCacheService::check(const QStringList& hashes)
{
    if (!enabled())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    static const QRegularExpression valid(QStringLiteral("^[a-f0-9]{40}$"));
    for (const auto& value : hashes) {
        const QString hash = value.toLower();
        if (!valid.match(hash).hasMatch() || m_cache.value(hash).expiresAt > now)
            continue;
        if (now < m_retryAt) {
            m_cache.insert(hash, {QStringLiteral("unavailable"), m_retryAt});
        } else {
            m_cache.insert(hash, {QStringLiteral("checking"), now + 60000});
            m_pending.insert(hash);
        }
    }
    emit changed();
    if (!m_reply && !m_pending.isEmpty())
        m_timer->start();
}

void TorBoxCacheService::sendBatch()
{
    if (!enabled() || m_reply || m_pending.isEmpty())
        return;
    QStringList hashes;
    while (!m_pending.isEmpty() && hashes.size() < 100) {
        const auto it = m_pending.begin();
        hashes.append(*it);
        m_pending.erase(it);
    }
    QUrl url(m_baseUrl.toString() + QStringLiteral("/torrents/checkcached"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), hashes.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("object"));
    query.addQueryItem(QStringLiteral("list_files"), QStringLiteral("false"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_settings->torboxApiKey().toUtf8());
    request.setTransferTimeout(15000);
    auto* reply = m_reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hashes]() {
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const auto data = object.value(QStringLiteral("data"));
        const bool success = reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && object.value(QStringLiteral("success")).toBool() && data.isObject();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonObject cached;
        const auto response = data.toObject();
        for (auto it = response.begin(); it != response.end(); ++it)
            cached.insert(it.key().toLower(), it.value());
        if (!success) {
            const int retrySeconds = qBound(60, reply->rawHeader("Retry-After").toInt(), 3600);
            m_retryAt = now + retrySeconds * 1000;
        }
        for (const auto& hash : hashes) {
            const auto value = cached.value(hash);
            const bool hit = value.isObject() && !value.toObject().isEmpty();
            m_cache.insert(hash, {success ? (hit ? QStringLiteral("cached") : QStringLiteral("uncached"))
                                          : QStringLiteral("unavailable"),
                success ? now + (hit ? 15 : 5) * 60 * 1000 : m_retryAt});
        }
        if (!success) {
            for (const auto& hash : std::as_const(m_pending))
                m_cache.insert(hash, {QStringLiteral("unavailable"), m_retryAt});
            m_pending.clear();
        }
        m_reply = nullptr;
        reply->deleteLater();
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it->expiresAt <= now)
                it = m_cache.erase(it);
            else
                ++it;
        }
        emit changed();
        if (!m_pending.isEmpty())
            m_timer->start(1000);
    });
}
} // namespace arachnel::core
