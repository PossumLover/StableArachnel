#pragma once

#include "plugin_interface.h"

namespace arachnel::core {

/**
 * ISourcePlugin exactly as it was laid out before commit 86b028f.
 *
 * That commit moved `updateMayBreakDlc` from between `detectUpdate` and
 * `launchInfo` to the end of the class. Plugins compiled against the older
 * header therefore have `updateMayBreakDlc` where the current host expects
 * `launchInfo`, and every slot after it is off by one - so the host calling
 * `launchInfo` lands in `updateMayBreakDlc`, which returns `bool` where a
 * `LaunchInfo` was expected and leaves the caller's sret buffer untouched.
 *
 * Declaration order here IS the ABI. Do not touch it; it describes a layout that
 * already shipped.
 */
class ISourcePluginRev1
{
public:
    virtual ~ISourcePluginRev1() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    virtual QStringList capabilities() const = 0;

    virtual QVector<CatalogEntry> catalog() const = 0;
    virtual QVector<CatalogEntry> search(const QString& query) const = 0;
    virtual std::optional<CatalogEntry> entryById(const QString& entryId) const = 0;

    virtual InstallResult installFromDownload(const InstallContext& ctx) const = 0;
    virtual InstallResult installAddonFromDownload(const AddonInstallContext& ctx) const = 0;

    virtual InstallAnalysis analyzeDownload(const InstallContext& ctx) const = 0;
    virtual InstallAnalysis analyzeFileNames(const QStringList& fileNames) const = 0;

    virtual std::optional<QString> detectUpdate(const LibraryGame& local,
                                                const CatalogEntry& remote) const = 0;
    virtual bool updateMayBreakDlc(const LibraryGame& local, const CatalogEntry& remote) const = 0;

    virtual LaunchInfo launchInfo(const LibraryGame& local) const = 0;

    virtual void resetCatalogCache() = 0;

    virtual InstallResult startOwnedDownload(
        const InstallContext& ctx,
        const std::function<void(const OwnedDownloadProgress&)>& onProgress) const = 0;

    virtual void cancelOwnedDownload(const QString& jobId) const = 0;
    virtual void setOwnedDownloadPaused(const QString& jobId, bool paused) const = 0;

    virtual bool applySelectedDlc(const LibraryGame& local,
                                  const QStringList& enabledAddonIds) const = 0;
};

/**
 * Presents a revision-1 plugin through the current interface.
 *
 * Every call is routed to the slot the plugin actually has. Calls that would put
 * a CatalogEntry across the boundary are refused whenever the plugin's
 * CatalogEntry layout differs from the host's - under API 4 the catalog itself
 * crosses as JSON, so a plugin can still do its job without them.
 */
class SourcePluginRev1Adapter final : public ISourcePlugin
{
public:
    SourcePluginRev1Adapter(ISourcePluginRev1* target, bool catalogEntryLayoutMatches)
        : m_target(target)
        , m_entryLayoutOk(catalogEntryLayoutMatches)
    {
    }

    QString id() const override { return m_target->id(); }
    QString name() const override { return m_target->name(); }
    QString description() const override { return m_target->description(); }
    QString version() const override { return m_target->version(); }
    QStringList capabilities() const override { return m_target->capabilities(); }

    QVector<CatalogEntry> catalog() const override
    {
        return m_entryLayoutOk ? m_target->catalog() : QVector<CatalogEntry>{};
    }

    QVector<CatalogEntry> search(const QString& query) const override
    {
        return m_entryLayoutOk ? m_target->search(query) : QVector<CatalogEntry>{};
    }

    std::optional<CatalogEntry> entryById(const QString& entryId) const override
    {
        if (!m_entryLayoutOk)
            return std::nullopt;
        return m_target->entryById(entryId);
    }

    InstallResult installFromDownload(const InstallContext& ctx) const override
    {
        return m_target->installFromDownload(ctx);
    }

    InstallResult installAddonFromDownload(const AddonInstallContext& ctx) const override
    {
        return m_target->installAddonFromDownload(ctx);
    }

    InstallAnalysis analyzeDownload(const InstallContext& ctx) const override
    {
        return m_target->analyzeDownload(ctx);
    }

    InstallAnalysis analyzeFileNames(const QStringList& fileNames) const override
    {
        return m_target->analyzeFileNames(fileNames);
    }

    std::optional<QString> detectUpdate(const LibraryGame& local,
                                        const CatalogEntry& remote) const override
    {
        if (!m_entryLayoutOk)
            return std::nullopt;
        return m_target->detectUpdate(local, remote);
    }

    LaunchInfo launchInfo(const LibraryGame& local) const override
    {
        return m_target->launchInfo(local);
    }

    void resetCatalogCache() override { m_target->resetCatalogCache(); }

    InstallResult startOwnedDownload(
        const InstallContext& ctx,
        const std::function<void(const OwnedDownloadProgress&)>& onProgress) const override
    {
        return m_target->startOwnedDownload(ctx, onProgress);
    }

    void cancelOwnedDownload(const QString& jobId) const override
    {
        m_target->cancelOwnedDownload(jobId);
    }

    void setOwnedDownloadPaused(const QString& jobId, bool paused) const override
    {
        m_target->setOwnedDownloadPaused(jobId, paused);
    }

    bool applySelectedDlc(const LibraryGame& local,
                          const QStringList& enabledAddonIds) const override
    {
        return m_target->applySelectedDlc(local, enabledAddonIds);
    }

    bool updateMayBreakDlc(const LibraryGame& local, const CatalogEntry& remote) const override
    {
        if (!m_entryLayoutOk)
            return false;
        return m_target->updateMayBreakDlc(local, remote);
    }

    /** Revision 1 has no launch options; the host falls back to its own defaults. */
    QVector<GameLaunchOption> launchOptions(const LibraryGame& local) const override
    {
        (void)local;
        return {};
    }

private:
    ISourcePluginRev1* m_target = nullptr;
    bool m_entryLayoutOk = false;
};

} // namespace arachnel::core
