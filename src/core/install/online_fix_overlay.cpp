#include "online_fix_overlay.h"

#include "file_utils.h"
#include "steam_shortcut_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace arachnel::core {
namespace {

constexpr auto kDisabledSuffix = ".arachnel-off";

const QStringList& overlayDllNames()
{
    // FreeTP SteamFix/EpicFix, Ryuu OnlineFix64, and online-fix.me (OnlineFix.dll + overlay).
    static const QStringList names = {
        QStringLiteral("winmm.dll"),
        QStringLiteral("SteamFix64.dll"),
        QStringLiteral("SteamFix32.dll"),
        QStringLiteral("EpicFix64.dll"),
        QStringLiteral("OnlineFix64.dll"),
        QStringLiteral("OnlineFix32.dll"),
        QStringLiteral("OnlineFix.dll"),
        QStringLiteral("SteamOverlay32.dll"),
        QStringLiteral("SteamOverlay64.dll"),
        QStringLiteral("StubDRM32.dll"),
        QStringLiteral("StubDRM64.dll")};
    return names;
}

QString appendDllOverride(QString overrides, const QString& dllStem, const QString& mode)
{
    const QString key = dllStem.toLower();
    if (key.isEmpty() || overrides.contains(key + QLatin1Char('='), Qt::CaseInsensitive))
        return overrides;
    if (!overrides.isEmpty() && !overrides.endsWith(QLatin1Char(';')))
        overrides += QLatin1Char(';');
    return overrides + key + mode;
}

QString findSteamidraCmdStub()
{
    const QStringList roots = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
    };
    QSet<QString> seen;
    for (const QString& root : roots) {
        if (root.isEmpty() || seen.contains(root))
            continue;
        seen.insert(root);
        const QStringList candidates = {
            root + QStringLiteral("/plugins/steamidra/online_fix_kit/cmd_stub.exe"),
            root + QStringLiteral("/Arachnel/plugins/steamidra/online_fix_kit/cmd_stub.exe"),
        };
        for (const QString& path : candidates) {
            if (QFileInfo::exists(path))
                return path;
        }
    }
    return {};
}

void plantFreetpPromoBlock(const QString& dir)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return;

    QDir().mkpath(dir + QStringLiteral("/FreeTP/UserData"));
    const QString anon = dir + QStringLiteral("/FreeTP/UserData/AnonFolderSave.txt");
    if (!QFileInfo::exists(anon)) {
        QFile f(anon);
        if (f.open(QIODevice::WriteOnly))
            f.close();
    }

    const QString stubSrc = findSteamidraCmdStub();
    if (stubSrc.isEmpty())
        return;
    const QString stubDst = dir + QStringLiteral("/cmd.exe");
    const QFileInfo srcInfo(stubSrc);
    const QFileInfo dstInfo(stubDst);
    if (dstInfo.exists() && dstInfo.size() == srcInfo.size()
        && dstInfo.lastModified() >= srcInfo.lastModified())
        return;
    QFile::remove(stubDst);
    QFile::copy(stubSrc, stubDst);
}

QString readDllListOverrides(const QString& listPath)
{
    QFile file(listPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString overrides;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (!line.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            continue;
        const QString stem = QFileInfo(line).completeBaseName().toLower();
        overrides = appendDllOverride(overrides, stem, QStringLiteral("=n,b"));
    }
    return overrides;
}

QString readIniAppId(const QString& iniPath, const QString& key)
{
    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QRegularExpression re(QStringLiteral("%1\\s*=\\s*(\\d+)").arg(key),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(QString::fromUtf8(file.readAll()));
    return match.hasMatch() ? match.captured(1) : QString();
}

void ensureSteamFixWinmmTxt(const QString& dir, int bits)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    const QDir overlay(dir);
    const bool has32 = overlay.exists(QStringLiteral("SteamFix32.dll"));
    const bool has64 = overlay.exists(QStringLiteral("SteamFix64.dll"));
    if (!has32 && !has64)
        return;

    QString wanted;
    if (bits == 32 && has32)
        wanted = QStringLiteral("SteamFix32.dll");
    else if (bits == 64 && has64)
        wanted = QStringLiteral("SteamFix64.dll");
    else if (has32)
        wanted = QStringLiteral("SteamFix32.dll");
    else
        wanted = QStringLiteral("SteamFix64.dll");

    const QString path = overlay.filePath(QStringLiteral("winmm.txt"));
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString current = QString::fromUtf8(in.readAll()).trimmed();
        in.close();
        if (current.compare(wanted, Qt::CaseInsensitive) == 0)
            return;
    }
    writeTextFile(path, wanted + QLatin1Char('\n'));
}

QString buildOverlayWineDllOverrides(const QString& overlayDir)
{
    // SOFL default Online-Fix overrides (=n,b). Scanned game DLLs are merged on top.
    QString overrides = QStringLiteral(
        "d3d11=n;d3d10=n;d3d10core=n;dxgi=n;openvr_api_dxvk=n;d3d12=n;d3d12core=n;d3d9=n;d3d8=n;"
        "onlinefix64=n,b;onlinefix=n,b;steamoverlay64=n,b;steamoverlay32=n,b;"
        "winmm=n,b;dnet=n,b;steam_api64=n,b;steam_api=n,b;"
        "winhttp=n,b;steamfix64=n,b;steamfix32=n,b;epicfix64=n,b");

    const QDir dir(overlayDir);
    if (!dir.exists())
        return overrides;

    for (const QString& listName :
         {QStringLiteral("winmm.txt"), QStringLiteral("dlllist.txt")}) {
        const QString listPath = dir.filePath(listName);
        if (QFileInfo::exists(listPath)) {
            for (const QString& token :
                 readDllListOverrides(listPath).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QString stem = token.section(QLatin1Char('='), 0, 0).trimmed();
                const QString mode = QLatin1Char('=') + token.section(QLatin1Char('='), 1).trimmed();
                if (!stem.isEmpty())
                    overrides = appendDllOverride(overrides, stem, mode);
            }
        }
    }

    const QStringList dlls = dir.entryList({QStringLiteral("*.dll")}, QDir::Files);
    for (const QString& name : dlls) {
        const QString lower = name.toLower();
        const QString stem = QFileInfo(name).completeBaseName().toLower();
        if (lower.startsWith(QStringLiteral("win")) || lower.contains(QStringLiteral("fix"))
            || lower.contains(QStringLiteral("overlay")) || lower.startsWith(QStringLiteral("steam"))
            || lower.startsWith(QStringLiteral("online")) || lower.startsWith(QStringLiteral("epic"))
            || lower.startsWith(QStringLiteral("custom")) || lower.startsWith(QStringLiteral("dnet"))
            || lower.startsWith(QStringLiteral("emp")))
            overrides = appendDllOverride(overrides, stem, QStringLiteral("=n,b"));
    }
    return overrides;
}

#if defined(Q_OS_LINUX)
QString steamClientRoot()
{
    // Prefer a root that actually has Steam client libs (Debian/.steam often wins over
    // an empty ~/.local/share/Steam that only holds GE-Proton tools).
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
    };
    QString fallback;
    for (const QString& candidate : candidates) {
        if (!QDir(candidate).exists())
            continue;
        if (fallback.isEmpty())
            fallback = candidate;
        if (QFileInfo::exists(candidate + QStringLiteral("/ubuntu12_64/gameoverlayrenderer.so"))
            || QFileInfo::exists(candidate + QStringLiteral("/steam.sh"))
            || QFileInfo::exists(candidate + QStringLiteral("/ubuntu12_32/steam")))
            return QFileInfo(candidate).canonicalFilePath().isEmpty()
                       ? candidate
                       : QFileInfo(candidate).canonicalFilePath();
    }
    return fallback.isEmpty() ? home + QStringLiteral("/.local/share/Steam") : fallback;
}

QString gameOverlayPreloadPaths()
{
    // SOFL: LD_PRELOAD both 32/64 gameoverlayrenderer from the real Steam install.
    QStringList soPaths;
    const QString home = QDir::homePath();
    const QStringList roots = {
        steamClientRoot(),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/debian-installation"),
        home + QStringLiteral("/.local/share/Steam"),
    };
    for (const QString& root : roots) {
        if (root.isEmpty() || !QDir(root).exists())
            continue;
        const QString so32 = root + QStringLiteral("/ubuntu12_32/gameoverlayrenderer.so");
        const QString so64 = root + QStringLiteral("/ubuntu12_64/gameoverlayrenderer.so");
        if (QFileInfo::exists(so32) && !soPaths.contains(so32))
            soPaths.append(so32);
        if (QFileInfo::exists(so64) && !soPaths.contains(so64))
            soPaths.append(so64);
        if (soPaths.size() >= 2)
            break;
    }
    if (soPaths.isEmpty()) {
        const QString root = steamClientRoot();
        soPaths = {root + QStringLiteral("/ubuntu12_32/gameoverlayrenderer.so"),
                   root + QStringLiteral("/ubuntu12_64/gameoverlayrenderer.so")};
    }
    return soPaths.join(QLatin1Char(':'));
}

bool dirHasBundledSteamOverlayDll(const QString& dir)
{
    const QDir d(dir);
    return d.exists(QStringLiteral("SteamOverlay32.dll"))
        || d.exists(QStringLiteral("SteamOverlay64.dll"));
}

void appendSteamOverlayEnvironment(LaunchInfo* info, const QString& fakeSteamId,
                                   const QString& overlayDir, bool forceValveOverlay,
                                   bool unsetSteamAppId)
{
    const QString overlayId = fakeSteamId.isEmpty() ? QStringLiteral("480") : fakeSteamId;
    // Proton falls back to steam_appid.txt, leaving SteamFix to supply the real game ID.
    info->environmentExtras.insert(QStringLiteral("SteamAppId"),
                                   unsetSteamAppId ? QString() : overlayId);
    info->environmentExtras.insert(QStringLiteral("SteamGameId"), overlayId);
    info->environmentExtras.insert(QStringLiteral("SteamOverlayGameId"), overlayId);

    // By default stand Valve's overlay down for installs that ship their own overlay
    // DLL. Forcing it works only inside the Steam Linux Runtime (see
    // applyOnlineFixLaunchInfo); outside it the preload failed with error 126.
    if (!forceValveOverlay
        && (dirHasBundledSteamOverlayDll(overlayDir)
            || QDir(overlayDir).exists(QStringLiteral("OnlineFix.dll"))
            || QDir(overlayDir).exists(QStringLiteral("OnlineFix64.dll")))) {
        // Explicit clear so host/Steam LD_PRELOAD cannot leak into Proton.
        info->environmentExtras.insert(QStringLiteral("LD_PRELOAD"), QString());
        info->environmentExtras.insert(QStringLiteral("ENABLE_VK_LAYER_VALVE_steam_overlay_1"),
                                       QStringLiteral("0"));
        return;
    }

    const QString preload = gameOverlayPreloadPaths();
    const QString existing = info->environmentExtras.value(QStringLiteral("LD_PRELOAD"));
    info->environmentExtras.insert(QStringLiteral("LD_PRELOAD"),
                                   existing.isEmpty() ? preload : existing + QLatin1Char(':') + preload);
    // SOFL and Steam both set "1" here; a strict == "1" check rejects "true".
    info->environmentExtras.insert(QStringLiteral("ENABLE_VK_LAYER_VALVE_steam_overlay_1"),
                                   QStringLiteral("1"));
}
#endif

/**
 * Unsteam uses winmm.dll as its proxy too, so a winmm.dll sitting next to unsteam.dll /
 * unsteam.ini belongs to Unsteam, not to Online Fix. Without this the two layers read as
 * one: turning Unsteam on made Online Fix report itself enabled, and turning Online Fix
 * off would have renamed Unsteam's proxy out from under it.
 */
bool winmmBelongsToUnsteam(const QDir& dir)
{
    return dir.exists(QStringLiteral("unsteam.dll")) || dir.exists(QStringLiteral("unsteam.ini"))
        || dir.exists(QStringLiteral("unsteam.dll.arachnel-off"));
}

/**
 * True when Unsteam is the live layer in this directory. Only one layer runs at a time,
 * so Online Fix must not also report itself enabled here - and it otherwise would:
 * repacks ship SteamFix.ini/OnlineFix.ini next to steam_appid.txt, which is all the
 * fakeAppIdMode heuristic below needs, and those files never go away.
 */
bool unsteamIsLiveIn(const QDir& dir)
{
    return dir.exists(QStringLiteral("unsteam.dll")) && dir.exists(QStringLiteral("winmm.dll"));
}

/**
 * Add RealAppId to an OnlineFix/SteamFix ini that lacks it, leaving everything else in
 * the file alone. Without it the layer has no real id to hand the game.
 */
void ensureRealAppIdInIni(const QString& overlayDir, const QString& realAppId)
{
    if (overlayDir.isEmpty() || realAppId.isEmpty())
        return;
    for (const QString& name :
         {QStringLiteral("SteamFix.ini"), QStringLiteral("OnlineFix.ini")}) {
        const QString path = QDir(overlayDir).filePath(name);
        if (!QFileInfo::exists(path))
            continue;
        QSettings ini(path, QSettings::IniFormat);
        const QString existing = ini.value(QStringLiteral("Main/RealAppId")).toString().trimmed();
        if (existing == realAppId)
            continue;
        ini.setValue(QStringLiteral("Main/RealAppId"), realAppId);
        ini.sync();
    }
}

/** A live fix DLL (a winmm.dll that belongs to Unsteam does not count). */
bool dirHasActiveOverlayDll(const QDir& dir)
{
    for (const QString& name : overlayDllNames()) {
        if (name == QStringLiteral("winmm.dll") && winmmBelongsToUnsteam(dir))
            continue;
        if (dir.exists(name))
            return true;
    }
    return false;
}

bool dirHasActiveOverlay(const QDir& dir)
{
    return dirHasActiveOverlayDll(dir) || dir.exists(QStringLiteral("SteamFix.ini")) || dir.exists(QStringLiteral("OnlineFix.ini"))
        || dir.exists(QStringLiteral("winmm.txt")) || dir.exists(QStringLiteral("dlllist.txt"));
}

bool dirHasDisabledOverlay(const QDir& dir)
{
    for (const QString& name : overlayDllNames()) {
        if (dir.exists(name + QLatin1String(kDisabledSuffix)))
            return true;
    }
    return false;
}

bool dirLooksLikeOverlay(const QDir& dir)
{
    return dirHasActiveOverlay(dir) || dirHasDisabledOverlay(dir);
}

QJsonObject readOnlineFixMarker(const QString& installPath)
{
    QFile file(installPath + QStringLiteral("/.arachnel-steamidra"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("onlineFix")).toObject();
}

bool restoreValveSteamApiFiles(const QString& installPath)
{
    bool wrote = false;
    QDirIterator it(installPath,
                    {QStringLiteral("steam_api64.dll.arachnel-valve"),
                     QStringLiteral("steam_api.dll.arachnel-valve")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString bak = it.filePath();
        QString api = bak;
        api.chop(QStringLiteral(".arachnel-valve").size());
        if (QFileInfo::exists(api))
            QFile::remove(api);
        if (QFile::copy(bak, api))
            wrote = true;
    }
    return wrote;
}

bool shouldSkipOverlayScanDir(const QString& name)
{
    const QString lower = name.toLower();
    return lower == QLatin1String(".depotdownloader") || lower == QLatin1String("staging")
        || lower == QLatin1String("engine") || lower == QLatin1String("intermediate")
        || lower == QLatin1String("saved") || lower == QLatin1String("thirdparty");
}

void collectOverlayDir(const QDir& root, const QFileInfo& fi, QStringList& dirs)
{
    const QString rel = root.relativeFilePath(fi.absoluteFilePath());
    const int depth = rel.count(QLatin1Char('/')) + rel.count(QLatin1Char('\\'));
    if (depth > 6)
        return;
    // Skip known non-game trees by path segment.
    static const QRegularExpression pathSeparator(QStringLiteral("[/\\\\]"));
    const QStringList parts = rel.split(pathSeparator, Qt::SkipEmptyParts);
    bool skip = false;
    for (const QString& part : parts) {
        if (shouldSkipOverlayScanDir(part)) {
            skip = true;
            break;
        }
    }
    if (skip)
        return;
    if (fi.isDir()) {
        const QString name = fi.fileName();
        if ((name == QLatin1String("Win64") || name == QLatin1String("Win32"))
            && fi.dir().dirName() == QLatin1String("Binaries")) {
            const QDir shipping(fi.absoluteFilePath());
            if (dirLooksLikeOverlay(shipping)
                && !dirs.contains(shipping.absolutePath(), Qt::CaseInsensitive))
                dirs.append(shipping.absolutePath());
        }
        return;
    }
    const QString fileName = fi.fileName();
    const bool match = fileName == QLatin1String("winmm.dll")
        || fileName == QLatin1String("SteamFix64.dll")
        || fileName == QLatin1String("SteamFix32.dll")
        || fileName == QLatin1String("OnlineFix64.dll")
        || fileName == QLatin1String("OnlineFix.dll")
        || fileName == QLatin1String("OnlineFix32.dll")
        || fileName == QLatin1String("SteamOverlay32.dll")
        || fileName == QLatin1String("SteamOverlay64.dll")
        || fileName == QLatin1String("StubDRM32.dll")
        || fileName == QLatin1String("SteamFix.ini")
        || fileName == QLatin1String("OnlineFix.ini")
        || fileName == QLatin1String("dlllist.txt")
        || fileName == QLatin1String("winmm.txt")
        || fileName.endsWith(QLatin1String(kDisabledSuffix));
    if (!match)
        return;
    const QDir parent = fi.dir();
    if (dirLooksLikeOverlay(parent)
        && !dirs.contains(parent.absolutePath(), Qt::CaseInsensitive))
        dirs.append(parent.absolutePath());
}

struct LayoutWalker {
    FixLayoutScan::Layout& layout;
    QDir root;
    int seen = 0;
    int settingsSeen = 0;

    explicit LayoutWalker(FixLayoutScan::Layout& result, const QString& path)
        : layout(result), root(path)
    {
        layout.path = path;
        if (!path.isEmpty() && dirLooksLikeOverlay(root))
            layout.fixDirs.append(root.absolutePath());
    }

    bool done() const
    {
        return seen >= 4000 && (!layout.fixDirs.isEmpty()
            || (layout.valveBackup && (layout.steamSettings || settingsSeen >= 2000)));
    }

    void visit(const QFileInfo& fi)
    {
        if (seen < 4000 && !fi.isSymLink()) {
            ++seen;
            collectOverlayDir(root, fi, layout.fixDirs);
        }
        // Legacy name-filtered searches include symlinks and have independent limits.
        if (!layout.fixDirs.isEmpty())
            return;
        const QString name = fi.fileName();
        if (!layout.valveBackup
            && (name.compare(QLatin1String("steam_api64.dll.arachnel-valve"), Qt::CaseInsensitive) == 0
                || name.compare(QLatin1String("steam_api.dll.arachnel-valve"), Qt::CaseInsensitive) == 0)
            && fi.isFile())
            layout.valveBackup = true;
        if (!layout.steamSettings && settingsSeen < 2000
            && name.compare(QLatin1String("steam_settings"), Qt::CaseInsensitive) == 0 && fi.isDir()) {
            ++settingsSeen;
            const QDir settings(fi.absoluteFilePath());
            layout.steamSettings = settings.exists(QStringLiteral("DLC.txt"))
                || settings.exists(QStringLiteral("steam_interfaces.txt"));
        }
    }
};

bool renameOverlayInDir(const QDir& dir, bool enable, QString* error)
{
    bool touched = false;
    for (const QString& name : overlayDllNames()) {
        const QString active = dir.filePath(name);
        const QString disabled = active + QLatin1String(kDisabledSuffix);
        if (enable) {
            if (!QFileInfo::exists(disabled))
                continue;
            if (QFileInfo::exists(active))
                QFile::remove(active);
            if (!QFile::rename(disabled, active)) {
                if (error)
                    *error = QCoreApplication::translate("Core", "Failed to enable Online Fix: %1")
                                 .arg(active);
                return false;
            }
            touched = true;
        } else {
            if (!QFileInfo::exists(active))
                continue;
            if (QFileInfo::exists(disabled))
                QFile::remove(disabled);
            if (!QFile::rename(active, disabled)) {
                if (error)
                    *error = QCoreApplication::translate("Core", "Failed to disable Online Fix: %1")
                                 .arg(active);
                return false;
            }
            touched = true;
        }
    }
    // steam_appid.txt FakeAppId fallback (32-bit / no matching winmm).
    const QString appIdFile = dir.filePath(QStringLiteral("steam_appid.txt"));
    const QString appIdOff = appIdFile + QLatin1String(kDisabledSuffix);
    if (enable) {
        if (QFileInfo::exists(appIdOff)) {
            if (QFileInfo::exists(appIdFile))
                QFile::remove(appIdFile);
            QFile::rename(appIdOff, appIdFile);
            touched = true;
        }
    } else if (QFileInfo::exists(appIdFile)) {
        if (QFileInfo::exists(appIdOff))
            QFile::remove(appIdOff);
        QFile::rename(appIdFile, appIdOff);
        touched = true;
    }
    Q_UNUSED(touched);
    return true;
}

void updateMarkerEnabled(const QString& installPath, bool enabled)
{
    const QString markerPath = installPath + QStringLiteral("/.arachnel-steamidra");
    QJsonObject root;
    QFile file(markerPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }
    QJsonObject onlineFix = root.value(QStringLiteral("onlineFix")).toObject();
    onlineFix.insert(QStringLiteral("enabled"), enabled);
    onlineFix.insert(QStringLiteral("embedded"), true);
    // Drop stale "goldberg" from older builds - SteamFix/winmm is the only overlay now.
    onlineFix.remove(QStringLiteral("backend"));
    root.insert(QStringLiteral("onlineFix"), onlineFix);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace

bool isSteamClientRunning()
{
#if defined(Q_OS_WIN)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            const QString name = QString::fromWCharArray(entry.szExeFile);
            if (name.compare(QStringLiteral("steam.exe"), Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
#elif defined(Q_OS_LINUX)
    // Same exact-name match as `pgrep -x steam`, read straight from /proc: this runs
    // on the GUI thread during launch, and spawning pidof/pgrep with a 3s wait each
    // cost up to 6s there.
    QDirIterator it(QStringLiteral("/proc"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString pidDir = it.next();
        if (!it.fileName().front().isDigit())
            continue;
        QFile comm(pidDir + QStringLiteral("/comm"));
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "steam")
            return true;
    }
    return false;
#else
    QProcess process;
    process.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("steam")});
    return process.waitForFinished(3000) && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
#endif
}

bool tryStartSteamClient()
{
    // A launch reaches this from both applyOnlineFixLaunchInfo() and the launch
    // controller within the same second. A just-started client is still `bash
    // steam.sh` bootstrapping, invisible to isSteamClientRunning()'s comm == "steam"
    // match, so the second call used to start another client; the two fought over
    // the single-instance lock and one's steamwebhelper crash-looped every 10s.
    static qint64 lastStartMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (lastStartMs != 0 && nowMs - lastStartMs < 120 * 1000)
        return true;

#if defined(Q_OS_WIN)
    QStringList candidates;
    const QString install = findSteamInstallPath();
    if (!install.isEmpty())
        candidates.append(install + QStringLiteral("/steam.exe"));
    candidates.append({
        QStringLiteral("C:/Program Files (x86)/Steam/steam.exe"),
        QStringLiteral("C:/Program Files/Steam/steam.exe"),
    });
    for (const QString& path : candidates) {
        if (!QFileInfo::exists(path))
            continue;
        qint64 pid = 0;
        if (QProcess::startDetached(path, {}, QFileInfo(path).absolutePath(), &pid)) {
            lastStartMs = nowMs;
            return true;
        }
    }
    return false;
#else
    const QStringList candidates = {
        QStringLiteral("steam"),
        QDir::homePath() + QStringLiteral("/.local/share/Steam/steam.sh"),
        QDir::homePath() + QStringLiteral("/.steam/steam/steam.sh"),
        QStringLiteral("/usr/bin/steam"),
    };
    for (const QString& cmd : candidates) {
        if (cmd != QStringLiteral("steam") && !QFileInfo::exists(cmd))
            continue;
        // Detach with a clean env so host Steam/Nix sessions do not poison
        // /usr/bin/env inside steam.sh via LD_LIBRARY_PATH from steam-runtime.
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("LD_LIBRARY_PATH"));
        env.remove(QStringLiteral("LD_PRELOAD"));
        env.remove(QStringLiteral("STEAM_RUNTIME"));
        env.remove(QStringLiteral("STEAM_RUNTIME_LIBRARY_PATH"));
        process.setProcessEnvironment(env);
        process.setProgram(cmd);
        process.setArguments({});
        qint64 pid = 0;
        if (process.startDetached(&pid)) {
            lastStartMs = nowMs;
            return true;
        }
    }
    return false;
#endif
}

FixLayoutScan scanFixLayout(const QString& installPath)
{
    // One walk of the install feeds every check that used to walk it on its own
    // (overlay dirs, Valve steam_api backups, steam_settings). Same limits as before:
    // overlay discovery stops counting at 4000 entries; the legacy name searches keep
    // their own budgets and only matter while no overlay dir has been found.
    FixLayoutScan scan;
    scan.install.path = installPath;
    if (installPath.isEmpty() || !QFileInfo::exists(installPath))
        return scan;
    LayoutWalker install(scan.install, installPath);
    QDirIterator it(installPath, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (!install.done() && it.hasNext()) {
        it.next();
        install.visit(it.fileInfo());
    }
    return scan;
}

static OnlineFixOverlayState detectLayout(const FixLayoutScan::Layout& layout)
{
    const QString& installPath = layout.path;
    OnlineFixOverlayState state;
    if (installPath.isEmpty() || !QFileInfo::exists(installPath))
        return state;

    // Prefer SteamFix / winmm overlay dirs over older valve-backup heuristics.
    const QStringList& dirs = layout.fixDirs;
    if (!dirs.isEmpty()) {
        state.present = true;
        state.overlayDir = dirs.first();
        for (const QString& path : dirs) {
            const QDir dir(path);
            const bool hasActiveDll = dirHasActiveOverlayDll(dir);
            const bool hasDisabled = dirHasDisabledOverlay(dir);
            // Default on: live DLLs, or FakeAppId mode (ini/txt + steam_appid) when nothing was
            // renamed off. Disable renames both DLLs and steam_appid.txt.
            const bool fakeAppIdMode = dir.exists(QStringLiteral("steam_appid.txt"))
                && (dir.exists(QStringLiteral("SteamFix.ini"))
                    || dir.exists(QStringLiteral("OnlineFix.ini"))
                    || dir.exists(QStringLiteral("winmm.txt")));
            if ((hasActiveDll || (fakeAppIdMode && !hasDisabled)) && !unsteamIsLiveIn(dir)) {
                state.enabled = true;
                state.overlayDir = path;
                break;
            }
        }
        if (!state.enabled) {
            for (const QString& path : dirs) {
                if (dirHasDisabledOverlay(QDir(path))) {
                    state.overlayDir = path;
                    break;
                }
            }
        }
        return state;
    }

    // Old embeds: valve steam_api backups and/or marker only.
    const QJsonObject of = readOnlineFixMarker(installPath);
    if (layout.valveBackup || of.value(QStringLiteral("embedded")).toBool(false)
        || of.contains(QStringLiteral("enabled"))) {
        state.present = true;
        state.overlayDir = installPath;
        if (of.contains(QStringLiteral("enabled")))
            state.enabled = of.value(QStringLiteral("enabled")).toBool(false);
        else
            state.enabled = layout.steamSettings && layout.valveBackup;
    }
    return state;
}

OnlineFixOverlayState detectOnlineFixOverlay(const FixLayoutScan& scan)
{
    return detectLayout(scan.install);
}

OnlineFixOverlayState detectOnlineFixOverlay(const QString& installPath)
{
    return detectOnlineFixOverlay(scanFixLayout(installPath));
}

bool setOnlineFixOverlayEnabled(const QString& installPath, bool enabled, QString* error)
{
    if (installPath.isEmpty() || !QFileInfo::exists(installPath)) {
        if (error)
            *error = QCoreApplication::translate("Core", "Online Fix overlay not found in this install");
        return false;
    }

    const QStringList dirs = scanFixLayout(installPath).install.fixDirs;
    if (!dirs.isEmpty()) {
        for (const QString& path : dirs) {
            if (!renameOverlayInDir(QDir(path), enabled, error))
                return false;
        }
        updateMarkerEnabled(installPath, enabled);
        return true;
    }

    // No SteamFix overlay yet - marker toggle; restore Valve DLLs when disabling old embeds.
    if (!enabled)
        restoreValveSteamApiFiles(installPath);
    updateMarkerEnabled(installPath, enabled);
    return true;
}

namespace {

/** The install ships its own overlay DLL, so Valve's overlay is stood down unless forced. */
bool installShipsOwnOverlay(const QString& dir)
{
    if (dir.isEmpty())
        return false;
    const QDir d(dir);
    return d.exists(QStringLiteral("SteamOverlay32.dll"))
           || d.exists(QStringLiteral("SteamOverlay64.dll"))
           || d.exists(QStringLiteral("OnlineFix.dll")) || d.exists(QStringLiteral("OnlineFix64.dll"));
}
}

QString fixLayerKind(const QString& overlayDir)
{
    if (overlayDir.isEmpty())
        return {};
    const QDir dir(overlayDir);
    if (unsteamIsLiveIn(dir))
        return {};
    if (dir.exists(QStringLiteral("SteamFix64.dll")) || dir.exists(QStringLiteral("SteamFix32.dll")))
        return QStringLiteral("steamfix");
    if (dir.exists(QStringLiteral("OnlineFix64.dll")) || dir.exists(QStringLiteral("OnlineFix.dll")))
        return QStringLiteral("onlinefix");
    return {};
}

static bool canConvertOnlineFix(const QDir& dir)
{
    // A parked SteamFix is still an existing installation.
    return fixLayerKind(dir.path()) == QStringLiteral("onlinefix")
        && !dir.exists(QStringLiteral("SteamFix64.dll.arachnel-off"))
        && !dir.exists(QStringLiteral("SteamFix32.dll.arachnel-off"));
}

static QStringList fixDirsWithExecutable(const FixLayoutScan& scan, const QString& executable)
{
    QStringList dirs = scan.install.fixDirs;
    if (!executable.isEmpty()) {
        const QString path = QFileInfo(executable).absolutePath();
        if (!dirs.contains(path))
            dirs.prepend(path);
    }
    return dirs;
}

bool onlineFixMissingLoader(const FixLayoutScan& scan, const QString& executable)
{
    bool found64 = false;
    for (const QString& path : fixDirsWithExecutable(scan, executable)) {
        const QDir dir(path);
        if (unsteamIsLiveIn(dir))
            continue;
        const bool has64 = dir.exists(QStringLiteral("OnlineFix64.dll"));
        if ((has64 || dir.exists(QStringLiteral("OnlineFix.dll"))) && dir.exists(QStringLiteral("winmm.dll")))
            return false;
        if (has64 && canConvertOnlineFix(dir))
            found64 = true;
    }
    return found64;
}

QString steamFixKitDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/kits/steamfix");
}

bool steamFixKitAvailable()
{
    const QDir kit(steamFixKitDir());
    return kit.exists(QStringLiteral("SteamFix64.dll")) && kit.exists(QStringLiteral("winmm.dll"));
}

bool convertOnlineFixToSteamFix(const QString& gameId, const FixLayoutScan& scan,
                                const QString& executablePath, const QString& realAppId,
                                QString* summaryOut, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const QString& installPath = scan.install.path;
    if (installPath.isEmpty() || !QDir(installPath).exists())
        return fail(QCoreApplication::translate("Core", "Install folder is missing"));
    if (!steamFixKitAvailable()) {
        return fail(QCoreApplication::translate("Core", "No SteamFix kit in %1")
                        .arg(steamFixKitDir()));
    }

    if (!executablePath.isEmpty() && QFileInfo::exists(executablePath)
        && peImageBits(executablePath) == 32) {
        return fail(QCoreApplication::translate(
            "Core", "The SteamFix kit is 64-bit and this game is 32-bit"));
    }

    QStringList dirs;
    QString appId = realAppId.trimmed();
    for (const QString& path : fixDirsWithExecutable(scan, executablePath)) {
        const QDir dir(path);
        if (!canConvertOnlineFix(dir))
            continue;
        const QString relative = QDir(installPath).relativeFilePath(dir.absolutePath());
        if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))
            || QDir::isAbsolutePath(relative))
            return fail(QCoreApplication::translate("Core", "Fix directory outside install: %1").arg(path));
        dirs.append(path);
        if (appId.isEmpty())
            appId = readIniAppId(dir.filePath(QStringLiteral("OnlineFix.ini")), QStringLiteral("RealAppId"));
    }
    if (dirs.isEmpty())
        return fail(QCoreApplication::translate("Core", "No active Online Fix to convert"));
    if (appId.isEmpty())
        return fail(QCoreApplication::translate("Core", "Unknown Steam app id for this game"));

    // Move - never delete - every Online Fix file, active or parked, out of the
    // install. Leaving them in a subfolder is not enough: the layout healer copies
    // missing fix files from any folder holding winmm.dll + dlllist.txt.
    // overlayDllNames() minus SteamFix/EpicFix (not Online Fix), plus Online Fix's own
    // ini and load list - derived, so a DLL added there is moved here too.
    static const QStringList kOnlineFixFiles = []() {
        QStringList files{QStringLiteral("OnlineFix.ini"), QStringLiteral("dlllist.txt"),
                          QStringLiteral("SteamFix.ini"), QStringLiteral("winmm.txt")};
        for (const QString& name : overlayDllNames()) {
            if (!name.startsWith(QStringLiteral("SteamFix")) && !name.startsWith(QStringLiteral("EpicFix")))
                files.append(name);
        }
        return files;
    }();
    const QString backups = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/backups");
    if (!QDir().mkpath(backups))
        return fail(QCoreApplication::translate("Core", "Could not create %1").arg(backups));
    QTemporaryDir backup(backups + QLatin1Char('/') + gameId + QStringLiteral("-onlinefix-XXXXXX"));
    if (!backup.isValid())
        return fail(backup.errorString());
    backup.setAutoRemove(false);
    QList<QPair<QString, QString>> moves;
    QStringList written;
    auto rollback = [&](QString why) {
        bool restored = true;
        for (auto it = written.crbegin(); it != written.crend(); ++it) {
            if (!QFile::remove(*it)) {
                restored = false;
                why += QCoreApplication::translate("Core", "\nCould not remove %1").arg(*it);
            }
        }
        for (auto it = moves.crbegin(); it != moves.crend(); ++it) {
            if (!QFile::rename(it->second, it->first)) {
                why += QCoreApplication::translate("Core", "\nCould not restore %1 from %2")
                           .arg(it->first, it->second);
                restored = false;
            }
        }
        if (restored)
            backup.remove();
        return fail(why);
    };

    QString journal = QStringLiteral("Undo in reverse order, from the last line up. Move a backup "
                                     "back only if it exists.\n\n");
    auto writeAtomic = [](const QString& path, const QString& text) {
        QSaveFile file(path);
        const QByteArray data = text.toUtf8();
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    };
    // Journal the next operation first so a crash between a rename and a manifest
    // update cannot leave an unlisted backup.
    const QString manifest = QDir(backup.path()).filePath(QStringLiteral("RESTORE.txt"));
    auto record = [&](const QString& line) {
        journal += line + QLatin1Char('\n');
        return writeAtomic(manifest, journal);
    };

    const QDir kit(steamFixKitDir());
    for (const QString& path : dirs) {
        const QDir src(path);
        const QString dst = QDir(backup.path()).filePath(QDir(installPath).relativeFilePath(path));
        if (!QDir().mkpath(dst))
            return rollback(QCoreApplication::translate("Core", "Could not create %1").arg(dst));
        QString fakeAppId = readIniAppId(src.filePath(QStringLiteral("OnlineFix.ini")), QStringLiteral("FakeAppId"));
        if (fakeAppId.isEmpty())
            fakeAppId = QStringLiteral("480");
        for (const QString& name : kOnlineFixFiles) {
            for (const QString& variant : {name, name + QLatin1String(kDisabledSuffix)}) {
                // Disabled Unsteam keeps its own proxy beside the active Online Fix.
                if (variant == QStringLiteral("winmm.dll.arachnel-off") && winmmBelongsToUnsteam(src))
                    continue;
                const QString from = src.filePath(variant);
                const QFileInfo file(from);
                if (!file.exists() && !file.isSymLink())
                    continue;
                const QString to = QDir(dst).filePath(variant);
                if (!record(to + QStringLiteral("  ->  ") + from))
                    return rollback(QCoreApplication::translate("Core", "Could not write %1").arg(manifest));
                if (!file.isFile() || !QFile::rename(from, to))
                    return rollback(QCoreApplication::translate("Core", "Could not back up %1").arg(from));
                moves.append({from, to});
            }
        }

        const QString ini = QStringLiteral("[Main]\nRealAppId=%1\nFakeAppId=%2\nBuildId=0\n\n"
                             "[Misc]\nOverlay=true\nFilterLobby=false\n\n"
                             "[Interfaces]\nApps=true\nFriends=true\nUser=true\nInventory=true\n"
                             "Stats=true\nStorage=true\nUtils=true\nWorkshop=false\n\n"
                             "[DLC]\n0=dlc\n")
                           .arg(appId, fakeAppId);
        for (const QString& name : {QStringLiteral("SteamFix64.dll"), QStringLiteral("winmm.dll"),
                                    QStringLiteral("winmm.txt"), QStringLiteral("SteamFix.ini")}) {
            const QString to = src.filePath(name);
            const QString original = QDir(dst).filePath(name);
            const QString undo = QFileInfo::exists(original)
                ? QStringLiteral("If %1 still exists, delete %2").arg(original, to)
                : QStringLiteral("Delete if present: ") + to;
            if (QFileInfo::exists(to) || QFileInfo(to).isSymLink())
                return rollback(QCoreApplication::translate("Core", "Could not prepare %1").arg(to));
            if (!record(undo))
                return rollback(QCoreApplication::translate("Core", "Could not write %1").arg(manifest));
            bool ok;
            if (name.endsWith(QStringLiteral(".dll")))
                ok = QFile::copy(kit.filePath(name), to);
            else
                ok = writeAtomic(to, name == QStringLiteral("winmm.txt") ? QStringLiteral("SteamFix64.dll\n") : ini);
            if (!ok)
                return rollback(QCoreApplication::translate("Core", "Could not install %1").arg(to));
            written.append(to);
        }
    }
    if (summaryOut)
        *summaryOut = QCoreApplication::translate("Core", "Switched to SteamFix in %1 folder(s). Backup: %2")
                          .arg(dirs.size()).arg(backup.path());
    return true;
}

bool steamOverlayForced(const QString& installPath)
{
    // Valve's shift+tab overlay is always on (Sprout has no switch for it any more).
    // Installs that ship their own overlay DLL need the Steam Linux Runtime for it -
    // a plain preload next to those failed with error 126 - so this answers "force the
    // runtime route". Everything else already gets the overlay under plain Proton.
    // The old per-game marker (.arachnel-steam-overlay) is no longer read.
    return !installPath.isEmpty() && installShipsOwnOverlay(detectOnlineFixOverlay(installPath).overlayDir);
}

bool setSteamOverlayForced(const QString& installPath, bool /*forced*/)
{
    // The overlay is always on; see steamOverlayForced().
    return !installPath.isEmpty();
}

QVariantMap onlineFixOverlayInfo(const QString& installPath)
{
    const FixLayoutScan scan = scanFixLayout(installPath);
    const OnlineFixOverlayState state = detectOnlineFixOverlay(scan);
    // Unsteam's winmm.dll makes its folder an overlay dir, so the bounded scan already
    // lists it; no need to walk the rest of the install looking for unsteam.dll.
    bool convertible = false;
    bool liveUnsteam = false;
    QString kind = fixLayerKind(state.overlayDir);
    for (const QString& path : scan.install.fixDirs) {
        const QDir dir(path);
        if (unsteamIsLiveIn(dir))
            liveUnsteam = true;
        if (kind.isEmpty())
            kind = fixLayerKind(path);
        if (canConvertOnlineFix(dir))
            convertible = true;
    }
    QString label;
    if (!state.present)
        label = QCoreApplication::translate("Core", "Not installed");
    else if (state.enabled)
        label = QCoreApplication::translate("Core", "Enabled");
    else
        label = QCoreApplication::translate("Core", "Disabled");

    return {
        {QStringLiteral("onlineFixPresent"), state.present},
        {QStringLiteral("onlineFixEnabled"), state.enabled},
        {QStringLiteral("onlineFixCanToggle"), state.present},
        {QStringLiteral("onlineFixLabel"), label},
        {QStringLiteral("onlineFixOverlayDir"), state.overlayDir},
        // "steamfix" or "onlinefix": which layer sits in the overlay dir, so the
        // settings sheet can say which one the game uses and offer the switch.
        {QStringLiteral("onlineFixKind"), kind},
        {QStringLiteral("canSwitchToSteamFix"),
         convertible && !liveUnsteam && steamFixKitAvailable()},
        {QStringLiteral("steamOverlayForced"), !installPath.isEmpty() && installShipsOwnOverlay(state.overlayDir)},
#if defined(Q_OS_LINUX)
        {QStringLiteral("steamOverlayCanToggle"), false},
#else
        {QStringLiteral("steamOverlayCanToggle"), false},
#endif
    };
}

int healOnlineFixLayoutForExecutable(const QString& installPath, const QString& executablePath)
{
    return healOnlineFixLayoutForExecutable(scanFixLayout(installPath), executablePath);
}

int healOnlineFixLayoutForExecutable(const FixLayoutScan& scan, const QString& executablePath)
{
    const QString& installPath = scan.install.path;
    if (installPath.isEmpty() || executablePath.isEmpty())
        return 0;
    const QString exeDir = QFileInfo(executablePath).absolutePath();
    if (exeDir.isEmpty() || !QFileInfo::exists(exeDir))
        return 0;

    // A SteamFix layer beside the executable is a different emulator on purpose - it is
    // what convertOnlineFixToSteamFix() leaves when Online Fix refused the game. Never
    // copy Online Fix back in next to it from some other folder that still has a set
    // (Cities: Skylines II keeps one under Launcher/ for Paradox's launcher).
    {
        const QDir dir(exeDir);
        if (dir.exists(QStringLiteral("SteamFix64.dll")) || dir.exists(QStringLiteral("SteamFix32.dll"))
            || dir.exists(QStringLiteral("SteamFix.ini")))
            return 0;
    }

    // Find the directory that actually holds the fix, and stop if it is already the
    // executable's own.
    QString sourceDir;
    for (const QString& dir : scan.install.fixDirs) {
        if (QFileInfo::exists(QDir(dir).filePath(QStringLiteral("dlllist.txt")))
            && QFileInfo::exists(QDir(dir).filePath(QStringLiteral("winmm.dll")))) {
            sourceDir = dir;
            break;
        }
    }
    if (sourceDir.isEmpty()
        || QFileInfo(sourceDir).absoluteFilePath() == QFileInfo(exeDir).absoluteFilePath())
        return 0;

    QStringList wanted{QStringLiteral("winmm.dll"), QStringLiteral("dlllist.txt"),
                       QStringLiteral("OnlineFix.ini"), QStringLiteral("SteamFix.ini")};
    QFile list(QDir(sourceDir).filePath(QStringLiteral("dlllist.txt")));
    if (list.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(list.readAll())
                                      .split(QRegularExpression(QStringLiteral("[\r\n]")),
                                             Qt::SkipEmptyParts);
        for (const QString& line : lines)
            wanted.append(line.trimmed());
        list.close();
    }

    int placed = 0;
    for (const QString& name : wanted) {
        if (name.isEmpty())
            continue;
        const QString from = QDir(sourceDir).filePath(name);
        if (!QFileInfo::exists(from))
            continue;
        const QString to = QDir(exeDir).filePath(name);
        // Never overwrite: a file already beside the executable may be a deliberately
        // different build of the layer, and replacing it with the copy from the install
        // root is how a working setup gets broken. Only fill in what is missing.
        if (QFileInfo::exists(to))
            continue;
        if (QFile::copy(from, to))
            ++placed;
    }
    return placed;
}

void applyOnlineFixLaunchInfo(const QString& installPath, LaunchInfo* info,
                              const QString& realAppId)
{
    if (!info || installPath.isEmpty())
        return;

    applyOnlineFixLaunchInfo(scanFixLayout(installPath), info, realAppId);
}

void applyOnlineFixLaunchInfo(const FixLayoutScan& scan, LaunchInfo* info, const QString& realAppId,
                              const QString& launchExecutable)
{
    const QString& installPath = scan.install.path;
    if (!info || installPath.isEmpty())
        return;

    const OnlineFixOverlayState installState = detectOnlineFixOverlay(scan);
    OnlineFixOverlayState state = installState;
    if (!state.enabled && !info->workingDirectory.isEmpty()
        && info->workingDirectory != installPath) {
        const OnlineFixOverlayState wd = detectOnlineFixOverlay(info->workingDirectory);
        if (wd.enabled)
            state = wd;
    }
    if (!state.enabled)
        return;

    QString overlayDir = state.overlayDir;
    if (overlayDir.isEmpty())
        overlayDir = info->workingDirectory.isEmpty() ? installPath : info->workingDirectory;

    // winmm.dll reads winmm.txt for the SteamFix DLL name. The kit default is
    // SteamFix64.dll; 32-bit games then fail with "failed to load SteamFix64.dll".
    const int bits = peImageBits(info->executable);
    ensureSteamFixWinmmTxt(overlayDir, bits);
    if (!info->workingDirectory.isEmpty())
        ensureSteamFixWinmmTxt(info->workingDirectory, bits);
    if (!info->executable.isEmpty())
        ensureSteamFixWinmmTxt(QFileInfo(info->executable).absolutePath(), bits);

    QString overrides = buildOverlayWineDllOverrides(overlayDir);
    if (overrides.isEmpty()) {
        overrides = QStringLiteral(
            "onlinefix64=n,b;onlinefix=n,b;steamoverlay64=n,b;steamoverlay32=n,b;"
            "winmm=n,b;dnet=n,b;steam_api64=n,b;steam_api=n,b;winhttp=n,b");
    }

    if (info->wineDllOverrides.trimmed().isEmpty()) {
        info->wineDllOverrides = overrides;
    } else {
        for (const QString& token : overrides.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString stem = token.section(QLatin1Char('='), 0, 0).trimmed();
            const QString mode = QLatin1Char('=') + token.section(QLatin1Char('='), 1).trimmed();
            if (!stem.isEmpty())
                info->wineDllOverrides = appendDllOverride(info->wineDllOverrides, stem, mode);
        }
    }

    const bool forceOverlay = installShipsOwnOverlay(installState.overlayDir);

    // Plain Proton is enough for Online Fix. The Valve overlay, when forced, needs the
    // modern Steam Linux Runtime ("1"): pressure-vessel copies gameoverlayrenderer.so
    // into the container and rewrites LD_PRELOAD with the ${LIB} token, and a preload
    // outside it failed with error 126. (Never the legacy steam-runtime/run.sh: it
    // exits after fork, which reads as the game quitting.)
    if (forceOverlay)
        info->environmentExtras.insert(QStringLiteral("ARACHNEL_USE_STEAM_RUNTIME"),
                                       QStringLiteral("1"));
    else
        info->environmentExtras.remove(QStringLiteral("ARACHNEL_USE_STEAM_RUNTIME"));

    // Do NOT set PROTON_ENABLE_WAYLAND here. SOFL's Games.ini carries
    // `nativeWayland=` (empty) and the live process shows PROTON_ENABLE_WAYLAND=
    // with no value - i.e. the native Wayland backend is OFF, and the game goes
    // through XWayland. Valve's gameoverlayrenderer hooks the X11/GL/Vulkan
    // presentation path, so turning the Wayland backend on is a plausible way to
    // lose the overlay. Setting it to "1" was a misreading of that config.

    // Let the fix layer do the translating rather than doing it from outside. A SteamFix
    // repack ships RealAppId alongside FakeAppId and presents the real id to the game
    // while Steam only ever sees the fake one; an OnlineFix repack ships FakeAppId alone,
    // so the game is told it is Spacewar and any title that checks its own id quits -
    // which Arachnel then misreads as "Online Fix quit right after launch" and disables
    // the whole layer. Writing the missing RealAppId in gives the same arrangement that
    // works on Windows, and keeps Steam showing Spacewar.
    // Every copy of the ini needs it, not just overlayDir: in a nested repack the layer
    // that loads reads the copy healOnlineFixLayoutForExecutable() put beside the
    // executable, which is never overwritten afterwards.
    if (const QString id = realAppId.trimmed(); !id.isEmpty()) {
        ensureRealAppIdInIni(overlayDir, id);
        for (const QString& dir : scan.install.fixDirs)
            ensureRealAppIdInIni(dir, id);
        if (!info->workingDirectory.isEmpty())
            ensureRealAppIdInIni(info->workingDirectory, id);
    }
    const QString& executable = launchExecutable.isEmpty() ? info->executable : launchExecutable;
    const QString exeDir = executable.isEmpty() ? QString() : QFileInfo(executable).absolutePath();
    const auto hasOnlineFixDll = [](const QDir& dir) {
        return dir.exists(QStringLiteral("OnlineFix.dll"))
            || dir.exists(QStringLiteral("OnlineFix32.dll"))
            || dir.exists(QStringLiteral("OnlineFix64.dll"));
    };

    // A repack can ship a different fix for its launcher and its game executable.
    QDir layerDir(overlayDir);
    if (!exeDir.isEmpty()) {
        const QDir exeLayer(exeDir);
        if (hasOnlineFixDll(exeLayer)
            || (!unsteamIsLiveIn(exeLayer)
                && !readIniAppId(exeLayer.filePath(QStringLiteral("SteamFix.ini")),
                                 QStringLiteral("RealAppId")).isEmpty())) {
            layerDir = exeLayer;
        }
    }
    const QString steamFixIni = layerDir.filePath(QStringLiteral("SteamFix.ini"));
    const bool onlineFix = hasOnlineFixDll(layerDir);
    // Repacks keep SteamFix.ini beside a live Unsteam (unsteam.dll + winmm.dll); that
    // folder is Unsteam's, and Unsteam needs SteamAppId like Online Fix does.
    const bool steamFix = !onlineFix && !unsteamIsLiveIn(layerDir)
        && !readIniAppId(steamFixIni, QStringLiteral("RealAppId")).isEmpty();
    const QString appIdIni = !onlineFix && QFileInfo::exists(steamFixIni)
        ? steamFixIni : layerDir.filePath(QStringLiteral("OnlineFix.ini"));
    QString fakeAppId = readIniAppId(appIdIni, QStringLiteral("FakeAppId"));
    if (fakeAppId.isEmpty())
        fakeAppId = QStringLiteral("480");

    const QString canonicalInstall = QFileInfo(installPath).canonicalFilePath();
    const auto insideInstall = [&canonicalInstall](const QString& path) {
        if (canonicalInstall.isEmpty() || path.isEmpty())
            return false;
        const QString relative = QDir(canonicalInstall).relativeFilePath(path);
        return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
            && !relative.startsWith(QStringLiteral("../"));
    };

    // An executable override or a symlink can point into another game's install.
    // Keep the fallback ID local, including for 32-bit games that cannot load the fix DLL.
    const auto ensureSteamAppIdFile = [&](const QString& dir) {
        if (dir.isEmpty())
            return;
        const QString parent = QFileInfo(dir).canonicalFilePath();
        if (!insideInstall(parent))
            return;
        const QString path = QDir(parent).filePath(QStringLiteral("steam_appid.txt"));
        const QFileInfo destination(path);
        const QString target = destination.exists() || destination.isSymLink()
            ? destination.canonicalFilePath() : path;
        if (insideInstall(target))
            writeTextFile(target, fakeAppId + QLatin1Char('\n'));
    };
    ensureSteamAppIdFile(overlayDir);
    if (!info->workingDirectory.isEmpty()
        && QFileInfo(info->workingDirectory).absoluteFilePath()
            != QFileInfo(overlayDir).absoluteFilePath())
        ensureSteamAppIdFile(info->workingDirectory);
    const QString appIdExeDir = steamFix ? exeDir
        : (info->executable.isEmpty() ? QString() : QFileInfo(info->executable).absolutePath());
    if (!appIdExeDir.isEmpty()
        && QFileInfo(appIdExeDir).absoluteFilePath() != QFileInfo(overlayDir).absoluteFilePath())
        ensureSteamAppIdFile(appIdExeDir);

    // Windows + Proton: block FreeTP promo browser (path-local .hash / noop cmd.exe).
    plantFreetpPromoBlock(overlayDir);
    if (!info->workingDirectory.isEmpty())
        plantFreetpPromoBlock(info->workingDirectory);
    if (!info->executable.isEmpty())
        plantFreetpPromoBlock(QFileInfo(info->executable).absolutePath());

    // Proton Steam client LoadLibrary("GameOverlayRenderer.dll") for Spacewar - many
    // prefixes only have the x64 renderer. Alias FreeTP SteamOverlay* to that name.
    // Do NOT plant the alias next to OF.me: an extra GameOverlayRenderer.dll trips
    // Self-protection (error 4). Valve overlay is already disabled for OF.me above.
    auto plantOverlayAlias = [](const QString& dir) {
        if (dir.isEmpty() || !QDir(dir).exists())
            return;
        const QString ov32 = dir + QStringLiteral("/SteamOverlay32.dll");
        const QString ov64 = dir + QStringLiteral("/SteamOverlay64.dll");
        auto copyAlias = [](const QString& src, const QString& dst) {
            if (!QFileInfo::exists(src))
                return;
            if (QFileInfo::exists(dst))
                QFile::remove(dst);
            QFile::copy(src, dst);
        };
        if (QFileInfo::exists(ov32))
            copyAlias(ov32, dir + QStringLiteral("/GameOverlayRenderer.dll"));
        if (QFileInfo::exists(ov64))
            copyAlias(ov64, dir + QStringLiteral("/GameOverlayRenderer64.dll"));
    };
    auto stripOfMeOverlayAlias = [](const QString& dir) {
        if (dir.isEmpty() || !QDir(dir).exists())
            return;
        QFile::remove(dir + QStringLiteral("/GameOverlayRenderer.dll"));
        QFile::remove(dir + QStringLiteral("/GameOverlayRenderer64.dll"));
    };
    const QDir ovDir(overlayDir);
    const bool onlineFixMe = ovDir.exists(QStringLiteral("OnlineFix.ini"))
                             || ovDir.exists(QStringLiteral("OnlineFix.dll"))
                             || ovDir.exists(QStringLiteral("OnlineFix64.dll"));
    // GameOverlayRenderer*.dll is a Windows-side alias for the repack's OWN
    // SteamOverlay DLL. It is unrelated to LD_PRELOADing Valve's Linux
    // gameoverlayrenderer.so, and SOFL plants no such alias. Forcing the Valve
    // overlay must not change this policy.
    auto applyAliasPolicy = [&](const QString& dir) {
        if (onlineFixMe)
            stripOfMeOverlayAlias(dir);
        else
            plantOverlayAlias(dir);
    };
    applyAliasPolicy(overlayDir);
    if (!info->workingDirectory.isEmpty())
        applyAliasPolicy(info->workingDirectory);
    if (!info->executable.isEmpty())
        applyAliasPolicy(QFileInfo(info->executable).absolutePath());

#if defined(Q_OS_LINUX)
    // Valve LD_PRELOAD only when the install does not already ship OF.me overlay
    // DLLs - unless this game is opted in to the overlay, SOFL-style.
    appendSteamOverlayEnvironment(info, fakeAppId, overlayDir, forceOverlay, steamFix);

    // SOFL refuses to launch at all without the Steam client ("STEAMNOTSTARTED"):
    // the preloaded overlay has nothing to attach to otherwise.
    if (forceOverlay && !isSteamClientRunning())
        tryStartSteamClient();
#endif
}

} // namespace arachnel::core
