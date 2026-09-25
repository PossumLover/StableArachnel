#pragma once

#include "catalog_types.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace arachnel::core {

/**
 * Steam content descriptors per app, fetched in bulk from
 * IStoreBrowseService/GetItems and cached in content-descriptors.json.
 *
 * Only descriptors 3 ("Adult Only Sexual Content") and 4 ("Frequent Nudity or
 * Sexual Content") count as adult. 1 (some nudity), 2 (violence/gore) and
 * 5 (general mature) do not - Baldur's Gate 3 and Cyberpunk 2077 carry those.
 * Apps not fetched yet are treated as not adult.
 */
class ContentRatingStore : public QObject
{
    Q_OBJECT
public:
    explicit ContentRatingStore(QObject* parent = nullptr);

    bool isAdult(const QString& steamAppId) const;
    /** Queue every numeric steamAppId in the catalog that hasn't been checked yet. */
    void requestMissing(const QVector<CatalogEntry>& entries);
    int adultCount() const { return m_adult.size(); }

signals:
    /** New descriptors arrived; filters should be rebuilt. */
    void ratingsChanged();

private:
    void load();
    void save() const;
    void fetchNext();
    void handleReply(QNetworkReply* reply, const QVector<quint32>& batch);
    void flush();

    QHash<quint32, quint8> m_descriptorMasks; // bit n = descriptor n, only non-empty
    QSet<quint32> m_checked;
    QSet<quint32> m_adult;
    QVector<quint32> m_queue;
    QSet<quint32> m_queued;
    QNetworkAccessManager* m_network = nullptr;
    QTimer* m_pacer = nullptr;
    bool m_inFlight = false;
    int m_backoffMs = 0;
    int m_batchesSinceFlush = 0;
    bool m_dirty = false;
    bool m_adultDirty = false;
};

} // namespace arachnel::core
