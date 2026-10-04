#include "catalog_identity.h"
#include "catalog_search_utils.h"

#include <QRegularExpression>
#include <QSet>

namespace arachnel::core {

QString catalogTitleKey(const QString& title)
{
    QString clean = title.trimmed();
    static const QRegularExpression metadataBlock(
        QStringLiteral(R"(\s*[\[({](?:[PL]|repack|(?:fitgirl|dodi)\s+repack|multi\d*|rus|eng|russian|english|v\s*\d|build\s*\d)\b[^\])}]*[\])}]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression releaseSuffix(
        QStringLiteral(R"(\s+(?:[-\x{2013}\x{2014}]\s*)?(?:v(?:ersion)?\s*\.?\s*\d[\d.]*(?:\b|/)|build\s*[-:]?\s*\d+\b).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression downloadSuffix(
        QStringLiteral(R"(\s+free\s+download\s*$)"), QRegularExpression::CaseInsensitiveOption);
    QString previous;
    do {
        previous = clean;
        clean.remove(metadataBlock);
        clean.remove(releaseSuffix);
        clean.remove(downloadSuffix);
        clean = clean.trimmed();
    } while (clean != previous);
    return compactSearchText(clean);
}

QHash<QString, QString> catalogSteamTitleKeys(const QVector<CatalogEntry>& entries)
{
    QHash<QString, QString> keys;
    QSet<QString> ambiguous;
    for (const auto& entry : entries) {
        if (entry.steamAppId.trimmed().isEmpty())
            continue;
        const QString title = catalogTitleKey(entry.title);
        if (title.isEmpty() || ambiguous.contains(title))
            continue;
        const QString key = QStringLiteral("steam:") + entry.steamAppId.trimmed();
        if (keys.contains(title) && keys.value(title) != key) {
            keys.remove(title);
            ambiguous.insert(title);
        } else {
            keys.insert(title, key);
        }
    }
    return keys;
}

QString catalogOfferGroupKey(const CatalogEntry& entry, const QHash<QString, QString>& steamTitles)
{
    if (!entry.steamAppId.trimmed().isEmpty())
        return QStringLiteral("steam:") + entry.steamAppId.trimmed();
    const QString title = catalogTitleKey(entry.title);
    if (title.isEmpty())
        return QStringLiteral("id:") + entry.id;
    const QString steamKey = steamTitles.value(title);
    return steamKey.isEmpty() ? QStringLiteral("title:") + title : steamKey;
}

} // namespace arachnel::core
