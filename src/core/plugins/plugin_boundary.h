#pragma once

#include "plugin_interface.h"

namespace arachnel::core {

/**
 * Give the host its own copy of plugin data.
 *
 * A QString made from a literal (QStringLiteral, fromRawData) owns nothing: it points
 * into the image of the module that made it, and so does every copy of it. Plugins are
 * unloaded when they are updated or reinstalled, and Sprout reinstalls them after every
 * app update, so any copy the host kept - a plugin's name in the library, a launch
 * option, a download status in the job list - then points at memory that is gone. v0.2.12
 * crashed saving library.json right after such a reinstall (QCborValue from QString).
 *
 * Heap strings need nothing: Qt allocates them in Qt6Core, which outlives every plugin.
 */
void ownPluginString(QString& value);
void ownPluginStrings(QStringList& values);
void ownPluginData(CatalogComponent& component);
void ownPluginData(CatalogEntry& entry);
void ownPluginData(LaunchInfo& info);
void ownPluginData(InstallResult& result);
void ownPluginData(InstallAnalysis& analysis);
void ownPluginData(GameLaunchOption& option);
void ownPluginData(OwnedDownloadProgress& progress);

/**
 * Stands between the host and a loaded plugin, so nothing the plugin returns keeps
 * pointing into its image (see ownPluginString). PluginHost hands out this instead of
 * the plugin itself.
 */
class PluginBoundaryAdapter final : public ISourcePlugin
{
public:
    explicit PluginBoundaryAdapter(ISourcePlugin* target) : m_target(target) {}

    QString id() const override;
    QString name() const override;
    QString description() const override;
    QString version() const override;
    QStringList capabilities() const override;

    QVector<CatalogEntry> catalog() const override;
    QVector<CatalogEntry> search(const QString& query) const override;
    std::optional<CatalogEntry> entryById(const QString& entryId) const override;

    InstallResult installFromDownload(const InstallContext& ctx) const override;
    InstallResult installAddonFromDownload(const AddonInstallContext& ctx) const override;
    InstallAnalysis analyzeDownload(const InstallContext& ctx) const override;
    InstallAnalysis analyzeFileNames(const QStringList& fileNames) const override;

    std::optional<QString> detectUpdate(const LibraryGame& local,
                                        const CatalogEntry& remote) const override;
    LaunchInfo launchInfo(const LibraryGame& local) const override;
    void resetCatalogCache() override;

    InstallResult startOwnedDownload(
        const InstallContext& ctx,
        const std::function<void(const OwnedDownloadProgress&)>& onProgress) const override;
    void cancelOwnedDownload(const QString& jobId) const override;
    void setOwnedDownloadPaused(const QString& jobId, bool paused) const override;

    bool applySelectedDlc(const LibraryGame& local,
                          const QStringList& enabledAddonIds) const override;
    bool updateMayBreakDlc(const LibraryGame& local, const CatalogEntry& remote) const override;
    QVector<GameLaunchOption> launchOptions(const LibraryGame& local) const override;

private:
    ISourcePlugin* m_target = nullptr;
};

} // namespace arachnel::core
