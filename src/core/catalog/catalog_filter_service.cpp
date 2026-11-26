#include "catalog_filter_service.h"

#include "content_rating_store.h"

#include "catalog_genre_normalize.h"
#include "crash_log.h"
#include "install_kind.h"

#include <QDate>
#include <QElapsedTimer>
#include <QHash>
#include <QReadLocker>
#include <QThreadPool>
#include <QTimer>
#include <QWriteLocker>
#include <QtConcurrent>
#include <QThread>

#include <algorithm>
#include <limits>

namespace arachnel::core {

CatalogFilterService::CatalogFilterService(CatalogModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    m_refilterTimer = new QTimer(this);
    m_refilterTimer->setSingleShot(true);
    m_refilterTimer->setInterval(120);
    connect(m_refilterTimer, &QTimer::timeout, this, [this]() { applyFilter(m_activeQuery); });
}

int CatalogFilterService::normalizePlayModeFilter(int filter)
{
    if (filter <= 0)
        return 0;
    if (filter == 1)
        return 1;
    return 2;
}

quint8 CatalogFilterService::internSourceSlot(const QString& sourceId)
{
    for (int i = 0; i < m_sourceIdsBySlot.size(); ++i) {
        if (m_sourceIdsBySlot.at(i) == sourceId)
            return static_cast<quint8>(i);
    }
    if (m_sourceIdsBySlot.size() >= 32)
        return 31;
    m_sourceIdsBySlot.append(sourceId);
    return static_cast<quint8>(m_sourceIdsBySlot.size() - 1);
}

void CatalogFilterService::publishGenreCounts(const std::array<int, 32>& counts, int total)
{
    m_bitCounts = counts;
    m_gameTotal = total;
    QVariantMap values{{QString(), total}};
    QStringList genres;
    for (const QString& key : browseGenreKeys()) {
        const int n = counts[curatedGenreBitIndex(key)];
        if (n > 0) {
            values.insert(key, n);
            genres.append(key);
        }
    }
    if (values == m_genreCounts)
        return;
    std::stable_sort(genres.begin(), genres.end(), [&](const QString& a, const QString& b) {
        return counts[curatedGenreBitIndex(a)] > counts[curatedGenreBitIndex(b)];
    });
    m_genreCounts = std::move(values);
    m_availableGenres = std::move(genres);
    emit availableGenresChanged();
}

bool CatalogFilterService::rowMatches(const CatalogFilterRow& row, const FilterSnapshot& snap)
{
    if (snap.typeFilter >= 0) {
        const int kind = static_cast<int>(row.installKind);
        if (snap.typeFilter == 2) {
            if (kind != static_cast<int>(InstallKind::BundledFix)
                && kind != static_cast<int>(InstallKind::FixDownload))
                return false;
        } else if (kind != snap.typeFilter) {
            return false;
        }
    }

    if (snap.sizeFilter > 0 && row.sizeBytes > 0) {
        constexpr qint64 kGb = 1024LL * 1024 * 1024;
        switch (snap.sizeFilter) {
        case 1:
            if (row.sizeBytes >= kGb)
                return false;
            break;
        case 2:
            if (row.sizeBytes < kGb || row.sizeBytes >= 5 * kGb)
                return false;
            break;
        case 3:
            if (row.sizeBytes < 5 * kGb || row.sizeBytes >= 20 * kGb)
                return false;
            break;
        case 4:
            if (row.sizeBytes < 20 * kGb)
                return false;
            break;
        default:
            break;
        }
    }

    if (snap.recencyFilter > 0) {
        if (row.uploadDay <= 0 || row.uploadDay < snap.cutoffDay)
            return false;
    }

    if (snap.hasAddonsFilter && (row.flags & kFilterFlagHasAddons) == 0)
        return false;

    if (snap.genreBit && (row.genreBits & snap.genreBit) == 0)
        return false;

    if (snap.playModeFilter == 1) {
        if ((row.playModeMask & kPlayModeSingle) == 0 && row.playModeMask != 0)
            return false;
    } else if (snap.playModeFilter == 2) {
        if ((row.playModeMask & kPlayModeTogether) == 0)
            return false;
    }

    return true;
}

CatalogFilterRow CatalogFilterService::rowForEntry(const CatalogEntry& entry, quint8 sourceSlot) const
{
    CatalogFilterRow row = catalogFilterRowFromEntry(entry, sourceSlot);
    if (m_ratings && m_ratings->isAdult(entry.steamAppId))
        row.flags |= kFilterFlagAdult;
    return row;
}

QSet<quint32> CatalogFilterService::adultSnapshot() const
{
    return m_ratings ? m_ratings->adultAppIds() : QSet<quint32>();
}

void CatalogFilterService::setHideAdult(bool hide)
{
    if (m_hideAdult == hide)
        return;
    m_hideAdult = hide;
    ++m_facetRevision;
    scheduleRefilter();
}

namespace {

bool isAdultAppId(const QSet<quint32>& adult, const QString& steamAppId)
{
    if (steamAppId.isEmpty())
        return false;
    bool ok = false;
    const quint32 appId = steamAppId.toUInt(&ok);
    return ok && adult.contains(appId);
}

// No row cap: the catalog GridView is virtualized, so only the delegates on screen exist.
// The old caps (8192 browse / 4096 search) hid ~94% of a 125k catalog behind "Found: 8192".
// Huge replacements still go through CatalogModel's bulk path (view unbound while swapping).
constexpr int kMaxVisibleBrowse = std::numeric_limits<int>::max();
constexpr int kMaxVisibleSearch = std::numeric_limits<int>::max();

struct FilterSoA {
    QVector<CatalogFilterRow> rows;
    QVector<CatalogSearchEntry> searchEntries;
    QStringList sourceIdsBySlot;
};

// `adult` is a copy of ContentRatingStore's set taken on the GUI thread: the store mutates it
// there while this runs on a worker.
FilterSoA buildFilterSoA(const QVector<CatalogEntry>* cachePtr, const QSet<quint32>& adult)
{
    FilterSoA out;
    if (!cachePtr)
        return out;
    const int n = cachePtr->size();
    // reserve() first: resize() alone over-allocates geometrically (see CatalogSnapshot::load).
    out.rows.reserve(n);
    out.rows.resize(n);
    out.searchEntries.reserve(n);
    out.searchEntries.resize(n);
    out.sourceIdsBySlot.reserve(8);

    // Cheap sequential pass: source slots depend on first-seen order.
    QVector<quint8> sourceSlotByRow(n);
    QHash<QString, quint8> intern;
    intern.reserve(8);
    for (int i = 0; i < n; ++i) {
        const CatalogEntry& entry = cachePtr->at(i);
        quint8 slot = 0;
        const auto it = intern.constFind(entry.sourceId);
        if (it != intern.cend()) {
            slot = it.value();
        } else if (out.sourceIdsBySlot.size() < 32) {
            slot = static_cast<quint8>(out.sourceIdsBySlot.size());
            intern.insert(entry.sourceId, slot);
            out.sourceIdsBySlot.append(entry.sourceId);
        } else {
            slot = 31;
        }
        sourceSlotByRow[i] = slot;
    }

    // Row + search-entry construction (tokenizing every title) is independent per entry.
    const CatalogEntry* entries = cachePtr->constData();
    CatalogFilterRow* rows = out.rows.data();
    CatalogSearchEntry* searchEntries = out.searchEntries.data();
    const auto buildRange = [&](int from, int to) {
        for (int i = from; i < to; ++i) {
            rows[i] = catalogFilterRowFromEntry(entries[i], sourceSlotByRow.at(i));
            if (!adult.isEmpty() && isAdultAppId(adult, entries[i].steamAppId))
                rows[i].flags |= kFilterFlagAdult;
            searchEntries[i] = CatalogSearchEntry::fromEntry(entries[i]);
        }
    };
    constexpr int kMinParallelRows = 8192;
    const int threads = qBound(1, QThread::idealThreadCount(), 8);
    if (n < kMinParallelRows || threads == 1) {
        buildRange(0, n);
        return out;
    }
    QVector<QPair<int, int>> ranges;
    const int chunk = (n + threads - 1) / threads;
    for (int from = 0; from < n; from += chunk)
        ranges.append({from, qMin(n, from + chunk)});
    QtConcurrent::blockingMap(ranges, [&](QPair<int, int>& range) {
        buildRange(range.first, range.second);
    });
    return out;
}

} // namespace

void CatalogFilterService::rebuildFilterTable()
{
    FilterSoA built;
    if (m_cacheLock) {
        QWriteLocker locker(m_cacheLock);
        built = buildFilterSoA(m_cache, adultSnapshot());
    } else {
        built = buildFilterSoA(m_cache, adultSnapshot());
    }
    m_rows = std::move(built.rows);
    m_searchEntries = std::move(built.searchEntries);
    m_sourceIdsBySlot = std::move(built.sourceIdsBySlot);
    ++m_facetRevision;
    m_builtEpoch = m_tableEpoch;
}

void CatalogFilterService::invalidateTable()
{
    ++m_tableEpoch;
    ++m_facetRevision;
}

void CatalogFilterService::invalidateGenreCounts()
{
    ++m_facetRevision;
    if ((!m_cache || m_cache->isEmpty()) && !m_genreCounts.isEmpty()) {
        m_genreCounts.clear();
        m_availableGenres.clear();
        emit availableGenresChanged();
    }
}

void CatalogFilterService::syncFilterRow(int cacheIndex)
{
    if (!m_cache || cacheIndex < 0)
        return;
    CatalogFilterRow row;
    CatalogFilterRow previous;
    CatalogSearchEntry searchEntry;
    if (m_cacheLock) {
        QWriteLocker locker(m_cacheLock);
        if (cacheIndex >= m_cache->size())
            return;
        if (m_rows.size() != m_cache->size() || m_searchEntries.size() != m_cache->size()) {
            locker.unlock();
            // Huge catalogs: never rebuild SoA on the UI thread from enrich.
            scheduleRefilter();
            return;
        }
        const CatalogEntry& entry = m_cache->at(cacheIndex);
        const quint8 slot = internSourceSlot(entry.sourceId);
        row = rowForEntry(entry, slot);
        searchEntry = CatalogSearchEntry::fromEntry(entry);
        previous = m_rows.at(cacheIndex);
        m_rows[cacheIndex] = row;
        m_searchEntries[cacheIndex] = std::move(searchEntry);
    } else {
        if (cacheIndex >= m_cache->size())
            return;
        if (m_rows.size() != m_cache->size() || m_searchEntries.size() != m_cache->size()) {
            scheduleRefilter();
            return;
        }
        const CatalogEntry& entry = m_cache->at(cacheIndex);
        const quint8 slot = internSourceSlot(entry.sourceId);
        row = rowForEntry(entry, slot);
        searchEntry = CatalogSearchEntry::fromEntry(entry);
        previous = m_rows.at(cacheIndex);
        m_rows[cacheIndex] = row;
        m_searchEntries[cacheIndex] = std::move(searchEntry);
    }
    const auto visible = [this](const CatalogFilterRow& r) {
        return (r.flags & kFilterFlagGame) && !(m_hideAdult && (r.flags & kFilterFlagAdult))
            && !m_hiddenSourceIds.contains(m_sourceIdsBySlot.at(r.sourceSlot));
    };
    const int oldGame = visible(previous), newGame = visible(row);
    const auto oldBits = oldGame ? previous.genreBits : 0u;
    const auto newBits = newGame ? row.genreBits : 0u;
    if (oldGame == newGame && oldBits == newBits)
        return;
    const bool current = m_publishedRevision == m_facetRevision;
    ++m_facetRevision;
    if (current) {
        // Enrichment changes the facets, not the scroll position or current sort.
        auto counts = m_bitCounts;
        for (quint32 bits = oldBits ^ newBits; bits; bits &= bits - 1) {
            const auto i = qCountTrailingZeroBits(bits);
            counts[i] += (newBits & (quint32(1) << i)) ? 1 : -1;
        }
        m_publishedRevision = m_facetRevision;
        publishGenreCounts(counts, m_gameTotal + newGame - oldGame);
    } else {
        scheduleRefilter();
    }
}

void CatalogFilterService::applyFilterResult(quint64 generation, QVector<int> indices,
                                             qint64 elapsedMs, int cacheSize, const QString& needle)
{
    if (generation != m_filterGeneration.load(std::memory_order_relaxed) || !m_model)
        return;
    if (m_cache && cacheSize != m_cache->size())
        return;
    m_model->setVisibleIndicesPresorted(std::move(indices));
    if (elapsedMs >= 16 || (!needle.isEmpty() && m_model->count() == 0)) {
        logDiagnostic(QStringLiteral("applyCatalogFilter: %1ms cache=%2 visible=%3 needle=\"%4\"")
                          .arg(elapsedMs)
                          .arg(cacheSize)
                          .arg(m_model->count())
                          .arg(needle));
    }
}

namespace {
/** Matches that make a search level good enough that the next, looser level is not tried. */
constexpr int kSearchEnough[] = {8, 5, 5, 0};
/** Score of a hit that has the query's words in the title (below: acronym, typo, sound-alike). */
constexpr int kStrongScore = 50000;
/** This many such hits are enough to hide the weaker kinds. */
constexpr int kStrongEnough = 3;
/** How far below the best fuzzy hit another fuzzy hit may score and still be shown. */
constexpr int kFuzzySpread = 4500;
} // namespace

void CatalogFilterService::applyFilter(const QString& query)
{
    if (!m_model || !m_cache)
        return;

    bool needRebuild = false;
    if (m_cacheLock) {
        QReadLocker locker(m_cacheLock);
        needRebuild = m_rows.size() != m_cache->size() || m_searchEntries.size() != m_cache->size();
    } else {
        needRebuild = m_rows.size() != m_cache->size() || m_searchEntries.size() != m_cache->size();
    }
    needRebuild = needRebuild || m_builtEpoch != m_tableEpoch;
    const quint64 tableEpoch = m_tableEpoch;

    m_activeQuery = query;
    m_filterCutoffDay = 0;
    if (m_recencyFilter > 0) {
        const int days = m_recencyFilter == 1   ? 7
                         : m_recencyFilter == 2 ? 30
                         : m_recencyFilter == 3 ? 90
                                                 : 365;
        m_filterCutoffDay = QDate::currentDate().addDays(-days).toJulianDay();
    }

    const quint64 generation = ++m_filterGeneration;
    m_model->bindSource(m_cache);

    FilterSnapshot snap;
    snap.query = ParsedSearchQuery::parse(query);
    snap.cutoffDay = m_filterCutoffDay;
    snap.typeFilter = m_typeFilter;
    snap.sizeFilter = m_sizeFilter;
    snap.recencyFilter = m_recencyFilter;
    snap.hasAddonsFilter = m_hasAddonsFilter;
    snap.genreBit = curatedGenreBit(m_genreFilter);
    snap.playModeFilter = m_playModeFilter;
    snap.hideAdult = m_hideAdult;
    snap.sortMode = m_model->sortMode();
    // Source mask is resolved on the worker after SoA slots are known.
    snap.sourceMask = 0;
    snap.checkSource = !m_hiddenSourceIds.isEmpty();

    QVector<CatalogEntry>* cachePtr = m_cache;
    QReadWriteLock* lock = m_cacheLock;
    const QStringList hiddenIds = m_hiddenSourceIds;
    const QSet<quint32> adult = adultSnapshot();
    const auto revision = m_facetRevision;
    const bool countGenres = needRebuild || revision != m_publishedRevision;

    QThreadPool::globalInstance()->start(
        [this, generation, snap, cachePtr, lock, needRebuild, hiddenIds, adult, tableEpoch,
         revision, countGenres]() {
            QElapsedTimer timer;
            timer.start();

            FilterSoA rebuilt;
            bool builtFresh = false;
            const QVector<CatalogFilterRow>* rowsPtr = nullptr;
            const QVector<CatalogSearchEntry>* searchPtr = nullptr;
            QStringList sourceIds;

            auto loadSoA = [&]() -> bool {
                if (!cachePtr)
                    return false;
                if (needRebuild || m_rows.size() != cachePtr->size()
                    || m_searchEntries.size() != cachePtr->size()) {
                    rebuilt = buildFilterSoA(cachePtr, adult);
                    rowsPtr = &rebuilt.rows;
                    searchPtr = &rebuilt.searchEntries;
                    sourceIds = rebuilt.sourceIdsBySlot;
                    builtFresh = true;
                } else {
                    rowsPtr = &m_rows;
                    searchPtr = &m_searchEntries;
                    sourceIds = m_sourceIdsBySlot;
                }
                return rowsPtr && searchPtr;
            };

            if (lock) {
                QReadLocker locker(lock);
                if (!loadSoA())
                    return;
            } else {
                if (!loadSoA())
                    return;
            }

            if (generation != m_filterGeneration.load(std::memory_order_relaxed))
                return;

            FilterSnapshot localSnap = snap;
            if (localSnap.checkSource) {
                quint32 sourceMask = 0;
                for (int i = 0; i < sourceIds.size(); ++i) {
                    if (!hiddenIds.contains(sourceIds.at(i)))
                        sourceMask |= (quint32(1) << i);
                }
                localSnap.sourceMask = sourceMask;
                if (sourceMask == 0) {
                    // all hidden
                } else {
                    quint32 all = 0;
                    for (int i = 0; i < sourceIds.size(); ++i)
                        all |= (quint32(1) << i);
                    if (sourceMask == all)
                        localSnap.checkSource = false;
                }
            }

            struct ScoredIndex {
                int index = 0;
                int score = 0;
            };
            QVector<ScoredIndex> scoredMatches;
            QVector<int> indices;
            int cacheSize = 0;
            CatalogModel::SortMode mode = CatalogModel::SortNewest;
            const bool hasSearch = !localSnap.query.isEmpty;
            std::array<int, 32> counts{};
            int total = 0;
            auto inScope = [&](const CatalogFilterRow& row, bool accumulate) {
                if (!(row.flags & kFilterFlagGame)
                    || (localSnap.hideAdult && (row.flags & kFilterFlagAdult))
                    || (localSnap.checkSource && !(localSnap.sourceMask & (quint32(1) << row.sourceSlot))))
                    return false;
                if (accumulate && countGenres) {
                    ++total;
                    for (quint32 bits = row.genreBits; bits; bits &= bits - 1)
                        ++counts[qCountTrailingZeroBits(bits)];
                }
                return true;
            };

            auto scanCache = [&]() -> bool {
                if (!cachePtr || !rowsPtr || !searchPtr)
                    return false;
                cacheSize = cachePtr->size();
                if (rowsPtr->size() != cacheSize || searchPtr->size() != cacheSize)
                    return false;

                if (!hasSearch) {
                    indices.reserve(qMin(cacheSize, kMaxVisibleBrowse));
                    for (int i = 0; i < cacheSize; ++i) {
                        if ((i & 0x3FF) == 0
                            && generation != m_filterGeneration.load(std::memory_order_relaxed)) {
                            return false;
                        }
                        if (!inScope(rowsPtr->at(i), true) || !rowMatches(rowsPtr->at(i), localSnap))
                            continue;
                        indices.append(i);
                    }
                } else {
                    // Whole words first. Only when that finds too little is the query widened to an
                    // unfinished last word, then typos, then sound-alikes across scripts, so a
                    // precise query never gets noise from the looser levels.
                    scoredMatches.reserve(qMin(cacheSize, 1024));
                    const ParsedSearchQuery baseQuery = localSnap.query;
                    for (int level = 0; level < 4; ++level) {
                        scoredMatches.clear();
                        const ParsedSearchQuery levelQuery = baseQuery.withLevel(level);
                        for (int i = 0; i < cacheSize; ++i) {
                            if ((i & 0x3FF) == 0
                                && generation
                                       != m_filterGeneration.load(std::memory_order_relaxed)) {
                                return false;
                            }
                            if (!inScope(rowsPtr->at(i), level == 0) || !rowMatches(rowsPtr->at(i), localSnap))
                                continue;

                            const int score = scoreCatalogMatch(searchPtr->at(i), cachePtr->at(i),
                                                                levelQuery);
                            if (score <= 0)
                                continue;

                            scoredMatches.append({i, score});
                        }
                        if (scoredMatches.size() >= kSearchEnough[level])
                            break;
                    }

                    // Fuzzy hits are only worth showing when they are close to the best fuzzy hit:
                    // a typo in a whole word beats "sort of looks like the start of another word".
                    // With real word matches on screen, acronym / typo / sound-alike hits are noise.
                    int strong = 0;
                    for (const ScoredIndex& sm : scoredMatches)
                        strong += sm.score >= kStrongScore ? 1 : 0;
                    if (strong >= kStrongEnough) {
                        scoredMatches.erase(
                            std::remove_if(scoredMatches.begin(), scoredMatches.end(),
                                           [](const ScoredIndex& sm) {
                                               return sm.score < kStrongScore;
                                           }),
                            scoredMatches.end());
                    }

                    constexpr int kFuzzyCeiling = 40000;
                    int bestFuzzy = 0;
                    for (const ScoredIndex& sm : scoredMatches) {
                        if (sm.score < kFuzzyCeiling)
                            bestFuzzy = qMax(bestFuzzy, sm.score);
                    }
                    if (bestFuzzy > 0) {
                        scoredMatches.erase(
                            std::remove_if(scoredMatches.begin(), scoredMatches.end(),
                                           [&](const ScoredIndex& sm) {
                                               return sm.score < kFuzzyCeiling
                                                      && sm.score < bestFuzzy - kFuzzySpread;
                                           }),
                            scoredMatches.end());
                    }
                }
                mode = static_cast<CatalogModel::SortMode>(localSnap.sortMode);
                return true;
            };

            if (lock) {
                QReadLocker locker(lock);
                if (!scanCache())
                    return;
            } else {
                if (!scanCache())
                    return;
            }

            if (generation != m_filterGeneration.load(std::memory_order_relaxed))
                return;

            if (hasSearch) {
                auto sortScored = [&](const ScoredIndex& a, const ScoredIndex& b) {
                    if (a.score != b.score)
                        return a.score > b.score;
                    if (cachePtr)
                        return catalogEntryLess(cachePtr->at(a.index), cachePtr->at(b.index), mode);
                    return a.index < b.index;
                };

                if (lock && cachePtr) {
                    QReadLocker locker(lock);
                    if (cacheSize != cachePtr->size())
                        return;
                    std::stable_sort(scoredMatches.begin(), scoredMatches.end(), sortScored);
                } else {
                    std::stable_sort(scoredMatches.begin(), scoredMatches.end(), sortScored);
                }

                indices.reserve(scoredMatches.size());
                for (const ScoredIndex& sm : scoredMatches)
                    indices.append(sm.index);
            } else {
                const int cap = kMaxVisibleBrowse;
                auto cmp = [cachePtr, mode](int ai, int bi) {
                    return catalogEntryLess(cachePtr->at(ai), cachePtr->at(bi), mode);
                };
                auto sortIndices = [&]() {
                    if (!cachePtr)
                        return;
                    if (indices.size() > cap) {
                        std::partial_sort(indices.begin(), indices.begin() + cap, indices.end(), cmp);
                        indices.resize(cap);
                    } else {
                        std::stable_sort(indices.begin(), indices.end(), cmp);
                    }
                };
                if (lock && cachePtr) {
                    QReadLocker locker(lock);
                    if (cacheSize != cachePtr->size())
                        return;
                    sortIndices();
                } else {
                    sortIndices();
                }
            }

            if (generation != m_filterGeneration.load(std::memory_order_relaxed))
                return;

            // GridView + required-property delegates Abort around ~100k inserts.
            const int cap = hasSearch ? kMaxVisibleSearch : kMaxVisibleBrowse;
            if (indices.size() > cap)
                indices.resize(cap);

            const qint64 ms = timer.elapsed();
            const QString queryStr = localSnap.query.rawQuery;
            const bool commitSoA = builtFresh;
            QTimer::singleShot(
                0, this,
                [this, generation, indices = std::move(indices), ms, cacheSize, queryStr, commitSoA,
                 rebuilt = std::move(rebuilt), tableEpoch, revision, countGenres, counts, total]() mutable {
                    if (generation != m_filterGeneration.load(std::memory_order_relaxed)
                        || tableEpoch != m_tableEpoch || cacheSize != m_cache->size())
                        return;
                    if (commitSoA) {
                        m_rows = std::move(rebuilt.rows);
                        m_searchEntries = std::move(rebuilt.searchEntries);
                        m_sourceIdsBySlot = std::move(rebuilt.sourceIdsBySlot);
                        m_builtEpoch = tableEpoch;
                    }
                    if (countGenres && revision == m_facetRevision) {
                        m_publishedRevision = revision;
                        publishGenreCounts(counts, total);
                    }
                    applyFilterResult(generation, std::move(indices), ms, cacheSize, queryStr);
                });
        });
}

void CatalogFilterService::scheduleRefilter()
{
    if (m_refilterTimer)
        m_refilterTimer->start();
}

void CatalogFilterService::notifyFiltersChanged()
{
    emit filtersChanged();
    applyFilter(m_activeQuery);
}

void CatalogFilterService::setTypeFilter(int filter)
{
    const int next = (filter < -1 || filter > 2) ? -1 : filter;
    if (m_typeFilter == next)
        return;
    m_typeFilter = next;
    notifyFiltersChanged();
}

void CatalogFilterService::setSizeFilter(int filter)
{
    const int next = qBound(0, filter, 4);
    if (m_sizeFilter == next)
        return;
    m_sizeFilter = next;
    notifyFiltersChanged();
}

void CatalogFilterService::setRecencyFilter(int filter)
{
    const int next = qBound(0, filter, 4);
    if (m_recencyFilter == next)
        return;
    m_recencyFilter = next;
    notifyFiltersChanged();
}

void CatalogFilterService::setHasAddonsFilter(bool enabled)
{
    if (m_hasAddonsFilter == enabled)
        return;
    m_hasAddonsFilter = enabled;
    notifyFiltersChanged();
}

void CatalogFilterService::setGenreFilter(const QString& genre)
{
    const QString next = browseGenreKey(genre);
    if (m_genreFilter == next)
        return;
    m_genreFilter = next;
    notifyFiltersChanged();
}

void CatalogFilterService::setPlayModeFilter(int filter)
{
    const int next = normalizePlayModeFilter(filter);
    if (m_playModeFilter == next)
        return;
    m_playModeFilter = next;
    notifyFiltersChanged();
}

void CatalogFilterService::setHiddenSourceIds(QStringList ids)
{
    ids.removeAll(QString());
    ids.removeDuplicates();
    if (ids == m_hiddenSourceIds)
        return;
    m_hiddenSourceIds = std::move(ids);
    ++m_facetRevision;
    notifyFiltersChanged();
}

void CatalogFilterService::setSourceHidden(const QString& sourceId, bool hidden)
{
    const QString id = sourceId.trimmed();
    if (id.isEmpty())
        return;
    const int at = m_hiddenSourceIds.indexOf(id);
    if (hidden) {
        if (at >= 0)
            return;
        m_hiddenSourceIds.append(id);
    } else {
        if (at < 0)
            return;
        m_hiddenSourceIds.removeAt(at);
    }
    ++m_facetRevision;
    notifyFiltersChanged();
}

int CatalogFilterService::activeFilterCount() const
{
    int count = 0;
    if (m_typeFilter >= 0)
        ++count;
    if (m_sizeFilter > 0)
        ++count;
    if (m_recencyFilter > 0)
        ++count;
    if (m_hasAddonsFilter)
        ++count;
    if (!m_genreFilter.isEmpty())
        ++count;
    if (m_playModeFilter > 0)
        ++count;
    count += m_hiddenSourceIds.size();
    return count;
}

void CatalogFilterService::clearFilters()
{
    const bool hiddenChanged = !m_hiddenSourceIds.isEmpty();
    if (hiddenChanged)
        ++m_facetRevision;
    m_hiddenSourceIds.clear();
    setFilters(-1, 0, 0, false, {}, 0);
    if (hiddenChanged && m_typeFilter == -1 && m_sizeFilter == 0 && m_recencyFilter == 0
        && !m_hasAddonsFilter && m_genreFilter.isEmpty() && m_playModeFilter == 0) {
        // setFilters no-op'd because side filters were already clear.
        notifyFiltersChanged();
    }
}

void CatalogFilterService::setFilters(int typeFilter, int sizeFilter, int recencyFilter,
                                      bool hasAddonsFilter, const QString& genreFilter,
                                      int playModeFilter)
{
    const int nextType = (typeFilter < -1 || typeFilter > 2) ? -1 : typeFilter;
    const int nextSize = qBound(0, sizeFilter, 4);
    const int nextRecency = qBound(0, recencyFilter, 4);
    const QString nextGenre = browseGenreKey(genreFilter);
    const int nextPlay = normalizePlayModeFilter(playModeFilter);
    if (m_typeFilter == nextType && m_sizeFilter == nextSize && m_recencyFilter == nextRecency
        && m_hasAddonsFilter == hasAddonsFilter && m_genreFilter == nextGenre
        && m_playModeFilter == nextPlay)
        return;

    m_typeFilter = nextType;
    m_sizeFilter = nextSize;
    m_recencyFilter = nextRecency;
    m_hasAddonsFilter = hasAddonsFilter;
    m_genreFilter = nextGenre;
    m_playModeFilter = nextPlay;
    notifyFiltersChanged();
}

void CatalogFilterService::applyPresentation(int sortMode, int typeFilter, int sizeFilter,
                                             int recencyFilter, bool hasAddonsFilter,
                                             const QString& genreFilter, int playModeFilter)
{
    if (m_model)
        m_model->setSortModeQuiet(sortMode);

    const int nextType = (typeFilter < -1 || typeFilter > 2) ? -1 : typeFilter;
    const int nextSize = qBound(0, sizeFilter, 4);
    const int nextRecency = qBound(0, recencyFilter, 4);
    const QString nextGenre = browseGenreKey(genreFilter);
    const int nextPlay = normalizePlayModeFilter(playModeFilter);
    const bool changed = m_typeFilter != nextType || m_sizeFilter != nextSize
        || m_recencyFilter != nextRecency || m_hasAddonsFilter != hasAddonsFilter
        || m_genreFilter != nextGenre || m_playModeFilter != nextPlay;

    m_typeFilter = nextType;
    m_sizeFilter = nextSize;
    m_recencyFilter = nextRecency;
    m_hasAddonsFilter = hasAddonsFilter;
    m_genreFilter = nextGenre;
    m_playModeFilter = nextPlay;

    if (changed)
        emit filtersChanged();
    applyFilter(m_activeQuery);
}

} // namespace arachnel::core
