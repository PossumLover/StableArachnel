#pragma once

#include "catalog_model.h"
#include "catalog_types.h"

#include <QObject>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <array>
#include <atomic>
#include <cstdint>

#include "catalog_search_utils.h"

namespace arachnel::core {

class ContentRatingStore;

/** Catalog filter + presentation state (type/size/recency/genre/play-mode). */
class CatalogFilterService : public QObject
{
    Q_OBJECT
public:
    explicit CatalogFilterService(CatalogModel* model, QObject* parent = nullptr);

    void setCache(QVector<CatalogEntry>* cache) { m_cache = cache; }
    /** Shared with CatalogController: write on merge/clear, read while snapshotting for filter. */
    void setCacheLock(QReadWriteLock* lock) { m_cacheLock = lock; }
    void setActiveQuery(const QString& query) { m_activeQuery = query; }
    QString activeQuery() const { return m_activeQuery; }

    int typeFilter() const { return m_typeFilter; }
    void setTypeFilter(int filter);
    int sizeFilter() const { return m_sizeFilter; }
    void setSizeFilter(int filter);
    int recencyFilter() const { return m_recencyFilter; }
    void setRecencyFilter(int filter);
    bool hasAddonsFilter() const { return m_hasAddonsFilter; }
    void setHasAddonsFilter(bool enabled);
    QString genreFilter() const { return m_genreFilter; }
    void setGenreFilter(const QString& genre);
    int playModeFilter() const { return m_playModeFilter; }
    void setPlayModeFilter(int filter);
    QStringList hiddenSourceIds() const { return m_hiddenSourceIds; }
    void setHiddenSourceIds(QStringList ids);
    void setContentRatings(const ContentRatingStore* ratings) { m_ratings = ratings; }
    bool hideAdult() const { return m_hideAdult; }
    void setHideAdult(bool hide);
    void setSourceHidden(const QString& sourceId, bool hidden);

    int activeFilterCount() const;
    QStringList availableGenres() const { return m_availableGenres; }
    QVariantMap genreCounts() const { return m_genreCounts; } // Empty key: all games.

    void clearFilters();
    void setFilters(int typeFilter, int sizeFilter, int recencyFilter, bool hasAddonsFilter,
                    const QString& genreFilter, int playModeFilter = 0);
    void applyPresentation(int sortMode, int typeFilter, int sizeFilter, int recencyFilter,
                           bool hasAddonsFilter, const QString& genreFilter,
                           int playModeFilter = 0);

    void applyFilter(const QString& query);
    /** Rebuild SoA from the merged cache (sync; prefer applyFilter). */
    void rebuildFilterTable();
    /**
     * The cache was replaced (new merge result). The filter table is only rebuilt when its size
     * differs from the cache, so a replacement with the same row count but another order or
     * content - e.g. the background revalidation of an unchanged-size feed - made searches
     * score the old titles and then show whatever sits at those indices in the new cache.
     */
    void invalidateTable();
    /** Invalidate navigation counts; nonempty replacements are counted by applyFilter. */
    void invalidateGenreCounts();
    /** Patch one SoA row after metadata enrich (no 100k rescan). */
    void syncFilterRow(int cacheIndex);
    void scheduleRefilter();

signals:
    void filtersChanged();
    void availableGenresChanged();

private:
    struct FilterSnapshot {
        ParsedSearchQuery query;
        qint64 cutoffDay = 0;
        int typeFilter = -1;
        int sizeFilter = 0;
        int recencyFilter = 0;
        bool hasAddonsFilter = false;
        quint32 genreBit = 0;
        int playModeFilter = 0;
        quint32 sourceMask = 0;
        bool checkSource = false;
        int sortMode = 0;
        bool hideAdult = false;
    };

    static int normalizePlayModeFilter(int filter);
    quint8 internSourceSlot(const QString& sourceId);
    void publishGenreCounts(const std::array<int, 32>& counts, int total);
    static bool rowMatches(const CatalogFilterRow& row, const FilterSnapshot& snap);
    void notifyFiltersChanged();
    void applyFilterResult(quint64 generation, QVector<int> indices, qint64 elapsedMs, int cacheSize,
                           const QString& needle);

    CatalogModel* m_model = nullptr;
    QVector<CatalogEntry>* m_cache = nullptr;
    QReadWriteLock* m_cacheLock = nullptr;
    quint64 m_tableEpoch = 0; ///< bumped when the cache is replaced
    quint64 m_builtEpoch = 0; ///< epoch the filter table was built for
    QVector<CatalogFilterRow> m_rows;
    QVector<CatalogSearchEntry> m_searchEntries;
    QStringList m_sourceIdsBySlot;
    QVariantMap m_genreCounts;
    std::array<int, 32> m_bitCounts{};
    int m_gameTotal = 0;
    quint64 m_facetRevision = 1;
    quint64 m_publishedRevision = 0;
    QString m_activeQuery;
    qint64 m_filterCutoffDay = 0;
    QStringList m_availableGenres;
    QTimer* m_refilterTimer = nullptr;
    std::atomic<quint64> m_filterGeneration{0};
    int m_typeFilter = -1;
    int m_sizeFilter = 0;
    int m_recencyFilter = 0;
    bool m_hasAddonsFilter = false;
    QString m_genreFilter;
    int m_playModeFilter = 0;
    QStringList m_hiddenSourceIds;
    const ContentRatingStore* m_ratings = nullptr;
    bool m_hideAdult = true;

    CatalogFilterRow rowForEntry(const CatalogEntry& entry, quint8 sourceSlot) const;
    QSet<quint32> adultSnapshot() const;
};

} // namespace arachnel::core
