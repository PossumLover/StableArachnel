#include "catalog_identity.h"
#include "catalog_search_utils.h"

#include <QRegularExpression>
#include <QSet>

namespace arachnel::core {

namespace {
QString cleanCatalogTitle(const QString& title)
{
    QString clean = title.trimmed();
    static const QRegularExpression metadataBlock(
        QStringLiteral(R"(\s*[\[({](?:[PL]|repack|(?:fitgirl|dodi)\s+repack|multi\d*|rus|eng|russian|english|v\s*\d+|build\s*[-:]?\s*\d+|gog|p2p|portable|online[- ]?fix)\b[^\])}]*[\])}]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression releaseSuffix(
        QStringLiteral(R"(\s+(?:[-\x{2013}\x{2014}]\s*)?(?:v(?:ersion)?\s*\.?\s*\d[\d.]*(?:\b|/)|build\s*[-:]?\s*\d+\b).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression downloadSuffix(
        QStringLiteral(R"(\s+free\s+download\s*$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression repackSuffix(
        QStringLiteral(R"(\s+(?:PC\s*\|\s*(?:repack|репак)\b|(?:repack|репак)\s+(?:от|by)\b).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numericMetadata(
        QStringLiteral(R"(\s*[\[({](?:(?:19|20)\d{2}(?:\s*,[^\])}]*)?|\d+(?:\.\d+)+(?:[-\w]*)?)[\])}]\s*$)"));
    const bool releaseContext = metadataBlock.match(clean).hasMatch()
        || releaseSuffix.match(clean).hasMatch() || downloadSuffix.match(clean).hasMatch()
        || repackSuffix.match(clean).hasMatch();
    QString previous;
    do {
        previous = clean;
        clean.remove(metadataBlock);
        clean.remove(repackSuffix);
        clean.remove(releaseSuffix);
        clean.remove(downloadSuffix);
        if (releaseContext)
            clean.remove(numericMetadata);
        clean = clean.trimmed();
    } while (clean != previous);
    return clean;
}

QStringList releaseTitleAliases(const QString& title)
{
    static const QRegularExpression downloadLabel(
        QStringLiteral(R"(\bfree\s+download\b)"), QRegularExpression::CaseInsensitiveOption);
    if (!downloadLabel.match(title).hasMatch())
        return {};
    const QString clean = cleanCatalogTitle(title);
    static const QRegularExpression releaseCode(
        QStringLiteral(R"(\s+(?:\.?[a-z]\d{1,6}|\d+(?:\.\d+)+)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression packageEdition(
        QStringLiteral(R"(\s+(?:digital\s+deluxe|deluxe|ultimate|gold|complete|collector'?s?|standard|premium)\s+edition\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression shortEvent(
        QStringLiteral(R"(\s+\S+\s+event\s*$)"), QRegularExpression::CaseInsensitiveOption);
    QStringList aliases;
    for (const auto* suffix : {&releaseCode, &packageEdition, &shortEvent}) {
        QString candidate = clean;
        candidate.remove(*suffix);
        if (candidate != clean && !candidate.trimmed().isEmpty())
            aliases.append(compactSearchText(candidate));
    }
    return aliases;
}
} // namespace

QString catalogTitleKey(const QString& title)
{
    return compactSearchText(cleanCatalogTitle(title));
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
            keys.insert(title, {});
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
    if (steamTitles.contains(title)) {
        const QString steamKey = steamTitles.value(title);
        return steamKey.isEmpty() ? QStringLiteral("title:") + title : steamKey;
    }
    if (entry.itemKind == CatalogItemKind::Game) {
        for (const auto& alias : releaseTitleAliases(entry.title)) {
            if (steamTitles.contains(alias)) {
                const QString steamKey = steamTitles.value(alias);
                if (!steamKey.isEmpty())
                    return steamKey;
                break;
            }
        }
    }
    return QStringLiteral("title:") + title;
}

} // namespace arachnel::core
