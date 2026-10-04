#include "job_orchestrator.h"

#include "http_download_session.h"
#include "i18n.h"
#include "job_status.h"
#include "torrent_session.h"
#include "torbox_download_session.h"
#include "hydra_catalog_client.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>

namespace arachnel::core {

namespace {

QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

JobOrchestrator::JobOrchestrator(SettingsStore* settings, JobStore* jobStore,
                                 TorrentSession* torrent, HttpDownloadSession* http, JobModel* jobs,
                                 QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_jobStore(jobStore)
    , m_torrent(torrent)
    , m_http(http)
    , m_torbox(new TorBoxDownloadSession(settings, this))
    , m_jobs(jobs)
{
    connect(m_torrent, &TorrentSession::torrentProgress, this, &JobOrchestrator::onTorrentProgress);
    connect(m_torrent, &TorrentSession::torrentFinished, this, &JobOrchestrator::onTorrentFinished);
    connect(m_torrent, &TorrentSession::torrentFailed, this, &JobOrchestrator::onTorrentFailed);

    connect(m_http, &HttpDownloadSession::httpProgress, this, &JobOrchestrator::onHttpProgress);
    connect(m_http, &HttpDownloadSession::httpFinished, this, &JobOrchestrator::onHttpFinished);
    connect(m_http, &HttpDownloadSession::httpFailed, this, &JobOrchestrator::onHttpFailed);
    connect(m_torbox, &TorBoxDownloadSession::progress, this, &JobOrchestrator::onHttpProgress);
    connect(m_torbox, &TorBoxDownloadSession::finished, this, &JobOrchestrator::onTorrentFinished);
    connect(m_torbox, &TorBoxDownloadSession::failed, this, &JobOrchestrator::onTorrentFailed);
    connect(m_torbox, &TorBoxDownloadSession::phase, this, &JobOrchestrator::setJobPhase);
    connect(m_settings, &SettingsStore::debridChanged, this,
            &JobOrchestrator::routePendingTorrentsThroughTorbox);
    connect(m_torbox, &TorBoxDownloadSession::torrentRegistered, this, [this](const QString& id, qint64 remoteId) {
        const int row = m_jobs->indexOfJob(id);
        if (row < 0)
            return;
        JobEntry job = jobFromModelRow(row);
        job.torboxTorrentId = remoteId;
        persistJob(job);
        updateJobInModel(job);
        m_jobStore->save();
    });

    m_persistTimer.setInterval(3000);
    m_persistTimer.setSingleShot(true);
    connect(&m_persistTimer, &QTimer::timeout, this, [this]() {
        if (!m_dirty)
            return;
        m_jobStore->save();
        m_dirty = false;
    });

    m_pruneTimer.setInterval(30 * 1000);
    connect(&m_pruneTimer, &QTimer::timeout, this, &JobOrchestrator::pruneFinishedJobs);
    m_pruneTimer.start();
}

void JobOrchestrator::restoreJobs()
{
    QVector<JobEntry> jobs = m_jobStore->jobs();
    for (auto& job : jobs) {
        // Library moves are not resumable - mark interrupted work failed.
        if (job.kind == JobKind::Move) {
            if (!isJobTerminal(job.status)) {
                job.status = QStringLiteral("failed");
                job.detail = QCoreApplication::translate("Core", "Move interrupted");
                job.completedAt = isoNow();
            }
            continue;
        }
        if (job.pluginDownload) {
            // Old bug routed plugin jobs through startTorrent ("Failed to start torrent").
            const bool falseTorrentFail = job.status == QStringLiteral("failed")
                && job.detail.contains(QStringLiteral("Failed to start torrent"),
                                       Qt::CaseInsensitive);
            if (isJobTerminal(job.status) && !falseTorrentFail)
                continue;
            // Keep plugin jobs out of torrent/HTTP restore; Core resumes them after plugins load.
            job.status = QStringLiteral("starting");
            job.detail = QStringLiteral("Resuming…");
            job.completedAt.clear();
            m_jobKinds.insert(job.id, job.kind);
            continue;
        }
        if (isJobTerminal(job.status))
            continue;
        if (!job.httpDownload && m_settings->torboxEnabled())
            job.torboxDownload = true;
        if (isJobQueued(job.status) || isJobActive(job.status))
            job.status = QStringLiteral("starting");
        m_jobKinds.insert(job.id, job.kind);
    }

    m_jobStore->setJobs(jobs);
    m_jobs->setJobs(jobs);

    for (const auto& job : jobs) {
        if (isJobTerminal(job.status) || job.pluginDownload)
            continue;
        const bool wasPaused = isJobPaused(job.status);
        if (wasPaused && (job.torboxDownload || job.magnetUri.startsWith(QStringLiteral("hydra:"))))
            continue;
        startDownload(job);
        if (!job.httpDownload && !job.torboxDownload && wasPaused)
            m_torrent->setPaused(job.id, true);
    }

    pruneFinishedJobs();
}

void JobOrchestrator::flushPersistence()
{
    m_persistTimer.stop();
    m_dirty = false;

    QVector<JobEntry> jobs;
    jobs.reserve(m_jobs->rowCount());
    for (int i = 0; i < m_jobs->rowCount(); ++i)
        jobs.append(jobFromModelRow(i));
    m_jobStore->setJobs(jobs);
}

void JobOrchestrator::shutdownDownloads()
{
    m_torbox->shutdown();
    const auto resolvers = m_hydraResolvers;
    m_hydraResolvers.clear();
    for (auto* resolver : resolvers) {
        resolver->cancel();
        resolver->deleteLater();
    }
}

QString JobOrchestrator::pickMagnet(const QStringList& uris) const
{
    for (const QString& uri : uris) {
        if (uri.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive))
            return uri;
    }
    return uris.value(0);
}

QString JobOrchestrator::findActiveJobId(const QString& entryId,
                                         const QString& parentEntryId) const
{
    for (int i = 0; i < m_jobs->rowCount(); ++i) {
        const QString jobEntryId =
            m_jobs->data(m_jobs->index(i, 0), JobModel::EntryIdRole).toString();
        const QString jobParentId =
            m_jobs->data(m_jobs->index(i, 0), JobModel::ParentEntryIdRole).toString();
        const QString status =
            m_jobs->data(m_jobs->index(i, 0), JobModel::StatusRole).toString();
        if (jobEntryId != entryId || jobParentId != parentEntryId)
            continue;
        if (isJobInProgress(status))
            return m_jobs->data(m_jobs->index(i, 0), JobModel::JobIdRole).toString();
    }
    return {};
}

QString JobOrchestrator::findExistingJobId(const QString& entryId,
                                            const QString& parentEntryId) const
{
    QString latestId;
    QString latestCreated;
    for (int i = 0; i < m_jobs->rowCount(); ++i) {
        const QString jobEntryId =
            m_jobs->data(m_jobs->index(i, 0), JobModel::EntryIdRole).toString();
        const QString jobParentId =
            m_jobs->data(m_jobs->index(i, 0), JobModel::ParentEntryIdRole).toString();
        if (jobEntryId != entryId || jobParentId != parentEntryId)
            continue;

        const QString status =
            m_jobs->data(m_jobs->index(i, 0), JobModel::StatusRole).toString();
        if (isJobInProgress(status))
            return m_jobs->data(m_jobs->index(i, 0), JobModel::JobIdRole).toString();

        const QString created =
            m_jobs->data(m_jobs->index(i, 0), JobModel::CreatedAtRole).toString();
        if (latestId.isEmpty() || created > latestCreated) {
            latestId = m_jobs->data(m_jobs->index(i, 0), JobModel::JobIdRole).toString();
            latestCreated = created;
        }
    }
    return latestId;
}

QString JobOrchestrator::createJob(const QString& title, JobKind kind, const QString& entryId,
                                     const QString& sourceId, const QString& downloadUri,
                                     const QString& saveSubdir, const QString& coverUrl,
                                     const QString& libraryId, const QString& parentEntryId,
                                     bool httpDownload, const QString& referer)
{
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString downloadsRoot = m_settings->resolvedDownloadsRoot(libraryId);
    const QString savePath = downloadsRoot + QLatin1Char('/') + saveSubdir;

    JobEntry job;
    job.id = jobId;
    job.title = title;
    job.kind = kind;
    job.status = QStringLiteral("starting");
    job.progress = 0;
    job.detail = httpDownload ? QStringLiteral("Downloading…")
                              : QStringLiteral("0% · Fetching metadata…");
    job.entryId = entryId;
    job.sourceId = sourceId;
    job.magnetUri = downloadUri;
    job.savePath = savePath;
    job.coverUrl = coverUrl;
    job.libraryId = libraryId.isEmpty() ? m_settings->defaultLibraryId() : libraryId;
    job.parentEntryId = parentEntryId;
    job.referer = referer;
    job.httpDownload = httpDownload;
    job.torboxDownload = !httpDownload && m_settings->torboxEnabled()
        && (downloadUri.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)
            || downloadUri.startsWith(QStringLiteral("hydra:")));
    job.createdAt = isoNow();
    m_jobKinds.insert(jobId, kind);

    m_jobs->addJob(job);
    persistJob(job);
    startDownload(job);
    return jobId;
}

void JobOrchestrator::routePendingTorrentsThroughTorbox()
{
    if (!m_settings->torboxEnabled())
        return;
    for (int row = 0; row < m_jobs->rowCount(); ++row) {
        JobEntry job = jobFromModelRow(row);
        if (job.httpDownload || job.pluginDownload || job.torboxDownload
            || job.kind == JobKind::Move || isJobTerminal(job.status)
            || job.status == QStringLiteral("installing") || job.status == QStringLiteral("moving"))
            continue;
        m_torrent->cancel(job.id, false);
        job.torboxDownload = true;
        persistJob(job);
        updateJobInModel(job);
        if (!isJobPaused(job.status) && !m_hydraResolvers.contains(job.id))
            startDownload(job);
    }
}

void JobOrchestrator::startDownload(const JobEntry& input)
{
    JobEntry job = input;
    if (!job.httpDownload && m_settings->torboxEnabled() && !job.torboxDownload) {
        job.torboxDownload = true;
        persistJob(job);
        updateJobInModel(job);
    }
    if (job.magnetUri.startsWith(QStringLiteral("hydra:"))) {
        auto* resolver = new HydraCatalogClient(this);
        m_hydraResolvers.insert(job.id, resolver);
        setJobPhase(job.id, QStringLiteral("starting"), QCoreApplication::translate("Core", "Getting download link from Hydra"));
        connect(resolver, &HydraCatalogClient::resolved, this, [this, id = job.id, resolver](const QString& uri) {
            m_hydraResolvers.remove(id);
            resolver->deleteLater();
            const int row = m_jobs->indexOfJob(id);
            if (row < 0)
                return;
            JobEntry ready = jobFromModelRow(row);
            if (isJobTerminal(ready.status) || isJobPaused(ready.status))
                return;
            ready.magnetUri = uri;
            ready.httpDownload = uri.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                || uri.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
            if (ready.httpDownload)
                ready.torboxDownload = false;
            persistJob(ready);
            updateJobInModel(ready);
            startDownload(ready);
        });
        connect(resolver, &HydraCatalogClient::failed, this, [this, id = job.id, resolver](const QString& error) {
            m_hydraResolvers.remove(id);
            resolver->deleteLater();
            onTorrentFailed(id, error);
        });
        resolver->resolve(job.magnetUri);
    } else if (job.torboxDownload) {
        m_torbox->addJob(job.id, job.magnetUri, job.savePath, job.torboxTorrentId);
    } else if (job.httpDownload) {
        startHttp(job);
    } else {
        startTorrent(job);
    }
}

void JobOrchestrator::startTorrent(const JobEntry& job)
{
    if (!m_torrent->addJob(job.id, job.magnetUri, job.savePath)) {
        JobEntry failed = job;
        failed.status = QStringLiteral("failed");
        failed.detail = QStringLiteral("Failed to start torrent");
        failed.completedAt = isoNow();
        updateJobInModel(failed);
        persistJob(failed);
        emit downloadFailed(job.id, failed.detail);
        m_jobKinds.remove(job.id);
    }
}

void JobOrchestrator::startHttp(const JobEntry& job)
{
    if (!m_http->addJob(job.id, job.magnetUri, job.referer, job.savePath)) {
        JobEntry failed = job;
        failed.status = QStringLiteral("failed");
        failed.detail = QStringLiteral("Failed to start HTTP download");
        failed.completedAt = isoNow();
        updateJobInModel(failed);
        persistJob(failed);
        emit downloadFailed(job.id, failed.detail);
        m_jobKinds.remove(job.id);
    }
}

void JobOrchestrator::persistJob(const JobEntry& job)
{
    m_jobStore->upsertJob(job);
    m_dirty = true;
    if (!m_persistTimer.isActive())
        m_persistTimer.start();
}

JobEntry JobOrchestrator::jobFromModelRow(int row) const
{
    JobEntry job;
    const QModelIndex idx = m_jobs->index(row, 0);
    job.id = m_jobs->data(idx, JobModel::JobIdRole).toString();
    job.title = m_jobs->data(idx, JobModel::TitleRole).toString();
    job.kind = static_cast<JobKind>(m_jobs->data(idx, JobModel::KindRole).toInt());
    job.status = m_jobs->data(idx, JobModel::StatusRole).toString();
    job.progress = m_jobs->data(idx, JobModel::ProgressRole).toInt();
    job.detail = m_jobs->data(idx, JobModel::DetailRole).toString();
    job.bytesDownloaded = m_jobs->data(idx, JobModel::BytesDownloadedRole).toLongLong();
    job.totalBytes = m_jobs->data(idx, JobModel::TotalBytesRole).toLongLong();
    job.entryId = m_jobs->data(idx, JobModel::EntryIdRole).toString();
    job.sourceId = m_jobs->data(idx, JobModel::SourceIdRole).toString();
    job.magnetUri = m_jobs->data(idx, JobModel::MagnetUriRole).toString();
    job.savePath = m_jobs->data(idx, JobModel::SavePathRole).toString();
    job.coverUrl = m_jobs->data(idx, JobModel::CoverUrlRole).toString();
    job.libraryId = m_jobs->data(idx, JobModel::LibraryIdRole).toString();
    job.parentEntryId = m_jobs->data(idx, JobModel::ParentEntryIdRole).toString();
    job.referer = m_jobs->data(idx, JobModel::RefererRole).toString();
    job.httpDownload = m_jobs->data(idx, JobModel::HttpDownloadRole).toBool();
    job.pluginDownload = m_jobs->data(idx, JobModel::PluginDownloadRole).toBool();
    job.artifactPath = m_jobs->data(idx, JobModel::ArtifactPathRole).toString();
    job.createdAt = m_jobs->data(idx, JobModel::CreatedAtRole).toString();
    job.completedAt = m_jobs->data(idx, JobModel::CompletedAtRole).toString();
    // Expected markers live on the store (not QML roles); keep them across progress updates.
    if (m_jobStore) {
        if (const JobEntry* stored = m_jobStore->jobById(job.id)) {
            job.expectedVersion = stored->expectedVersion;
            job.expectedUploadDate = stored->expectedUploadDate;
            job.expectedSteamAppId = stored->expectedSteamAppId;
            job.torboxDownload = stored->torboxDownload;
            job.torboxTorrentId = stored->torboxTorrentId;
        }
    }
    return job;
}

void JobOrchestrator::updateJobInModel(const JobEntry& job)
{
    m_jobs->updateJob(job);
}

QString JobOrchestrator::startCatalogDownload(const CatalogEntry& entry, JobKind kind,
                                              const QString& libraryId)
{
    const QString uri = pickMagnet(entry.magnetUris);
    if (uri.isEmpty())
        return {};

    const QString existing = findActiveJobId(entry.id, {});
    if (!existing.isEmpty())
        return existing;

    const QString prefix =
        kind == JobKind::Update ? QStringLiteral("update") : QStringLiteral("install");
    const QString saveSubdir = QStringLiteral("%1/%2").arg(prefix, entry.id);
    const QString title = kind == JobKind::Update
                              ? QStringLiteral("Updating %1").arg(entry.title)
                              : QStringLiteral("Downloading %1").arg(entry.title);

    const QString libId = libraryId.isEmpty() ? m_settings->defaultLibraryId() : libraryId;
    const bool http = uri.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                      || uri.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
    return createJob(title, kind, entry.id, entry.sourceId, uri, saveSubdir, entry.coverUrl, libId,
                     {}, http);
}

QString JobOrchestrator::startPluginOwnedDownload(const CatalogEntry& entry, JobKind kind,
                                                    const QString& libraryId)
{
    const QString existing = findActiveJobId(entry.id, {});
    if (!existing.isEmpty())
        return existing;

    const QString prefix = kind == JobKind::Update   ? QStringLiteral("update")
                           : kind == JobKind::Verify ? QStringLiteral("verify")
                                                     : QStringLiteral("install");
    const QString saveSubdir = QStringLiteral("%1/%2").arg(prefix, entry.id);
    const QString title = kind == JobKind::Update
                              ? QStringLiteral("Updating %1").arg(entry.title)
                          : kind == JobKind::Verify
                              ? QStringLiteral("Verifying %1").arg(entry.title)
                              : QStringLiteral("Downloading %1").arg(entry.title);
    const QString libId = libraryId.isEmpty() ? m_settings->defaultLibraryId() : libraryId;
    const QString downloadsRoot = m_settings->resolvedDownloadsRoot(libId);
    const QString savePath = downloadsRoot + QLatin1Char('/') + saveSubdir;

    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    JobEntry job;
    job.id = jobId;
    job.title = title;
    job.kind = kind;
    job.status = QStringLiteral("starting");
    job.progress = 0;
    job.detail = QStringLiteral("Preparing…");
    job.entryId = entry.id;
    job.sourceId = entry.sourceId;
    job.magnetUri = entry.steamAppId.isEmpty()
                        ? QString()
                        : QStringLiteral("steam://app/%1").arg(entry.steamAppId);
    job.savePath = savePath;
    job.coverUrl = entry.coverUrl;
    job.libraryId = libId;
    job.pluginDownload = true;
    job.expectedVersion = entry.version;
    job.expectedUploadDate = entry.uploadDate;
    job.expectedSteamAppId = entry.steamAppId;
    job.createdAt = isoNow();
    const qint64 estimatedTotal = parseSizeLabelBytes(entry.sizeLabel);
    if (estimatedTotal > 0) {
        m_pluginEstimatedTotal.insert(jobId, estimatedTotal);
        job.totalBytes = estimatedTotal;
    }
    m_jobKinds.insert(jobId, kind);
    m_jobs->addJob(job);
    m_jobStore->upsertJob(job);
    return jobId;
}


} // namespace arachnel::core
