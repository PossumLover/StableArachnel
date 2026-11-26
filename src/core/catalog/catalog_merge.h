#pragma once

#include "catalog_types.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace arachnel::core {

struct CatalogMergeResult {
    /** One showcase row per offer group, in first-seen order. */
    QVector<CatalogEntry> merged;
    /** Groups offered by more than one source: every source's newest offer. */
    QHash<QString, QVector<CatalogEntry>> installOffers;
    QHash<QString, QString> entryIdToOfferGroup;
};

/** Higher is a better card to show for a group (cover, Steam id, description, ...). */
int catalogShowcaseScore(const CatalogEntry& entry);

/**
 * Groups the active sources' rows into one card per game (Steam app id, else title).
 * Runs off the UI thread; rows are borrowed from `bySource` and each is copied only into its
 * result row.
 */
CatalogMergeResult mergeCatalogSources(const QHash<QString, QVector<CatalogEntry>>& bySource,
                                       const QStringList& activeSourceIds,
                                       const QSet<QString>& enabledIds);

} // namespace arachnel::core
