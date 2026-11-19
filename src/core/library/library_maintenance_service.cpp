#include "library_maintenance_service.h"

#include "catalog_types.h"
#include "job_store.h"
#include "library_store.h"

#include <QFileInfo>
#include <QSet>

namespace arachnel::core {

LibraryMaintenanceService::LibraryMaintenanceService(LibraryStore* library, JobStore* jobs,
                                                     Hooks hooks)
    : m_library(library), m_jobs(jobs), m_hooks(std::move(hooks))
{}

void LibraryMaintenanceService::migratePollutedEntryIds()
{
    bool libraryDirty = false;
    QSet<QString> seen;
    QVector<LibraryGame> games;
    for (LibraryGame game : m_library->games()) {
        const QString original = game.id;
        game.id = repairCatalogEntryId(game.id);
        game.downloadPath.remove(QStringLiteral("count:"));
        game.installPath.remove(QStringLiteral("count:"));
        libraryDirty |= game.id != original;
        if (!seen.contains(game.id)) {
            seen.insert(game.id);
            games.append(game);
        }
    }
    if (libraryDirty) {
        m_library->setGames(games);
        m_library->save();
    }

    bool jobsDirty = false;
    QVector<JobEntry> jobs = m_jobs->jobs();
    for (JobEntry& job : jobs) {
        const QString before = job.entryId + job.parentEntryId + job.savePath;
        job.entryId = repairCatalogEntryId(job.entryId);
        job.parentEntryId = repairCatalogEntryId(job.parentEntryId);
        job.savePath.remove(QStringLiteral("count:"));
        jobsDirty |= before != job.entryId + job.parentEntryId + job.savePath;
    }
    if (jobsDirty) {
        m_jobs->setJobs(jobs);
        m_jobs->save();
    }
}

void LibraryMaintenanceService::pruneCancelledAddonJobs()
{
    bool changed = false;
    QVector<JobEntry> jobs;
    for (const JobEntry& job : m_jobs->jobs()) {
        if (!job.parentEntryId.isEmpty() && job.status == QStringLiteral("cancelled")) {
            changed = true;
            continue;
        }
        jobs.append(job);
    }
    if (changed) {
        m_jobs->setJobs(jobs);
        m_jobs->save();
    }
}

void LibraryMaintenanceService::runStartupMaintenance()
{
    // Missing paths can belong to offline drives. isEntryPlayable handles
    // availability; only an explicit removal should forget the saved game.
    pruneCancelledAddonJobs();
    if (m_hooks.reconcileInstallState)
        m_hooks.reconcileInstallState();
    if (m_hooks.restorePlaceholders)
        m_hooks.restorePlaceholders();
    if (m_hooks.retryPendingInstalls)
        m_hooks.retryPendingInstalls();
}

} // namespace arachnel::core
