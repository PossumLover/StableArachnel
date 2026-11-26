#include "catalog_merge.h"

#include "catalog_identity.h"

#include <QDate>
#include <QVarLengthArray>

#include <deque>

namespace arachnel::core {

int catalogShowcaseScore(const CatalogEntry& entry)
{
    int score = 0;
    if (!entry.coverUrl.isEmpty() || !entry.remoteCoverUrl.isEmpty())
        score += 100;
    if (!entry.steamAppId.isEmpty())
        score += 50;
    if (!entry.description.isEmpty())
        score += 20;
    if (entry.sourceId.contains(QStringLiteral("steam"), Qt::CaseInsensitive))
        score += 10;
    if (entry.releaseDay > 0)
        score += qMin(30, int(entry.releaseDay / 30));
    if (entry.sizeBytes > 0)
        score += 5;
    return score;
}

CatalogMergeResult mergeCatalogSources(const QHash<QString, QVector<CatalogEntry>>& bySource,
                                       const QStringList& activeSourceIds,
                                       const QSet<QString>& enabledIds)
{
    CatalogMergeResult out;

    // Rows are borrowed from bySource and copied once, into the merged row (and into
    // installOffers for the few multi-source groups). This used to copy every row into a
    // per-group QVector first: a second full catalog plus one small vector per game, all
    // freed again a moment later, which the Windows heap then kept committed.
    QVector<const CatalogEntry*> rows;
    for (const QString& sourceId : activeSourceIds) {
        if (!enabledIds.contains(sourceId))
            continue;
        const auto it = bySource.constFind(sourceId);
        if (it == bySource.cend())
            continue;
        rows.reserve(rows.size() + it.value().size());
        for (const CatalogEntry& entry : it.value())
            rows.append(&entry);
    }

    // Ids are repaired when rows are prepared; keep a repaired copy only for a row
    // that still needs it (std::deque: pointers into it stay valid while it grows).
    std::deque<CatalogEntry> repairedRows;
    for (const CatalogEntry*& row : rows) {
        QString repaired = repairCatalogEntryId(row->id);
        if (repaired == row->id)
            continue;
        repairedRows.push_back(*row);
        repairedRows.back().id = std::move(repaired);
        row = &repairedRows.back();
    }

    // Steam rows key by app id; FreeTP often has no steamAppId yet and keys by title.
    // Map title -> steam:* so "The Forest" from both sources becomes one card.
    const auto titleToSteamKey = catalogSteamTitleKeys(rows);

    struct Group {
        QString key;
        // Offers in first-seen order, at most one per source (the newest upload).
        QVarLengthArray<const CatalogEntry*, 2> offers;
    };
    QVector<Group> groups;
    QHash<QString, qsizetype> groupIndex;
    groupIndex.reserve(rows.size());
    for (const CatalogEntry* row : std::as_const(rows)) {
        QString key = catalogOfferGroupKey(*row, titleToSteamKey);
        const auto found = groupIndex.constFind(key);
        if (found == groupIndex.cend()) {
            groupIndex.insert(key, groups.size());
            Group group;
            group.key = std::move(key);
            group.offers.append(row);
            groups.append(std::move(group));
            continue;
        }
        Group& group = groups[found.value()];
        bool sameSource = false;
        for (const CatalogEntry*& offer : group.offers) {
            if (offer->sourceId != row->sourceId)
                continue;
            sameSource = true;
            const auto uploadDay = [](const CatalogEntry& e) -> qint64 {
                if (e.uploadDay > 0 || e.uploadDate.isEmpty())
                    return e.uploadDay;
                const QDate day = QDate::fromString(e.uploadDate.left(10), Qt::ISODate);
                return day.isValid() ? day.toJulianDay() : 0;
            };
            if (uploadDay(*row) > uploadDay(*offer))
                offer = row;
            break;
        }
        if (!sameSource)
            group.offers.append(row);
    }
    groupIndex = {};

    out.merged.reserve(groups.size());
    out.entryIdToOfferGroup.reserve(rows.size());
    for (const Group& group : std::as_const(groups)) {
        const auto& offers = group.offers;
        qsizetype bestIdx = 0;
        int bestScore = catalogShowcaseScore(*offers.first());
        for (qsizetype i = 1; i < offers.size(); ++i) {
            const int score = catalogShowcaseScore(*offers.at(i));
            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
            }
        }
        CatalogEntry showcase = *offers.at(bestIdx);
        // Keep showcase.id from the winning offer. Never swap in another offer's id -
        // that produced Frankenstein cards (Car Mechanic art + Undying Flower entryId).
        // Only fill missing fields from peers with the same normalized title.
        if (offers.size() > 1) {
            const QString showcaseTitleKey = catalogTitleKey(showcase.title);
            for (const CatalogEntry* offer : offers) {
                if (catalogTitleKey(offer->title) != showcaseTitleKey)
                    continue;
                if (showcase.remoteCoverUrl.isEmpty() && !offer->remoteCoverUrl.isEmpty())
                    showcase.remoteCoverUrl = offer->remoteCoverUrl;
                if (showcase.coverUrl.isEmpty() && !offer->coverUrl.isEmpty())
                    showcase.coverUrl = offer->coverUrl;
                if (showcase.steamAppId.isEmpty() && !offer->steamAppId.isEmpty())
                    showcase.steamAppId = offer->steamAppId;
            }
        }

        out.entryIdToOfferGroup.insert(showcase.id, group.key);
        for (const CatalogEntry* offer : offers)
            out.entryIdToOfferGroup.insert(offer->id, group.key);

        // Multi-source only - single-offer groups use the showcase row.
        if (offers.size() > 1) {
            QVector<CatalogEntry> copies;
            copies.reserve(offers.size());
            for (const CatalogEntry* offer : offers)
                copies.append(*offer);
            out.installOffers.insert(group.key, std::move(copies));
        }

        out.merged.append(std::move(showcase));
    }
    out.merged.squeeze();
    return out;
}

} // namespace arachnel::core
