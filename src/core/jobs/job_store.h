#pragma once

#include "job_model.h"

#include <QObject>
#include <QTimer>
#include <QVector>

namespace arachnel::core {

class JobStore : public QObject
{
    Q_OBJECT

public:
    explicit JobStore(QObject* parent = nullptr);

    /** Stable view of store entries. Do not keep pointers across upsert/remove/setJobs. */
    const QVector<JobEntry>& jobs() const { return m_jobs; }
    void setJobs(QVector<JobEntry> jobs);

    const JobEntry* jobById(const QString& id) const;
    void upsertJob(const JobEntry& job);
    void removeJob(const QString& id);

    QVector<JobEntry> incompleteJobs() const;

    void load();
    void save();

signals:
    void saveFailed(const QString& error);
    void jobsChanged();

private:
    QTimer m_retry;
    bool m_saveFailed = false;
    QVector<JobEntry> m_jobs;
};

} // namespace arachnel::core
