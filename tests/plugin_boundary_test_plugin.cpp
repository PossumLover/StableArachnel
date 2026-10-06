// A source plugin that answers the way real ones do: with string literals, whose
// characters live in this library's image. The tests unload it and then use what the host
// kept from it.
#define ARACHNEL_PLUGIN_BUILD
#include "plugin_api.h"

#include <cstdlib>
#include <cstring>

using namespace arachnel::core;

namespace {

CatalogComponent literalAddon()
{
    CatalogComponent addon;
    addon.id = QStringLiteral("steam-481");
    addon.title = QStringLiteral("Soundtrack");
    addon.magnetUris = {QStringLiteral("magnet:?xt=urn:btih:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};
    addon.downloadUrl = QStringLiteral("https://cdn.example/soundtrack.zip");
    addon.referer = QStringLiteral("https://source.example/");
    addon.getfileUrl = QStringLiteral("https://source.example/getfile");
    addon.fileSize = QStringLiteral("120 MB");
    addon.uploadDate = QStringLiteral("2026-10-01");
    addon.coverUrl = QStringLiteral("https://cdn.example/soundtrack.jpg");
    addon.screenshotUrls = {QStringLiteral("https://cdn.example/soundtrack-1.jpg")};
    return addon;
}

CatalogEntry literalEntry()
{
    CatalogEntry entry;
    entry.id = QStringLiteral("steam-480");
    entry.title = QStringLiteral("Spacewar");
    entry.coverUrl = QStringLiteral("https://cdn.example/cover.jpg");
    entry.remoteCoverUrl = QStringLiteral("https://cdn.example/remote-cover.jpg");
    entry.sourceId = QStringLiteral("boundary-test");
    entry.sourcePageUrl = QStringLiteral("https://source.example/spacewar");
    entry.version = QStringLiteral("build 41");
    entry.sizeLabel = QStringLiteral("1.2 GB");
    entry.description = QStringLiteral("A game");
    entry.genres = QStringLiteral("Action");
    entry.steamAppId = QStringLiteral("480");
    entry.trailerUrl = QStringLiteral("https://cdn.example/trailer.mp4");
    entry.trailerThumbnailUrl = QStringLiteral("https://cdn.example/trailer.jpg");
    entry.screenshotUrls = {QStringLiteral("https://cdn.example/1.jpg")};
    entry.magnetUris = {QStringLiteral("magnet:?xt=urn:btih:ffffffffffffffffffffffffffffffffffffffff")};
    entry.uploadDate = QStringLiteral("2026-10-02");
    entry.parentEntryId = QStringLiteral("steam-1");
    entry.addons = {literalAddon()};
    entry.titleLower = QStringLiteral("spacewar");
    return entry;
}

class LiteralPlugin final : public ISourcePlugin
{
public:
    QString id() const override { return QStringLiteral("boundary-test"); }
    QString name() const override { return QStringLiteral("Boundary Test Source"); }
    QString description() const override { return QStringLiteral("Answers with string literals"); }
    QString version() const override { return QStringLiteral("1.2.3"); }
    QStringList capabilities() const override
    {
        return {QStringLiteral("owns_download"), QStringLiteral("launch_options")};
    }

    QVector<CatalogEntry> catalog() const override { return {literalEntry()}; }
    QVector<CatalogEntry> search(const QString&) const override { return {literalEntry()}; }
    std::optional<CatalogEntry> entryById(const QString&) const override { return literalEntry(); }

    InstallResult installFromDownload(const InstallContext&) const override { return result(); }
    InstallResult installAddonFromDownload(const AddonInstallContext&) const override
    {
        return result();
    }
    InstallAnalysis analyzeDownload(const InstallContext&) const override { return analysis(); }
    InstallAnalysis analyzeFileNames(const QStringList&) const override { return analysis(); }

    std::optional<QString> detectUpdate(const LibraryGame&, const CatalogEntry&) const override
    {
        return QStringLiteral("build 42");
    }

    LaunchInfo launchInfo(const LibraryGame&) const override
    {
        LaunchInfo info;
        info.executable = QStringLiteral("/games/spacewar/spacewar.exe");
        info.workingDirectory = QStringLiteral("/games/spacewar");
        info.arguments = {QStringLiteral("-windowed")};
        info.argumentsPrefix = {QStringLiteral("--prefix")};
        info.wineDllOverrides = QStringLiteral("steam_api64=n,b");
        info.environmentExtras.insert(QStringLiteral("SteamAppId"), QStringLiteral("480"));
        return info;
    }

    InstallResult startOwnedDownload(
        const InstallContext&,
        const std::function<void(const OwnedDownloadProgress&)>& onProgress) const override
    {
        OwnedDownloadProgress progress;
        progress.percent = 50;
        progress.detail = QStringLiteral("Downloading depots");
        progress.status = QStringLiteral("downloading");
        onProgress(progress);
        return result();
    }

    QVector<GameLaunchOption> launchOptions(const LibraryGame&) const override
    {
        GameLaunchOption option;
        option.id = QStringLiteral("dx11");
        option.title = QStringLiteral("DirectX 11");
        option.executable = QStringLiteral("/games/spacewar/spacewar.exe");
        option.workingDirectory = QStringLiteral("/games/spacewar");
        option.arguments = {QStringLiteral("-dx11")};
        option.type = QStringLiteral("default");
        option.isDefault = true;
        return {option};
    }

private:
    static InstallResult result()
    {
        InstallResult result;
        result.success = true;
        result.installPath = QStringLiteral("/games/spacewar");
        result.error = QStringLiteral("No error");
        return result;
    }

    static InstallAnalysis analysis()
    {
        InstallAnalysis analysis;
        analysis.methodId = QStringLiteral("portable");
        analysis.detail = QStringLiteral("Looks portable");
        analysis.confidence = 10;
        analysis.canInstall = true;
        return analysis;
    }
};

} // namespace

extern "C" {

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_api_version()
{
    return ARACHNEL_PLUGIN_API_VERSION;
}

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_catalog_entry_size()
{
    return static_cast<int>(sizeof(CatalogEntry));
}

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_interface_revision()
{
    return ARACHNEL_PLUGIN_INTERFACE_REVISION;
}

ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_abi_sizes(ArachnelAbiSizes* out)
{
    arachnel_fill_abi_sizes(out);
}

ARACHNEL_PLUGIN_EXPORT ISourcePlugin* arachnel_plugin_create(const char*)
{
    return new LiteralPlugin();
}

ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_destroy(ISourcePlugin* plugin)
{
    delete plugin;
}

ARACHNEL_PLUGIN_EXPORT int arachnel_plugin_catalog_json(ISourcePlugin*, char** out_utf8,
                                                       size_t* out_len)
{
    static const char json[] = R"({"entries":[]})";
    *out_utf8 = static_cast<char*>(std::malloc(sizeof(json)));
    std::memcpy(*out_utf8, json, sizeof(json));
    *out_len = sizeof(json) - 1;
    return 0;
}

ARACHNEL_PLUGIN_EXPORT void arachnel_plugin_catalog_json_free(char* p)
{
    std::free(p);
}

} // extern "C"
