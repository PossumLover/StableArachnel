#include "plugin_host.h"

#include "crash_log.h"
#include "catalog_disk_cache.h"
#include "catalog_types.h"
#include "file_utils.h"
#include "plugin_api.h"
#include "plugin_interface_rev1.h"
#include "plugin_catalog_json.h"
#include "plugin_urls.h"
#include "plugin_version.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>
#include <QStringList>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <dlfcn.h>
#endif

namespace arachnel::core {


#include "plugin_host_helpers.h"

/**
 * sizeof(CatalogEntry) in the SDK just before 86b028f - the last revision that
 * still had genreTokens + genreKeys. Measured, not guessed: that header compiles
 * to exactly 592 bytes against Qt 6 (two extra QStringList at 24 bytes each).
 * It is the marker for a revision-1 plugin that does not export its revision.
 */
constexpr int kCatalogEntrySizeRev1 = 592;

// Layout claim inside an abiToken, e.g. "api=4;entry=544". Returns 0 when the
// token carries no claim, which is the case for every plugin built before the
// fork started asking for one.
static int abiTokenEntrySize(const QString& abiToken)
{
    const QStringList parts =
        abiToken.split(QRegularExpression(QStringLiteral("[;,\\s]+")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.startsWith(QStringLiteral("entry="), Qt::CaseInsensitive))
            continue;
        bool ok = false;
        const int value = QStringView(trimmed).mid(6).toInt(&ok);
        if (ok && value > 0)
            return value;
    }
    return 0;
}

#if defined(Q_OS_LINUX)
static QStringList linuxMissingSharedLibs(const QString& libraryPath)
{
    QProcess proc;
    proc.setProgram(QStringLiteral("ldd"));
    proc.setArguments({libraryPath});
    proc.start();
    if (!proc.waitForStarted(3000))
        return {};
    if (!proc.waitForFinished(5000))
        return {};

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput())
                            + QLatin1Char('\n')
                            + QString::fromLocal8Bit(proc.readAllStandardError());
    QStringList missing;
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const int marker = line.indexOf(QStringLiteral("=> not found"));
        if (marker <= 0)
            continue;
        QString lib = line.left(marker).trimmed();
        const int tab = lib.indexOf(QLatin1Char('\t'));
        if (tab >= 0)
            lib = lib.left(tab).trimmed();
        const int space = lib.indexOf(QLatin1Char(' '));
        if (space >= 0)
            lib = lib.left(space).trimmed();
        if (!lib.isEmpty() && !missing.contains(lib))
            missing.append(lib);
    }
    return missing;
}
#endif

PluginHost::PluginHost(QObject* parent)
    : QObject(parent)
{
#if defined(Q_OS_WIN)
    prependWindowsPathDirectory(QCoreApplication::applicationDirPath());
#endif
}

PluginHost::~PluginHost()
{
    unloadAll();
}

void PluginHost::unloadAll()
{
    if (m_beforeUnload)
        m_beforeUnload(QString());

    const QStringList ids = m_plugins.keys();
    for (const QString& id : ids)
        unloadPlugin(id);
}

void PluginHost::unloadPlugin(const QString& pluginId)
{
    auto it = m_plugins.find(pluginId);
    if (it == m_plugins.end())
        return;

    LoadedPlugin* loaded = it.value();
    m_plugins.erase(it);
    if (!loaded)
        return;

    if (loaded->instance) {
        // Do not call destroyFn / plugin virtuals here. On Linux AppImage the host can
        // interpose CatalogEntry::~ into the plugin DSO; destroying a loaded catalog
        // (FreeTP reinstall) then segfaults inside arachnel_plugin_destroy.
        // Leak the instance and drop the DSO - same idea as plugin resetCatalogCache.
        loaded->instance = nullptr;
        loaded->rawInstance = nullptr;
    }
    // The shim is ours, holds no plugin state, and must go before the DSO does.
    delete loaded->ownedShim;
    loaded->ownedShim = nullptr;
    if (loaded->library.isLoaded()) {
        const QString path = loaded->library.fileName();
        if (!loaded->library.unload()) {
            logDiagnostic(QStringLiteral("Plugin unload failed for %1: %2")
                              .arg(path, loaded->library.errorString()));
        }
    }
    delete loaded;
}

void PluginHost::setBeforeUnloadHook(std::function<void(const QString&)> hook)
{
    m_beforeUnload = std::move(hook);
}

void PluginHost::shutdownPlugins()
{
    unloadAll();
}

QStringList PluginHost::pluginSearchRoots()
{
    QStringList roots;
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dataDir.isEmpty())
        roots << dataDir + QStringLiteral("/plugins");

#if defined(Q_OS_WIN)
    const QByteArray roaming = qgetenv("APPDATA");
    if (!roaming.isEmpty()) {
        const QString legacy =
            QString::fromLocal8Bit(roaming) + QStringLiteral("/Arachnel/plugins");
        if (!roots.contains(legacy, Qt::CaseInsensitive))
            roots << legacy;
    }
#endif

    const QString sidecar =
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
    if (!roots.contains(sidecar, Qt::CaseInsensitive))
        roots << sidecar;

    return roots;
}

void PluginHost::migratePluginTrees()
{
    const QString destRoot = writablePluginsDir();
    if (destRoot.isEmpty())
        return;

    QStringList sources;
#if defined(Q_OS_WIN)
    const QByteArray roaming = qgetenv("APPDATA");
    if (!roaming.isEmpty())
        sources << QString::fromLocal8Bit(roaming) + QStringLiteral("/Arachnel/plugins");
#endif
    sources << QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");

    for (const QString& sourceRoot : sources) {
        if (QDir::cleanPath(sourceRoot).compare(QDir::cleanPath(destRoot), Qt::CaseInsensitive) == 0)
            continue;
        QDir src(sourceRoot);
        if (!src.exists())
            continue;
        const QStringList ids = src.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& id : ids) {
            const QString from = src.absoluteFilePath(id);
            const QString to = destRoot + QLatin1Char('/') + id;
            if (QDir(to).exists())
                continue;
            if (!QFileInfo::exists(from + QStringLiteral("/plugin.json")))
                continue;
            QString error;
            copyPathRecursive(from, to, &error);
        }
    }
}

void PluginHost::scan()
{
    if (m_scanDepth > 0) {
        logDiagnostic(QStringLiteral("Plugin scan skipped (re-entrant)"));
        return;
    }
    ++m_scanDepth;

    migratePluginTrees();
    unloadAll();

    const QStringList roots = pluginSearchRoots();
    for (const QString& root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists())
            continue;

        // Finish delayed uninstalls from a previous session (DLL was still locked).
        const QStringList leftover =
            rootDir.entryList({QStringLiteral("*.deleted-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : leftover)
            removePathRecursive(rootDir.absoluteFilePath(name));

        const QStringList entries =
            rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& entry : entries) {
            if (entry.endsWith(QStringLiteral(".staging")) || entry.endsWith(QStringLiteral(".bak")))
                continue;
            if (entry.contains(QStringLiteral(".deleted-")))
                continue;
            const QString pluginDir = rootDir.absoluteFilePath(entry);
            loadPluginDir(pluginDir);
            if (QCoreApplication::instance()
                && QThread::currentThread() == QCoreApplication::instance()->thread()
                && QCoreApplication::instance()->thread()->loopLevel() > 0) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
            }
        }
    }

    --m_scanDepth;
    emit pluginsChanged();
}

bool PluginHost::loadPluginById(const QString& pluginId)
{
    if (pluginId.isEmpty() || m_plugins.contains(pluginId))
        return m_plugins.contains(pluginId);

    for (const QString& root : pluginSearchRoots()) {
        const QString pluginDir = root + QLatin1Char('/') + pluginId;
        if (!QFileInfo::exists(pluginDir + QStringLiteral("/plugin.json")))
            continue;
        if (loadPluginDir(pluginDir))
            return true;
    }
    return false;
}

QString PluginHost::resolveLibraryFile(const QString& pluginDir, const QString& libraryBase)
{
    const QStringList candidates = {
        platformLibraryName(libraryBase),
#if defined(Q_OS_WIN)
        QStringLiteral("lib") + libraryBase + QStringLiteral(".dll"),
#endif
    };
    for (const QString& fileName : candidates) {
        const QString candidate = pluginDir + QLatin1Char('/') + fileName;
        if (QFile::exists(candidate))
            return candidate;
    }
    return {};
}

bool PluginHost::loadPluginDir(const QString& dirPath)
{
    m_lastLoadRejectReason.clear();

    const QString manifestPath = dirPath + QStringLiteral("/plugin.json");
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QString id = manifest.value(QStringLiteral("id")).toString();
    const QString libraryBase = manifest.value(QStringLiteral("library")).toString();
    const int apiVersion = manifest.value(QStringLiteral("apiVersion")).toInt(1);
    const QString displayName = manifest.value(QStringLiteral("name")).toString(id);

    if (id.isEmpty() || libraryBase.isEmpty())
        return false;
    if (apiVersion < ARACHNEL_PLUGIN_API_VERSION_MIN || apiVersion > ARACHNEL_PLUGIN_API_VERSION) {
        setLoadRejectReason(QCoreApplication::translate(
            "Core",
            "%1 needs a different JamesGames plugin API (plugin=%2, this app supports %3-%4). "
            "Update JamesGames or install a matching plugin build.")
                                .arg(displayName)
                                .arg(apiVersion)
                                .arg(ARACHNEL_PLUGIN_API_VERSION_MIN)
                                .arg(ARACHNEL_PLUGIN_API_VERSION));
        logDiagnostic(QStringLiteral("Plugin rejected (apiVersion %1, allowed %2..%3): %4")
                          .arg(apiVersion)
                          .arg(ARACHNEL_PLUGIN_API_VERSION_MIN)
                          .arg(ARACHNEL_PLUGIN_API_VERSION)
                          .arg(dirPath));
        return false;
    }

    // "api=4" encodes an API generation but nothing about struct layout, so a
    // plugin built against a modified SDK still resolves as compatible. Honour a
    // layout claim when the token carries one ("api=4;entry=544"), so a bad build
    // is refused before its code is ever mapped. Tokens without it fall through to
    // the export-based check after load.
    const QString abiToken = manifest.value(QStringLiteral("abiToken")).toString();
    const int declaredEntrySize = abiTokenEntrySize(abiToken);
    if (declaredEntrySize > 0 && declaredEntrySize != static_cast<int>(sizeof(CatalogEntry))) {
        setLoadRejectReason(QCoreApplication::translate(
            "Core",
            "%1 declares a CatalogEntry of %2 bytes; this app uses %3. "
            "Install a plugin build made for this app version.")
                                .arg(displayName)
                                .arg(declaredEntrySize)
                                .arg(static_cast<int>(sizeof(CatalogEntry))));
        logDiagnostic(QStringLiteral(
                          "Plugin rejected (abiToken layout mismatch): %1 declared=%2 core=%3 "
                          "token=\"%4\" from %5")
                          .arg(id)
                          .arg(declaredEntrySize)
                          .arg(static_cast<int>(sizeof(CatalogEntry)))
                          .arg(abiToken, dirPath));
        return false;
    }

    const QString minArachnel = manifest.value(QStringLiteral("minArachnel")).toString();
    const QString maxArachnel = manifest.value(QStringLiteral("maxArachnel")).toString();
    const QString appVersion = QCoreApplication::applicationVersion();
    if (!appVersionInRange(appVersion, minArachnel, maxArachnel)) {
        const QString needMin =
            minArachnel.trimmed().isEmpty() ? QStringLiteral("0.0.0") : minArachnel.trimmed();
        if (!maxArachnel.trimmed().isEmpty()
            && compareAppVersions(appVersion, maxArachnel.trimmed()) > 0) {
            setLoadRejectReason(QCoreApplication::translate(
                                    "Core",
                                    "%1 only supports JamesGames up to %2 (you have %3). "
                                    "Install a newer plugin build from the store.")
                                    .arg(displayName, maxArachnel.trimmed(), appVersion));
        } else {
            setLoadRejectReason(QCoreApplication::translate(
                                    "Core",
                                    "%1 needs JamesGames %2 or newer (you have %3). Update the app.")
                                    .arg(displayName, needMin, appVersion));
        }
        logDiagnostic(QStringLiteral("Plugin rejected (Arachnel %1 not in [%2, %3]): %4")
                          .arg(appVersion, minArachnel, maxArachnel, dirPath));
        return false;
    }

    if (m_plugins.contains(id))
        return false;

    const QString libraryPath = resolveLibraryFile(dirPath, libraryBase);
    if (libraryPath.isEmpty())
        return false;

    auto* loaded = new LoadedPlugin();
    // Every rejection below tears down the same way. Past arachnel_plugin_create the
    // instance is deliberately not destroyed (see unloadPlugin: destroying a loaded
    // catalog can crash inside the DSO); only the library goes.
    auto abandon = [&loaded]() {
        loaded->library.unload();
        delete loaded;
        return false;
    };
    loaded->rootPath = dirPath;
    loaded->library.setFileName(libraryPath);

#if defined(Q_OS_WIN)
    ScopedAddDllDirectory dllDirectories(
        {QCoreApplication::applicationDirPath(), QFileInfo(libraryPath).absolutePath()});
#elif defined(Q_OS_LINUX)
    // Plugins are dlopened outside the main binary RPATH. glibc also ignores
    // LD_LIBRARY_PATH changes after process start, so preload absolute paths with
    // RTLD_GLOBAL before QLibrary::load.
    //
    // AppImage puts its (often older) OpenSSL on LD_LIBRARY_PATH. Host libcurl on
    // rolling distros needs newer OPENSSL_* and then fails to load. Prefer any
    // libcurl/ssl shipped beside the plugin (built against AppImage-era OpenSSL),
    // then Qt from the AppImage/install lib dir.
    {
        const QString pluginDir = QFileInfo(libraryPath).absolutePath();
        const QString appDir = QCoreApplication::applicationDirPath();
        QStringList qtDirs;
        const QString appLib = QDir::cleanPath(QDir(appDir).absoluteFilePath(QStringLiteral("../lib")));
        if (QDir(appLib).exists())
            qtDirs << appLib;
        const QByteArray appImageDir = qgetenv("APPDIR");
        if (!appImageDir.isEmpty()) {
            const QString appImageLib =
                QDir(QString::fromUtf8(appImageDir)).filePath(QStringLiteral("usr/lib"));
            if (QDir(appImageLib).exists() && !qtDirs.contains(appImageLib))
                qtDirs << appImageLib;
        }

        auto preload = [](const QString& path) {
            if (!QFileInfo::exists(path))
                return;
            // Keep handles for process lifetime; intentional leak.
            dlopen(QFile::encodeName(path).constData(), RTLD_NOW | RTLD_GLOBAL);
        };

        static const char* kPluginRuntime[] = {
            "libcrypto.so.3", "libssl.so.3", "libcurl.so.4", nullptr};
        for (int i = 0; kPluginRuntime[i]; ++i)
            preload(QDir(pluginDir).filePath(QString::fromUtf8(kPluginRuntime[i])));

        static const char* kQt[] = {"libQt6Core.so.6", "libQt6Network.so.6", nullptr};
        for (const QString& dir : qtDirs) {
            for (int i = 0; kQt[i]; ++i)
                preload(QDir(dir).filePath(QString::fromUtf8(kQt[i])));
        }
    }
#endif
    if (!loaded->library.load()) {
        g_lastPluginLoadError = loaded->library.errorString();
        setLoadRejectReason(QCoreApplication::translate("Core", "Could not load %1: %2")
                                .arg(displayName, g_lastPluginLoadError));
        logDiagnostic(QStringLiteral("Plugin library load failed for %1: %2")
                          .arg(libraryPath, g_lastPluginLoadError));
#if defined(Q_OS_LINUX)
        const QStringList missing = linuxMissingSharedLibs(libraryPath);
        if (!missing.isEmpty()) {
            logDiagnostic(QStringLiteral("Plugin missing runtime libs for %1: %2")
                              .arg(libraryPath, missing.join(QStringLiteral(", "))));
        }
#endif
        delete loaded;
        return false;
    }
    g_lastPluginLoadError.clear();

    auto resolvePluginFn = [&](const char* name) -> QFunctionPointer {
        QFunctionPointer symbol = loaded->library.resolve(name);
#if defined(Q_OS_WIN)
        // QLibrary already loaded the module - never LoadLibraryW again (leaks a ref and
        // blocks uninstall/replace while the DLL stays locked).
        if (!symbol) {
            const HMODULE module =
                GetModuleHandleW(reinterpret_cast<LPCWSTR>(libraryPath.utf16()));
            if (module)
                symbol = reinterpret_cast<QFunctionPointer>(GetProcAddress(module, name));
        }
#endif
        return symbol;
    };

    auto* apiVersionFn = reinterpret_cast<int (*)()>(resolvePluginFn("arachnel_plugin_api_version"));
    auto* catalogEntrySizeFn =
        reinterpret_cast<int (*)()>(resolvePluginFn("arachnel_plugin_catalog_entry_size"));
    auto* createFn = reinterpret_cast<ISourcePlugin* (*)(const char*)>(
        resolvePluginFn("arachnel_plugin_create"));
    auto* destroyFn = reinterpret_cast<void (*)(ISourcePlugin*)>(
        resolvePluginFn("arachnel_plugin_destroy"));
    auto* catalogJsonFn = reinterpret_cast<int (*)(ISourcePlugin*, char**, size_t*)>(
        resolvePluginFn("arachnel_plugin_catalog_json"));
    auto* catalogJsonFreeFn =
        reinterpret_cast<void (*)(char*)>(resolvePluginFn("arachnel_plugin_catalog_json_free"));
    auto* interfaceRevisionFn =
        reinterpret_cast<int (*)()>(resolvePluginFn("arachnel_plugin_interface_revision"));
    auto* abiSizesFn = reinterpret_cast<void (*)(ArachnelAbiSizes*)>(
        resolvePluginFn("arachnel_plugin_abi_sizes"));

    if (!apiVersionFn || !createFn || !destroyFn) {
        setLoadRejectReason(QCoreApplication::translate(
            "Core", "%1 is missing required plugin exports. Reinstall from the store.")
                                .arg(displayName));
        return abandon();
    }
    const int exportedApi = apiVersionFn();
    if (exportedApi < ARACHNEL_PLUGIN_API_VERSION_MIN
        || exportedApi > ARACHNEL_PLUGIN_API_VERSION) {
        setLoadRejectReason(QCoreApplication::translate(
            "Core",
            "%1 needs a different JamesGames plugin API (plugin=%2, this app supports %3-%4). "
            "Update JamesGames or install a matching plugin build.")
                                .arg(displayName)
                                .arg(exportedApi)
                                .arg(ARACHNEL_PLUGIN_API_VERSION_MIN)
                                .arg(ARACHNEL_PLUGIN_API_VERSION));
        return abandon();
    }

    const int coreEntrySize = static_cast<int>(sizeof(CatalogEntry));
    bool layoutTrusted = false;

    // API 4+: catalog crosses as JSON. sizeof still gates entryById / detectUpdate.
    if (exportedApi >= 4) {
        if (!catalogJsonFn || !catalogJsonFreeFn) {
            setLoadRejectReason(QCoreApplication::translate(
                "Core", "%1 is missing API 4 catalog JSON exports. Reinstall from the store.")
                                    .arg(displayName));
            logDiagnostic(QStringLiteral(
                              "Plugin rejected (API 4 requires catalog_json exports): %1 from %2")
                              .arg(id, libraryPath));
            return abandon();
        }
        loaded->catalogJsonFn = catalogJsonFn;
        loaded->catalogJsonFreeFn = catalogJsonFreeFn;

        if (catalogEntrySizeFn) {
            const int pluginEntrySize = catalogEntrySizeFn();
            logDiagnostic(
                QStringLiteral("Plugin %1 CatalogEntry size: plugin=%2 core=%3 (API %4)")
                    .arg(id)
                    .arg(pluginEntrySize)
                    .arg(coreEntrySize)
                    .arg(exportedApi));
            if (pluginEntrySize == coreEntrySize) {
                layoutTrusted = true;
            } else if (pluginEntrySize == kCatalogEntrySizeRev1) {
                // Built against the SDK immediately before 86b028f, where
                // CatalogEntry still carried genreTokens + genreKeys (2 QStringList
                // = 48 bytes). The host can still talk to such a plugin: under
                // API 4 the catalog crosses as JSON, and the revision shim below
                // puts the vtable slots back where this plugin has them. Only the
                // CatalogEntry-carrying calls stay disabled.
                logDiagnostic(
                    QStringLiteral("Plugin %1 uses the revision 1 CatalogEntry "
                                   "(plugin=%2 core=%3): JSON catalog only, "
                                   "entryById / detectUpdate stay disabled")
                        .arg(id)
                        .arg(pluginEntrySize)
                        .arg(coreEntrySize));
            } else {
                // Some other vintage - nothing known to shim against, and a
                // CatalogEntry of an unknown shape cannot be allowed to cross.
                setLoadRejectReason(QCoreApplication::translate(
                    "Core",
                    "%1 was built against an JamesGames SDK this app does not know: CatalogEntry "
                    "is %2 bytes in the plugin and %3 bytes here. Loading it would corrupt "
                    "memory, so it was not loaded. Rebuild the plugin against this app version.")
                                        .arg(displayName)
                                        .arg(pluginEntrySize)
                                        .arg(coreEntrySize));
                logDiagnostic(
                    QStringLiteral(
                        "Plugin rejected (unknown CatalogEntry size): %1 plugin=%2 core=%3 "
                        "(API %4) from %5 - rebuild the plugin against this SDK")
                        .arg(id)
                        .arg(pluginEntrySize)
                        .arg(coreEntrySize)
                        .arg(exportedApi)
                        .arg(libraryPath));
                return abandon();
            }
        } else {
            logDiagnostic(QStringLiteral(
                "Plugin %1: no catalog_entry_size export - JSON catalog only "
                "(skip entryById / detectUpdate across DLL)")
                              .arg(id));
        }
    } else if (catalogEntrySizeFn) {
        const int pluginEntrySize = catalogEntrySizeFn();
        logDiagnostic(QStringLiteral("Plugin %1 CatalogEntry size: plugin=%2 core=%3 (legacy API %4)")
                          .arg(id)
                          .arg(pluginEntrySize)
                          .arg(coreEntrySize)
                          .arg(exportedApi));
        if (pluginEntrySize != coreEntrySize) {
            setLoadRejectReason(QCoreApplication::translate(
                "Core",
                "%1 was built for a different JamesGames SDK (CatalogEntry %2 vs %3 bytes). "
                "Update JamesGames, or install a plugin build for this app version.")
                                    .arg(displayName)
                                    .arg(pluginEntrySize)
                                    .arg(coreEntrySize));
            logDiagnostic(QStringLiteral(
                              "Plugin rejected (CatalogEntry size mismatch): %1 plugin=%2 core=%3 "
                              "from %4 - rebuild with matching SDK or migrate to API v4")
                              .arg(id)
                              .arg(pluginEntrySize)
                              .arg(coreEntrySize)
                              .arg(libraryPath));
            return abandon();
        }
        layoutTrusted = true;
        logDiagnostic(QStringLiteral(
                          "Plugin %1 uses legacy CatalogEntry ABI (API %2); prefer API v4 JSON")
                          .arg(id)
                          .arg(exportedApi));
    } else {
        setLoadRejectReason(QCoreApplication::translate(
            "Core",
            "%1 is missing the CatalogEntry size check. Reinstall a current plugin build.")
                                .arg(displayName));
        logDiagnostic(QStringLiteral(
                          "Plugin rejected (catalog_entry_size missing): %1 from %2 (API %3)")
                          .arg(id, libraryPath)
                          .arg(exportedApi));
        return abandon();
    }

    loaded->instance = createFn(dirPath.toUtf8().constData());
    loaded->rawInstance = loaded->instance;
    loaded->destroyFn = destroyFn;
    loaded->catalogEntryLayoutTrusted = layoutTrusted;
    if (!loaded->instance) {
        setLoadRejectReason(QCoreApplication::translate("Core", "%1 failed to start.")
                                .arg(displayName));
        return abandon();
    }

    // --- vtable layout -------------------------------------------------------
    // apiVersion does not describe where the virtuals sit, and upstream moved one
    // without bumping anything (86b028f). Establish the revision explicitly.
    // The revision comes from, in order: the plugin's own export, the revision in its
    // ABI size table, and last an inference from the CatalogEntry vintage (the commit
    // that moved the virtual is the same one that shrank the struct).
    int sizesRevision = 0;
    if (abiSizesFn) {
        ArachnelAbiSizes pluginSizes{};
        abiSizesFn(&pluginSizes);
        ArachnelAbiSizes coreSizes{};
        arachnel_fill_abi_sizes(&coreSizes);

        // CatalogEntry is handled above (a known older layout is tolerated because
        // nothing carrying it is called). Every other struct is passed straight
        // across on install / launch, so any drift there is fatal.
        const struct {
            const char* name;
            unsigned int plugin;
            unsigned int core;
        } checks[] = {
            {"CatalogComponent", pluginSizes.catalogComponent, coreSizes.catalogComponent},
            {"LibraryGame", pluginSizes.libraryGame, coreSizes.libraryGame},
            {"InstallContext", pluginSizes.installContext, coreSizes.installContext},
            {"AddonInstallContext", pluginSizes.addonInstallContext, coreSizes.addonInstallContext},
            {"InstallResult", pluginSizes.installResult, coreSizes.installResult},
            {"InstallAnalysis", pluginSizes.installAnalysis, coreSizes.installAnalysis},
            {"LaunchInfo", pluginSizes.launchInfo, coreSizes.launchInfo},
            {"OwnedDownloadProgress", pluginSizes.ownedDownloadProgress,
             coreSizes.ownedDownloadProgress},
        };
        for (const auto& check : checks) {
            if (check.plugin == check.core)
                continue;
            setLoadRejectReason(
                QCoreApplication::translate(
                    "Core",
                    "%1 was built against a different JamesGames SDK: %2 is %3 bytes in the "
                    "plugin and %4 bytes here. Rebuild the plugin against this app version.")
                    .arg(displayName, QLatin1String(check.name))
                    .arg(check.plugin)
                    .arg(check.core));
            logDiagnostic(QStringLiteral("Plugin rejected (%1 size mismatch): %2 plugin=%3 core=%4")
                              .arg(QLatin1String(check.name), id)
                              .arg(check.plugin)
                              .arg(check.core));
            return abandon();
        }
        sizesRevision = static_cast<int>(pluginSizes.interfaceRevision);
    }

    int pluginRevision = interfaceRevisionFn ? interfaceRevisionFn() : 0;
    if (pluginRevision <= 0)
        pluginRevision = sizesRevision;
    if (pluginRevision <= 0) {
        pluginRevision = (catalogEntrySizeFn && catalogEntrySizeFn() == kCatalogEntrySizeRev1)
                             ? 1
                             : ARACHNEL_PLUGIN_INTERFACE_REVISION;
    }
    loaded->interfaceRevision = pluginRevision;

    if (loaded->interfaceRevision != ARACHNEL_PLUGIN_INTERFACE_REVISION) {
        if (loaded->interfaceRevision == 1) {
            // Route every call to the slot this plugin actually has.
            auto* shim = new SourcePluginRev1Adapter(
                reinterpret_cast<ISourcePluginRev1*>(loaded->rawInstance), layoutTrusted);
            loaded->ownedShim = shim;
            loaded->instance = shim;
            logDiagnostic(
                QStringLiteral("Plugin %1 uses interface revision 1 (this app is %2); "
                               "calls routed through the revision 1 shim")
                    .arg(id)
                    .arg(ARACHNEL_PLUGIN_INTERFACE_REVISION));
        } else {
            setLoadRejectReason(
                QCoreApplication::translate(
                    "Core",
                    "%1 was built against plugin interface revision %2; this app speaks %3 "
                    "and has no shim for that revision. Rebuild the plugin against this "
                    "app version.")
                    .arg(displayName)
                    .arg(loaded->interfaceRevision)
                    .arg(ARACHNEL_PLUGIN_INTERFACE_REVISION));
            logDiagnostic(QStringLiteral("Plugin rejected (interface revision %1, host %2): %3")
                              .arg(loaded->interfaceRevision)
                              .arg(ARACHNEL_PLUGIN_INTERFACE_REVISION)
                              .arg(id));
            return abandon();
        }
    }

    SourcePluginInfo info;
    info.id = loaded->instance->id();
    info.name = loaded->instance->name();
    info.description = loaded->instance->description();
    info.catalogUrl = manifest.value(QStringLiteral("catalogUrl")).toString();
    info.repositoryUrl = resolvePluginRepository(info.id, manifest);
    info.iconName = manifest.value(QStringLiteral("iconName")).toString(QStringLiteral("storefront"));
    info.enabled = true;
    info.isPlugin = true;
    // Prefer plugin.json version (CI bumps this); fall back to DLL if missing.
    const QString manifestVersion = manifest.value(QStringLiteral("version")).toString().trimmed();
    info.pluginVersion = !manifestVersion.isEmpty() ? manifestVersion : loaded->instance->version();
    info.pluginRootPath = dirPath;
    info.capabilities = loaded->instance->capabilities();
    info.apiVersion = exportedApi;
    loaded->info = info;
    loaded->apiVersion = exportedApi;

    m_plugins.insert(id, loaded);
    if (exportedApi >= 4) {
        logDiagnostic(QStringLiteral("Plugin loaded: %1 v%2 from %3 (API %4, JSON catalog%5)")
                          .arg(info.id, info.pluginVersion, libraryPath)
                          .arg(exportedApi)
                          .arg(layoutTrusted ? QStringLiteral(", CatalogEntry trusted")
                                             : QStringLiteral(", CatalogEntry untrusted")));
    } else {
        logDiagnostic(QStringLiteral("Plugin loaded: %1 v%2 from %3 (API %4, CatalogEntry=%5 bytes)")
                          .arg(info.id, info.pluginVersion, libraryPath)
                          .arg(exportedApi)
                          .arg(sizeof(CatalogEntry)));
    }
    return true;
}

void PluginHost::setLoadRejectReason(const QString& reason)
{
    m_lastLoadRejectReason = reason;
    if (!reason.isEmpty())
        g_lastPluginLoadError = reason;
}

QByteArray PluginHost::loadPluginCatalogPayload(const QString& id, QByteArray* payloadSha) const
{
    if (payloadSha)
        payloadSha->clear();
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.constEnd() || !it.value() || !it.value()->instance)
        return {};

    LoadedPlugin* loaded = it.value();
    QByteArray bytes;
    if (loaded->apiVersion >= 4 && loaded->catalogJsonFn && loaded->catalogJsonFreeFn) {
        char* buf = nullptr;
        size_t len = 0;
        const int rc = loaded->catalogJsonFn(loaded->rawInstance, &buf, &len);
        if (rc != 0 || !buf) {
            if (buf)
                loaded->catalogJsonFreeFn(buf);
            logDiagnostic(QStringLiteral("Plugin %1 catalog_json failed (rc=%2)").arg(id).arg(rc));
            return {};
        }
        bytes = QByteArray(buf, static_cast<int>(len));
        loaded->catalogJsonFreeFn(buf);
    } else {
        bytes = serializePluginCatalogJson(loaded->instance->catalog());
    }
    if (bytes.isEmpty())
        return {};
    CatalogDiskCache::savePayload(id, bytes, {});
    if (payloadSha)
        *payloadSha = CatalogDiskCache::payloadSha256(bytes);
    return bytes;
}

QVector<CatalogEntry> PluginHost::loadPluginCatalog(const QString& id) const
{
    const QByteArray bytes = loadPluginCatalogPayload(id);
    if (bytes.isEmpty())
        return {};
    return parsePluginCatalogJson(bytes, id);
}

QVector<SourcePluginInfo> PluginHost::pluginInfos() const
{
    QVector<SourcePluginInfo> infos;
    infos.reserve(m_plugins.size());
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it.value())
            infos.append(it.value()->info);
    }
    return infos;
}

ISourcePlugin* PluginHost::plugin(const QString& id) const
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.constEnd() || !it.value())
        return nullptr;
    return it.value()->instance;
}

bool PluginHost::hasPlugin(const QString& id) const
{
    return m_plugins.contains(id);
}

bool PluginHost::hasPluginFilesOnDisk(const QString& id) const
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (const QString& root : pluginSearchRoots()) {
        const QString manifest =
            QDir(root).absoluteFilePath(trimmed + QStringLiteral("/plugin.json"));
        if (QFileInfo::exists(manifest))
            return true;
    }
    return false;
}

QString PluginHost::pluginVersionOnDisk(const QString& id) const
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return {};

    for (const QString& root : pluginSearchRoots()) {
        const QString manifest =
            QDir(root).absoluteFilePath(trimmed + QStringLiteral("/plugin.json"));
        QFile file(manifest);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        const QString version = obj.value(QStringLiteral("version")).toString().trimmed();
        if (!version.isEmpty())
            return version;
    }
    return {};
}

QVector<SourcePluginInfo> PluginHost::diskPluginInfos() const
{
    QVector<SourcePluginInfo> infos;
    QSet<QString> seen;

    for (const QString& root : pluginSearchRoots()) {
        QDir rootDir(root);
        if (!rootDir.exists())
            continue;
        const QStringList entries =
            rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& entry : entries) {
            if (entry.endsWith(QStringLiteral(".staging")) || entry.endsWith(QStringLiteral(".bak")))
                continue;
            const QString pluginDir = rootDir.absoluteFilePath(entry);
            QFile file(pluginDir + QStringLiteral("/plugin.json"));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen.insert(id);

            SourcePluginInfo info;
            info.id = id;
            info.name = obj.value(QStringLiteral("name")).toString(id);
            info.description = obj.value(QStringLiteral("description")).toString();
            info.pluginVersion = obj.value(QStringLiteral("version")).toString();
            info.pluginRootPath = pluginDir;
            info.iconName = obj.value(QStringLiteral("iconName")).toString(QStringLiteral("extension"));
            info.catalogUrl = obj.value(QStringLiteral("catalogUrl")).toString();
            info.repositoryUrl = resolvePluginRepository(id, obj);
            info.isPlugin = true;
            info.enabled = false;
            infos.append(info);
        }
    }
    return infos;
}

QStringList PluginHost::pluginIds() const
{
    return m_plugins.keys();
}

} // namespace arachnel::core
