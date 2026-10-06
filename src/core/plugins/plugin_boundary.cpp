#include "plugin_boundary.h"

#include <algorithm>

namespace arachnel::core {

namespace {

// A string Qt allocated always has room of its own; a literal or fromRawData string has
// none, because its characters live wherever the code that made it put them.
bool borrowsCharacters(const QString& value)
{
    return !value.isNull() && value.capacity() == 0;
}

QString owned(QString value)
{
    ownPluginString(value);
    return value;
}

QStringList owned(QStringList values)
{
    ownPluginStrings(values);
    return values;
}

template <typename T>
T ownedData(T value)
{
    ownPluginData(value);
    return value;
}

QVector<CatalogEntry> ownedEntries(QVector<CatalogEntry> entries)
{
    for (CatalogEntry& entry : entries)
        ownPluginData(entry);
    return entries;
}

} // namespace

void ownPluginString(QString& value)
{
    if (borrowsCharacters(value))
        value = QString(value.constData(), value.size());
}

void ownPluginStrings(QStringList& values)
{
    const bool anyBorrowed = std::any_of(values.cbegin(), values.cend(), borrowsCharacters);
    if (!anyBorrowed)
        return;
    for (QString& value : values)
        ownPluginString(value);
}

void ownPluginData(CatalogComponent& component)
{
    ownPluginString(component.id);
    ownPluginString(component.title);
    ownPluginStrings(component.magnetUris);
    ownPluginString(component.downloadUrl);
    ownPluginString(component.referer);
    ownPluginString(component.getfileUrl);
    ownPluginString(component.fileSize);
    ownPluginString(component.uploadDate);
    ownPluginString(component.coverUrl);
    ownPluginStrings(component.screenshotUrls);
}

void ownPluginData(CatalogEntry& entry)
{
    ownPluginString(entry.id);
    ownPluginString(entry.title);
    ownPluginString(entry.coverUrl);
    ownPluginString(entry.remoteCoverUrl);
    ownPluginString(entry.sourceId);
    ownPluginString(entry.sourcePageUrl);
    ownPluginString(entry.version);
    ownPluginString(entry.sizeLabel);
    ownPluginString(entry.description);
    ownPluginString(entry.genres);
    ownPluginString(entry.steamAppId);
    ownPluginString(entry.trailerUrl);
    ownPluginString(entry.trailerThumbnailUrl);
    ownPluginStrings(entry.screenshotUrls);
    ownPluginStrings(entry.magnetUris);
    ownPluginString(entry.uploadDate);
    ownPluginString(entry.parentEntryId);
    for (CatalogComponent& addon : entry.addons)
        ownPluginData(addon);
    ownPluginString(entry.titleLower);
}

void ownPluginData(LaunchInfo& info)
{
    ownPluginString(info.executable);
    ownPluginString(info.workingDirectory);
    ownPluginStrings(info.arguments);
    ownPluginStrings(info.argumentsPrefix);
    ownPluginString(info.wineDllOverrides);
    QHash<QString, QString> environment;
    environment.reserve(info.environmentExtras.size());
    for (auto it = info.environmentExtras.cbegin(); it != info.environmentExtras.cend(); ++it)
        environment.insert(owned(it.key()), owned(it.value()));
    info.environmentExtras = std::move(environment);
}

void ownPluginData(InstallResult& result)
{
    ownPluginString(result.installPath);
    ownPluginString(result.error);
}

void ownPluginData(InstallAnalysis& analysis)
{
    ownPluginString(analysis.methodId);
    ownPluginString(analysis.detail);
}

void ownPluginData(GameLaunchOption& option)
{
    ownPluginString(option.id);
    ownPluginString(option.title);
    ownPluginString(option.executable);
    ownPluginString(option.workingDirectory);
    ownPluginStrings(option.arguments);
    ownPluginString(option.type);
}

void ownPluginData(OwnedDownloadProgress& progress)
{
    ownPluginString(progress.detail);
    ownPluginString(progress.status);
}

QString PluginBoundaryAdapter::id() const
{
    return owned(m_target->id());
}

QString PluginBoundaryAdapter::name() const
{
    return owned(m_target->name());
}

QString PluginBoundaryAdapter::description() const
{
    return owned(m_target->description());
}

QString PluginBoundaryAdapter::version() const
{
    return owned(m_target->version());
}

QStringList PluginBoundaryAdapter::capabilities() const
{
    return owned(m_target->capabilities());
}

QVector<CatalogEntry> PluginBoundaryAdapter::catalog() const
{
    return ownedEntries(m_target->catalog());
}

QVector<CatalogEntry> PluginBoundaryAdapter::search(const QString& query) const
{
    return ownedEntries(m_target->search(query));
}

std::optional<CatalogEntry> PluginBoundaryAdapter::entryById(const QString& entryId) const
{
    std::optional<CatalogEntry> entry = m_target->entryById(entryId);
    if (entry)
        ownPluginData(*entry);
    return entry;
}

InstallResult PluginBoundaryAdapter::installFromDownload(const InstallContext& ctx) const
{
    return ownedData(m_target->installFromDownload(ctx));
}

InstallResult PluginBoundaryAdapter::installAddonFromDownload(const AddonInstallContext& ctx) const
{
    return ownedData(m_target->installAddonFromDownload(ctx));
}

InstallAnalysis PluginBoundaryAdapter::analyzeDownload(const InstallContext& ctx) const
{
    return ownedData(m_target->analyzeDownload(ctx));
}

InstallAnalysis PluginBoundaryAdapter::analyzeFileNames(const QStringList& fileNames) const
{
    return ownedData(m_target->analyzeFileNames(fileNames));
}

std::optional<QString> PluginBoundaryAdapter::detectUpdate(const LibraryGame& local,
                                                           const CatalogEntry& remote) const
{
    std::optional<QString> version = m_target->detectUpdate(local, remote);
    if (version)
        ownPluginString(*version);
    return version;
}

LaunchInfo PluginBoundaryAdapter::launchInfo(const LibraryGame& local) const
{
    return ownedData(m_target->launchInfo(local));
}

void PluginBoundaryAdapter::resetCatalogCache()
{
    m_target->resetCatalogCache();
}

InstallResult PluginBoundaryAdapter::startOwnedDownload(
    const InstallContext& ctx,
    const std::function<void(const OwnedDownloadProgress&)>& onProgress) const
{
    // Progress text ends up in the job list and in jobs.json.
    const std::function<void(const OwnedDownloadProgress&)> owningProgress =
        [onProgress](const OwnedDownloadProgress& progress) {
            if (!onProgress)
                return;
            onProgress(ownedData(progress));
        };
    return ownedData(m_target->startOwnedDownload(ctx, owningProgress));
}

void PluginBoundaryAdapter::cancelOwnedDownload(const QString& jobId) const
{
    m_target->cancelOwnedDownload(jobId);
}

void PluginBoundaryAdapter::setOwnedDownloadPaused(const QString& jobId, bool paused) const
{
    m_target->setOwnedDownloadPaused(jobId, paused);
}

bool PluginBoundaryAdapter::applySelectedDlc(const LibraryGame& local,
                                             const QStringList& enabledAddonIds) const
{
    return m_target->applySelectedDlc(local, enabledAddonIds);
}

bool PluginBoundaryAdapter::updateMayBreakDlc(const LibraryGame& local,
                                              const CatalogEntry& remote) const
{
    return m_target->updateMayBreakDlc(local, remote);
}

QVector<GameLaunchOption> PluginBoundaryAdapter::launchOptions(const LibraryGame& local) const
{
    QVector<GameLaunchOption> options = m_target->launchOptions(local);
    for (GameLaunchOption& option : options)
        ownPluginData(option);
    return options;
}

} // namespace arachnel::core
