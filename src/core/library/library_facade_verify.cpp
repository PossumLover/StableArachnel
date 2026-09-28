#include "core_controller_impl.h"

#include "online_fix_overlay.h"

#include <QJsonArray>

namespace arachnel::core {

namespace {

/** DLC the install carries, so a verify neither adds nor drops any. */
QStringList installedAddonIds(const LibraryGame& game)
{
    QStringList ids;
    for (const InstalledComponent& component : game.components) {
        if (component.installed && !ids.contains(component.id))
            ids.append(component.id);
    }
    // steamidra records its DLC selection in its own marker, which can know DLC the
    // library row does not. Leaving one out would verify without that DLC's depots and
    // rewrite the Online Fix unlocks without it.
    QFile marker(game.installPath + QStringLiteral("/.arachnel-steamidra"));
    if (marker.open(QIODevice::ReadOnly)) {
        const QJsonArray selected = QJsonDocument::fromJson(marker.readAll())
                                        .object()
                                        .value(QStringLiteral("selectedDlc"))
                                        .toArray();
        for (const QJsonValue& value : selected) {
            const QString appId = value.toVariant().toString().trimmed();
            const QString id = QStringLiteral("steam-") + appId;
            if (!appId.isEmpty() && !ids.contains(id))
                ids.append(id);
        }
    }
    return ids;
}

} // namespace

bool CoreController::canVerifyGameFiles(const QString& entryId) const
{
    const LibraryGame* game = m_libraryStore.gameById(entryId);
    if (!game || game->installPath.isEmpty() || !QFileInfo::exists(game->installPath))
        return false;
    // Only a plugin that downloads the game itself has a file list to check against.
    // Torrent installs come from archives and installers, and the payload is deleted
    // once the game is installed.
    return m_pluginHost && m_pluginHost->pluginOwnsDownload(game->sourceId);
}

bool CoreController::isVerifyingGameFiles(const QString& entryId) const
{
    for (const JobEntry& job : m_jobStore.jobs()) {
        if (job.entryId == entryId && job.kind == JobKind::Verify && isJobInProgress(job.status))
            return true;
    }
    return false;
}

/**
 * Steam's "verify integrity of game files", for sources that download the game
 * themselves. The plugin runs its own download again into the existing install in
 * "update" mode, which checks every file against the source's manifests and fetches
 * only the ones that are missing or differ. The manifests are the source's current
 * ones, so a pending update is installed along the way.
 */
void CoreController::verifyGameFiles(const QString& entryId)
{
    const LibraryGame* existing = m_libraryStore.gameById(entryId);
    if (!existing || existing->installPath.isEmpty() || !QDir(existing->installPath).exists()) {
        showNotice(QCoreApplication::translate("Core", "Install the game first"));
        return;
    }
    const LibraryGame game = *existing;

    if (runningGameId() == entryId
        || (m_runtimeSetupInProgress && m_runtimeSetupGameId == entryId)) {
        showNotice(QCoreApplication::translate("Core", "Close %1 before verifying its files")
                       .arg(game.title));
        return;
    }
    if (entryHasActiveJob(entryId)) {
        showNotice(QCoreApplication::translate(
                       "Core", "%1 is busy - wait for its current task to finish")
                       .arg(game.title));
        return;
    }

    ISourcePlugin* plugin = m_pluginHost ? m_pluginHost->plugin(game.sourceId) : nullptr;
    if (!plugin) {
        showNotice(QCoreApplication::translate("Core", "Plugin not loaded: %1").arg(game.sourceId));
        return;
    }
    if (!m_pluginHost->pluginOwnsDownload(game.sourceId)) {
        showNotice(QCoreApplication::translate("Core", "This source can't verify game files"));
        return;
    }

    // The offer from the game's own source: its version markers describe what the
    // plugin is about to check against, and the commit records them.
    std::optional<CatalogEntry> offer;
    if (m_catalogController)
        offer = m_catalogController->resolveInstallOffer(entryId, game.sourceId);
    CatalogEntry entry;
    if (offer && offer->id == game.id) {
        entry = *offer;
    } else {
        entry.id = game.id;
        entry.sourceId = game.sourceId;
        entry.version = game.version;
        entry.uploadDate = game.uploadDate;
        if (!game.magnetUri.isEmpty())
            entry.magnetUris.append(game.magnetUri);
    }
    if (entry.title.isEmpty())
        entry.title = game.title;
    if (entry.coverUrl.isEmpty())
        entry.coverUrl = game.coverUrl;
    if (entry.sizeLabel.isEmpty())
        entry.sizeLabel = game.sizeLabel;
    if (entry.steamAppId.isEmpty())
        entry.steamAppId = game.steamAppId;

    QString sourceRef = entry.steamAppId.isEmpty() ? entry.magnetUris.value(0) : entry.steamAppId;
    if (sourceRef.startsWith(QStringLiteral("steam://app/")))
        sourceRef = sourceRef.mid(QStringLiteral("steam://app/").size());
    if (sourceRef.isEmpty()) {
        showNotice(QCoreApplication::translate("Core", "No Steam App ID for %1").arg(game.title));
        return;
    }

    const QString libId = game.libraryId.isEmpty() ? m_settings.defaultLibraryId() : game.libraryId;
    const QString jobId = m_jobOrchestrator->startPluginOwnedDownload(entry, JobKind::Verify, libId);
    if (jobId.isEmpty()) {
        showNotice(QCoreApplication::translate("Core", "Could not start verifying %1")
                       .arg(game.title));
        return;
    }

    InstallContext ctx;
    ctx.jobId = jobId;
    ctx.entryId = game.id;
    ctx.sourceId = game.sourceId;
    ctx.title = entry.title;
    ctx.targetPath = game.installPath;
    ctx.downloadsPath = m_settings.resolvedDownloadsRoot(libId);
    const JobEntry* job = m_jobStore.jobById(jobId);
    ctx.downloadPath = job && !job->savePath.isEmpty()
                           ? job->savePath
                           : ctx.downloadsPath + QStringLiteral("/verify/") + game.id;
    ctx.magnetUri = sourceRef;
    ctx.uploadDate = entry.uploadDate;
    ctx.version = entry.version;
    ctx.steamAppId = entry.steamAppId;
    ctx.installKind = game.installKind;
    // "update" is the mode that has an owns_download plugin re-check files it already has.
    ctx.installMode = QStringLiteral("update");
    prepareGameFilesVerify(jobId, game, ctx);

    qInfo().noquote() << "[verify]" << game.id << "source" << game.sourceId << "addons"
                      << (ctx.selectedAddonIds.isEmpty()
                              ? QStringLiteral("(none)")
                              : ctx.selectedAddonIds.join(QLatin1Char(',')))
                      << "target" << ctx.targetPath;

    m_pluginHost->runOwnedDownloadAsync(
        plugin, ctx,
        [this, jobId](const OwnedDownloadProgress& progress) {
            m_jobOrchestrator->reportPluginProgress(jobId, progress);
        },
        [this, jobId, entryId = game.id](const InstallResult& result) {
            if (result.success) {
                m_jobOrchestrator->completePluginDownload(jobId, result.installPath);
                return;
            }
            restoreOnlineFixAfterVerify(jobId, entryId);
            m_jobOrchestrator->failPluginDownload(
                jobId, result.error.isEmpty()
                           ? QCoreApplication::translate("Core", "Could not verify game files")
                           : result.error);
        });
}

void CoreController::prepareGameFilesVerify(const QString& jobId, const LibraryGame& game,
                                            InstallContext& ctx)
{
    ctx.selectedAddonIds = installedAddonIds(game);
    // The plugin only re-embeds Online Fix once its run is done, so on a resume the files
    // still show how the user left it.
    const OnlineFixOverlayState fix = detectOnlineFixOverlay(game.installPath);
    if (fix.present && !fix.enabled)
        m_verifyKeepsOnlineFixOff.insert(jobId);
}

void CoreController::beginGameFilesVerifyCommit(const QString& jobId, const QString& entryId,
                                                const QString& sourceId)
{
    const LibraryGame* game = m_libraryStore.gameById(entryId);
    if (!game)
        return;
    // The commit rebuilds the DLC list from the catalog plus the session's selection.
    // Without one, DLC the catalog has not listed yet would be dropped from the library.
    beginInstallSession(entryId, jobId, sourceId, installedAddonIds(*game));
}

void CoreController::restoreOnlineFixAfterVerify(const QString& jobId, const QString& entryId)
{
    if (!m_verifyKeepsOnlineFixOff.remove(jobId))
        return;
    // The plugin embeds Online Fix again at the end of its run. Turn it back off if that
    // is how the user had it.
    const LibraryGame* game = m_libraryStore.gameById(entryId);
    if (game && detectOnlineFixOverlay(game->installPath).enabled)
        setGameOnlineFixEnabled(entryId, false);
}

void CoreController::finishGameFilesVerify(const QString& jobId, const QString& entryId)
{
    // The session only carried the DLC selection into the commit. It is left open when
    // the catalog has no row for the game, so close it here.
    m_installSessionService->cancelEntry(entryId);
    restoreOnlineFixAfterVerify(jobId, entryId);
    const LibraryGame* game = m_libraryStore.gameById(entryId);
    if (!game)
        return;
    const QString title = game->title;
    m_jobOrchestrator->setJobPhase(jobId, QStringLiteral("completed"),
                                   QStringLiteral("Files verified"));
    showNotice(QCoreApplication::translate("Core", "Files verified: %1").arg(title));
}

} // namespace arachnel::core
