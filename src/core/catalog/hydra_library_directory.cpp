#include "hydra_library_directory.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace arachnel::core {

namespace {

constexpr auto kSourcesApi = "https://api.hydralibrary.com/sources";
constexpr int kPageSize = 100;
// The list is a few dozen entries; this only stops a misbehaving API from paging forever.
constexpr int kMaxPages = 20;

QVariantMap toSourceMap(const QJsonObject& source)
{
    QString url = source.value(QStringLiteral("url")).toString().trimmed();
    if (url.isEmpty())
        url = source.value(QStringLiteral("link")).toString().trimmed();

    QStringList labels;
    for (const QJsonValue& label : source.value(QStringLiteral("status")).toArray()) {
        const QString text = label.toString().trimmed();
        if (!text.isEmpty())
            labels.append(text);
    }

    QJsonObject stats = source.value(QStringLiteral("stats")).toObject();
    if (stats.isEmpty())
        stats = source.value(QStringLiteral("statistics")).toObject();
    const QJsonObject rating = source.value(QStringLiteral("rating")).toObject();

    return {
        {QStringLiteral("title"), source.value(QStringLiteral("title")).toString().trimmed()},
        {QStringLiteral("description"),
         source.value(QStringLiteral("description")).toString().trimmed()},
        {QStringLiteral("url"), url},
        {QStringLiteral("gamesCount"), source.value(QStringLiteral("gamesCount")).toInt()},
        {QStringLiteral("labels"), labels},
        {QStringLiteral("installs"), stats.value(QStringLiteral("installs")).toInteger()},
        {QStringLiteral("rating"), rating.value(QStringLiteral("avg")).toDouble()},
        {QStringLiteral("ratingCount"), rating.value(QStringLiteral("total")).toInt()},
    };
}

} // namespace

HydraLibraryDirectory::HydraLibraryDirectory(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void HydraLibraryDirectory::fetch()
{
    if (m_reply) {
        QNetworkReply* old = m_reply;
        m_reply.clear();
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    m_sources.clear();
    fetchPage(1);
}

void HydraLibraryDirectory::fetchPage(int page)
{
    QUrl url(QString::fromLatin1(kSourcesApi));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kPageSize));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Sprout/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20000);

    QNetworkReply* reply = m_network->get(request);
    reply->setProperty("page", page);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handlePage(reply); });
}

void HydraLibraryDirectory::handlePage(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply.clear();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(reply->errorString());
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("sources")).isArray()) {
        emit failed(QCoreApplication::translate("Core",
                                                "Hydra Library sent a list Sprout can't read."));
        return;
    }

    const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue& value : sources) {
        const QVariantMap source = toSourceMap(value.toObject());
        if (!source.value(QStringLiteral("url")).toString().isEmpty()
            && !source.value(QStringLiteral("title")).toString().isEmpty())
            m_sources.append(source);
    }

    const int page = reply->property("page").toInt();
    const int totalPages = root.value(QStringLiteral("totalPages")).toInt(1);
    if (!sources.isEmpty() && page < totalPages && page < kMaxPages) {
        fetchPage(page + 1);
        return;
    }
    emit loaded(m_sources);
}

} // namespace arachnel::core
