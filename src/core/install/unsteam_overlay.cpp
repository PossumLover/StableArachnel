#include "unsteam_overlay.h"

#include "file_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#if defined(Q_OS_LINUX)
#include <signal.h>
#endif

namespace arachnel::core {

namespace {

// Same marker Online Fix uses, so a game carrying both layers reads consistently.
constexpr auto kDisabledSuffix = ".arachnel-off";
constexpr auto kProxyName = "winmm.dll";
constexpr auto kPayloadName = "unsteam.dll";
constexpr auto kIniName = "unsteam.ini";

QString g_payloadDirOverride;

QString appDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool dirHasPayload(const QString& dir)
{
    if (dir.isEmpty())
        return false;
    const QDir root(dir);
    return QFileInfo::exists(root.filePath(QStringLiteral("x64/unsteam64.dll")))
        && QFileInfo::exists(root.filePath(QStringLiteral("x64/winmm64.dll")))
        && QFileInfo::exists(root.filePath(QStringLiteral("x86/unsteam.dll")))
        && QFileInfo::exists(root.filePath(QStringLiteral("x86/winmm.dll")));
}

/** The directory the overlay belongs in: beside the executable that loads it. */
QString overlayDirFor(const QString& installPath, const QString& executablePath)
{
    if (!executablePath.isEmpty()) {
        const QString exeDir = QFileInfo(executablePath).absolutePath();
        if (!exeDir.isEmpty() && QFileInfo::exists(exeDir))
            return exeDir;
    }
    return installPath;
}

/** Candidate directories an overlay may already live in, nearest first. */
QStringList candidateDirs(const QString& installPath)
{
    QStringList dirs;
    if (installPath.isEmpty() || !QFileInfo::exists(installPath))
        return dirs;
    dirs.append(installPath);

    // Repacks often nest the real game one level down ("<install>/How to Fish/").
    const QDir root(installPath);
    const QFileInfoList subs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& sub : subs) {
        if (QFileInfo::exists(sub.absoluteFilePath() + QLatin1Char('/') + QLatin1String(kIniName)))
            dirs.append(sub.absoluteFilePath());
    }
    return dirs;
}

bool dirHasUnsteam(const QDir& dir, bool* activeOut)
{
    const bool activeProxy = dir.exists(QLatin1String(kProxyName))
                             && dir.exists(QLatin1String(kPayloadName));
    const bool disabledProxy = dir.exists(QLatin1String(kProxyName) + QLatin1String(kDisabledSuffix))
                               || dir.exists(QLatin1String(kPayloadName)
                                             + QLatin1String(kDisabledSuffix));
    if (activeOut)
        *activeOut = activeProxy;
    return activeProxy || disabledProxy || dir.exists(QLatin1String(kIniName));
}

bool renameIfPresent(const QDir& dir, const QString& from, const QString& to)
{
    if (!dir.exists(from))
        return false;
    if (dir.exists(to))
        QFile::remove(dir.filePath(to));
    return QFile::rename(dir.filePath(from), dir.filePath(to));
}

} // namespace

int stopStraySteamShims()
{
#if !defined(Q_OS_LINUX)
    return 0;
#else
    // Wine reports the stub's command line as its Windows path, which is specific enough
    // to match on: nothing else in the system runs C:\arachnel\steam.exe.
    static const QByteArray marker = QByteArrayLiteral("arachnel\\steam.exe");
    int stopped = 0;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid || pid <= 0 || pid == QCoreApplication::applicationPid())
            continue;
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(entry));
        if (!cmdline.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = cmdline.readAll();
        cmdline.close();
        if (!line.contains(marker))
            continue;
        if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0)
            ++stopped;
    }
    return stopped;
#endif
}

QString steamShimSourcePath()
{
    const QStringList candidates = {
        appDataDir() + QStringLiteral("/steam-shim/steam.exe"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/steam-shim/steam.exe"),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString ensureSteamShimInPrefix(const QString& compatDataPath)
{
    const QString source = steamShimSourcePath();
    if (source.isEmpty() || compatDataPath.isEmpty())
        return {};

    const QString driveC = compatDataPath + QStringLiteral("/pfx/drive_c");
    if (!QFileInfo::exists(driveC))
        return {};

    const QString dir = driveC + QStringLiteral("/arachnel");
    QDir().mkpath(dir);
    const QString dest = dir + QStringLiteral("/steam.exe");
    // Refresh whenever the shipped stub is newer, so a rebuilt stub reaches old prefixes.
    const QFileInfo destInfo(dest);
    if (!destInfo.exists() || QFileInfo(source).lastModified() > destInfo.lastModified()) {
        QFile::remove(dest);
        if (!QFile::copy(source, dest))
            return {};
    }
    return QStringLiteral("C:\\arachnel\\steam.exe");
}

QString unsteamPayloadDir()
{
    if (!g_payloadDirOverride.isEmpty() && dirHasPayload(g_payloadDirOverride))
        return g_payloadDirOverride;

    const QString inData = appDataDir() + QStringLiteral("/unsteam");
    if (dirHasPayload(inData))
        return inData;

    const QString bundled =
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/unsteam");
    if (dirHasPayload(bundled))
        return bundled;

    return {};
}

void setUnsteamPayloadDirOverride(const QString& dir)
{
    g_payloadDirOverride = dir.trimmed();
}

bool hasUnsteamPayload()
{
    return !unsteamPayloadDir().isEmpty();
}

UnsteamOverlayState detectUnsteamOverlay(const QString& installPath)
{
    UnsteamOverlayState state;
    for (const QString& path : candidateDirs(installPath)) {
        const QDir dir(path);
        bool active = false;
        if (!dirHasUnsteam(dir, &active))
            continue;
        state.present = true;
        state.overlayDir = path;
        state.iniPath = dir.exists(QLatin1String(kIniName)) ? dir.filePath(QLatin1String(kIniName))
                                                            : QString();
        state.enabled = active;
        if (active)
            break;  // an active overlay wins over a disabled one further down
    }
    return state;
}

QVariantMap unsteamOverlayInfo(const QString& installPath)
{
    const UnsteamOverlayState state = detectUnsteamOverlay(installPath);
    QString label;
    if (!state.present)
        label = hasUnsteamPayload() ? QCoreApplication::translate("Core", "Not installed")
                                    : QCoreApplication::translate("Core", "Payload missing");
    else if (state.enabled)
        label = QCoreApplication::translate("Core", "Enabled");
    else
        label = QCoreApplication::translate("Core", "Disabled");

    return {
        {QStringLiteral("unsteamPresent"), state.present},
        {QStringLiteral("unsteamEnabled"), state.enabled},
        // Offer the switch when it is installed, or when we hold a payload to install.
        {QStringLiteral("unsteamCanToggle"), state.present || hasUnsteamPayload()},
        {QStringLiteral("unsteamPayloadAvailable"), hasUnsteamPayload()},
        {QStringLiteral("unsteamLabel"), label},
        {QStringLiteral("unsteamOverlayDir"), state.overlayDir},
    };
}

bool setUnsteamOverlayEnabled(const QString& installPath, bool enabled, QString* error)
{
    const UnsteamOverlayState state = detectUnsteamOverlay(installPath);
    if (!state.present) {
        if (error)
            *error = QCoreApplication::translate("Core", "Unsteam is not installed for this game");
        return false;
    }

    const QDir dir(state.overlayDir);
    const QString proxy = QLatin1String(kProxyName);
    const QString payload = QLatin1String(kPayloadName);
    const QString off = QLatin1String(kDisabledSuffix);

    bool changed = false;
    if (enabled) {
        changed |= renameIfPresent(dir, proxy + off, proxy);
        changed |= renameIfPresent(dir, payload + off, payload);
    } else {
        changed |= renameIfPresent(dir, proxy, proxy + off);
        changed |= renameIfPresent(dir, payload, payload + off);
    }

    if (!changed && detectUnsteamOverlay(installPath).enabled != enabled) {
        if (error)
            *error = QCoreApplication::translate("Core", "Could not rename the Unsteam files");
        return false;
    }
    return true;
}

bool installUnsteamOverlay(const QString& installPath, const QString& executablePath,
                           const QString& realAppId, const QString& playerName, QString* error)
{
    const QString payloadRoot = unsteamPayloadDir();
    if (payloadRoot.isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate(
                "Core", "No Unsteam payload available. Set its folder in Settings.");
        }
        return false;
    }

    const QString target = overlayDirFor(installPath, executablePath);
    if (target.isEmpty() || !QFileInfo::exists(target)) {
        if (error)
            *error = QCoreApplication::translate("Core", "Game folder not found");
        return false;
    }

    // Unsteam ships per-architecture names; the game folder always gets the plain ones,
    // because unsteam.ini's dll_file and the winmm proxy lookup both expect those.
    const int bits = executablePath.isEmpty() ? 64 : peImageBits(executablePath);
    const bool use64 = bits != 32;
    const QDir source(payloadRoot + (use64 ? QStringLiteral("/x64") : QStringLiteral("/x86")));
    const QString sourcePayload =
        source.filePath(use64 ? QStringLiteral("unsteam64.dll") : QStringLiteral("unsteam.dll"));
    const QString sourceProxy =
        source.filePath(use64 ? QStringLiteral("winmm64.dll") : QStringLiteral("winmm.dll"));

    const QDir dir(target);
    for (const auto& pair : {std::pair<QString, QString>{sourcePayload, QLatin1String(kPayloadName)},
                             std::pair<QString, QString>{sourceProxy, QLatin1String(kProxyName)}}) {
        if (!QFileInfo::exists(pair.first)) {
            if (error) {
                *error = QCoreApplication::translate("Core", "Unsteam payload is incomplete: %1")
                             .arg(QFileInfo(pair.first).fileName());
            }
            return false;
        }
        const QString dest = dir.filePath(pair.second);
        // A pre-existing winmm.dll belongs to something else (Online Fix). Keep it.
        if (QFileInfo::exists(dest) && pair.second == QLatin1String(kProxyName)
            && !QFileInfo::exists(dir.filePath(QLatin1String(kIniName)))) {
            QFile::rename(dest, dest + QStringLiteral(".pre-unsteam"));
        }
        QFile::remove(dest);
        if (!QFile::copy(pair.first, dest)) {
            if (error) {
                *error = QCoreApplication::translate("Core", "Could not copy %1 into the game")
                             .arg(pair.second);
            }
            return false;
        }
    }

    const QString iniPath = dir.filePath(QLatin1String(kIniName));
    QSettings ini(iniPath, QSettings::IniFormat);
    ini.setValue(QStringLiteral("loader/exe_file"), QFileInfo(executablePath).fileName());
    ini.setValue(QStringLiteral("loader/dll_file"), QLatin1String(kPayloadName));
    if (!realAppId.trimmed().isEmpty())
        ini.setValue(QStringLiteral("game/real_app_id"), realAppId.trimmed());
    ini.setValue(QStringLiteral("game/fake_app_id"), QStringLiteral("480"));
    if (!playerName.trimmed().isEmpty())
        ini.setValue(QStringLiteral("game/player_name"), playerName.trimmed());
    if (!ini.contains(QStringLiteral("game/offline_mode")))
        ini.setValue(QStringLiteral("game/offline_mode"), QStringLiteral("0"));
    if (!ini.contains(QStringLiteral("game/language")))
        ini.setValue(QStringLiteral("game/language"), QStringLiteral("english"));
    if (!ini.contains(QStringLiteral("game/beta_name")))
        ini.setValue(QStringLiteral("game/beta_name"), QStringLiteral("public"));
    ini.sync();
    // QSettings drops an empty section; the shipped example carries [dlcs], so keep it.
    if (ini.status() == QSettings::NoError) {
        QFile iniFile(iniPath);
        if (iniFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray body = iniFile.readAll();
            iniFile.close();
            if (!body.contains("[dlcs]")
                && iniFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                iniFile.write("\n[dlcs]\n");
                iniFile.close();
            }
        }
    }
    if (ini.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("Core", "Could not write unsteam.ini");
        return false;
    }
    return true;
}

bool removeUnsteamOverlay(const QString& installPath, QString* error)
{
    const UnsteamOverlayState state = detectUnsteamOverlay(installPath);
    if (!state.present)
        return true;

    const QDir dir(state.overlayDir);
    const QString off = QLatin1String(kDisabledSuffix);
    for (const QString& name : {QLatin1String(kProxyName), QLatin1String(kPayloadName),
                                QLatin1String(kIniName)}) {
        QFile::remove(dir.filePath(name));
        QFile::remove(dir.filePath(name + off));
    }
    // Put back whatever owned winmm.dll before Unsteam took the name.
    const QString saved = dir.filePath(QLatin1String(kProxyName) + QStringLiteral(".pre-unsteam"));
    if (QFileInfo::exists(saved))
        QFile::rename(saved, dir.filePath(QLatin1String(kProxyName)));

    Q_UNUSED(error);
    return true;
}

void applyUnsteamLaunchInfo(const QString& installPath, LaunchInfo* info)
{
    if (!info || installPath.isEmpty())
        return;

    UnsteamOverlayState state = detectUnsteamOverlay(installPath);
    if (!state.enabled && !info->workingDirectory.isEmpty()
        && info->workingDirectory != installPath) {
        const UnsteamOverlayState wd = detectUnsteamOverlay(info->workingDirectory);
        if (wd.enabled)
            state = wd;
    }
    if (!state.enabled)
        return;

    // The proxy has to be the native winmm, loaded from the game folder first.
    const QString overrides = QStringLiteral("winmm=n,b");
    info->wineDllOverrides = info->wineDllOverrides.trimmed().isEmpty()
                                 ? overrides
                                 : info->wineDllOverrides + QLatin1Char(';') + overrides;

    // Unsteam presents real_app_id to the game itself; Steam only ever sees fake_app_id.
    QString fakeAppId = QStringLiteral("480");
    if (!state.iniPath.isEmpty()) {
        QSettings ini(state.iniPath, QSettings::IniFormat);
        const QString fromIni = ini.value(QStringLiteral("game/fake_app_id")).toString().trimmed();
        if (!fromIni.isEmpty())
            fakeAppId = fromIni;
    }
    info->environmentExtras.insert(QStringLiteral("SteamAppId"), fakeAppId);
    info->environmentExtras.insert(QStringLiteral("SteamGameId"), fakeAppId);
}

} // namespace arachnel::core
