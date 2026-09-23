#include "launch_resolver.h"

#include "file_utils.h"
#include "install_heuristics.h"
#include "proton_manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace arachnel::core {

QStringList splitLaunchArguments(const QString& text)
{
    QStringList result;
    QString current;
    bool inQuotes = false;

    const QString trimmed = text.trimmed();
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if ((ch == QLatin1Char(' ') || ch == QLatin1Char('\t')) && !inQuotes) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }

    if (!current.isEmpty())
        result.append(current);
    return result;
}

LaunchOptions parseLaunchOptions(const QString& text)
{
    LaunchOptions options;
    const QStringList tokens = splitLaunchArguments(text);
    const int commandIndex = tokens.indexOf(QStringLiteral("%command%"));

    // No %command%: every token is a game argument, exactly as before. Lifting VAR=value
    // out of a string that never asked for substitution would silently change the meaning
    // of launch options people already rely on.
    if (commandIndex < 0) {
        options.arguments = tokens;
        return options;
    }

    static const QRegularExpression assignment(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)=(.*)$"),
                                               QRegularExpression::DotMatchesEverythingOption);

    int i = 0;
    for (; i < commandIndex; ++i) {
        const QRegularExpressionMatch match = assignment.match(tokens.at(i));
        if (!match.hasMatch())
            break;
        options.environment.insert(match.captured(1), match.captured(2));
    }

    // Anything still left before %command% is a wrapper command (mangohud, gamemoderun…).
    for (; i < commandIndex; ++i)
        options.wrapper.append(tokens.at(i));

    options.arguments = tokens.mid(commandIndex + 1);
    return options;
}

namespace {

bool shouldUseProton(const QString& executable)
{
#if !defined(Q_OS_LINUX)
    (void)executable;
    return false;
#else
    return executable.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
#endif
}

QProcessEnvironment buildProtonEnvironment(const QString& gameId, const QString& protonInstallDir,
                                           ProtonManager& manager)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Host Steam / NixOS sessions often inject steam-runtime into LD_LIBRARY_PATH.
    // That breaks /usr/bin/env (ATTR_1.3) and unrelated host tools. Keep a clean
    // baseline; Proton and optional run.sh set up what they need themselves.
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    env.remove(QStringLiteral("LD_PRELOAD"));
    env.remove(QStringLiteral("STEAM_RUNTIME"));
    env.remove(QStringLiteral("STEAM_RUNTIME_LIBRARY_PATH"));
    env.insert(QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"), manager.steamCompatClientPath());
    env.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"), manager.compatDataPathForGame(gameId));
    // Everything off except message boxes: +msgbox puts the text of any Windows
    // dialog in launch-<id>.log (MSGBOX_OnInit L"..."). That is how Arachnel spots
    // Online Fix's "Self-protection failed" - otherwise only visible on screen.
    // It prints nothing unless a dialog actually opens.
    env.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all,+msgbox"));
    if (!protonInstallDir.trimmed().isEmpty())
        env.insert(QStringLiteral("PROTON_PATH"), protonInstallDir);
    return env;
}

bool hostBreaksWithLegacySteamRuntime()
{
    // Legacy ubuntu12_32/steam-runtime/run.sh puts old libs on LD_LIBRARY_PATH.
    // NixOS / ostree / Bazzite then fail shebang `/usr/bin/env` with libattr ATTR_1.3.
    if (QFileInfo::exists(QStringLiteral("/etc/NIXOS")))
        return true;
    if (QFileInfo::exists(QStringLiteral("/run/ostree-booted")))
        return true;
    if (!qEnvironmentVariableIsEmpty("NIX_STORE") || !qEnvironmentVariableIsEmpty("NIX_PATH"))
        return true;
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly)) {
        const QByteArray text = osRelease.readAll().toLower();
        if (text.contains("bazzite") || text.contains("silverblue")
            || text.contains("kinoite"))
            return true;
    }
    const QByteArray ld = qgetenv("LD_LIBRARY_PATH");
    if (ld.contains("steam-runtime") && ld.contains("libattr"))
        return true;
    return false;
}

QString filterOverlayPreloadForHost(const QString& preload, int gameBits)
{
    // Overlay .so must match the game PE bitness; wrong ELF class is skipped by ld.so.
    // Unknown bitness: keep both.
    QStringList kept;
    for (const QString& part : preload.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        if (p.isEmpty())
            continue;
        if (gameBits == 32 && !p.contains(QStringLiteral("ubuntu12_32/")))
            continue;
        if (gameBits == 64 && !p.contains(QStringLiteral("ubuntu12_64/")))
            continue;
        kept.append(p);
    }
    return kept.join(QLatin1Char(':'));
}

// User launch options win over the plugin's, except for the two variables where
// replacing outright would quietly disable something the plugin needs: those merge.
void applyUserLaunchOptions(ResolvedLaunch* resolved, const LaunchOptions& global,
                            const LaunchOptions& game)
{
    for (const LaunchOptions* options : {&global, &game}) {
        for (auto it = options->environment.constBegin(); it != options->environment.constEnd();
             ++it) {
            if (it.key() == QStringLiteral("WINEDLLOVERRIDES")
                || it.key() == QStringLiteral("LD_PRELOAD")) {
                const QChar separator = it.key() == QStringLiteral("LD_PRELOAD")
                                            ? QLatin1Char(':')
                                            : QLatin1Char(';');
                const QString existing = resolved->environment.value(it.key());
                resolved->environment.insert(it.key(), existing.isEmpty()
                                                           ? it.value()
                                                           : existing + separator + it.value());
            } else {
                resolved->environment.insert(it.key(), it.value());
            }
        }
    }

    const QStringList wrapper = game.wrapper.isEmpty() ? global.wrapper : game.wrapper;
    if (wrapper.isEmpty())
        return;

    QStringList wrapped = wrapper.mid(1);
    wrapped.append(resolved->program);
    wrapped += resolved->arguments;
    resolved->program = wrapper.first();
    resolved->arguments = wrapped;
}

} // namespace

QString chooseLaunchExecutable(const LaunchInfo& pluginInfo, const LibraryGame& game,
                               bool* fromOverride)
{
    if (fromOverride)
        *fromOverride = false;

    const QString overrideExe = game.executableOverride.trimmed();
    if (!overrideExe.isEmpty() && !isExcludedGameExecutable(QFileInfo(overrideExe).fileName())) {
        if (fromOverride)
            *fromOverride = true;
        return overrideExe;
    }

    const QString pluginExe = pluginInfo.executable;
    if (!pluginExe.isEmpty() && !isExcludedGameExecutable(QFileInfo(pluginExe).fileName()))
        return pluginExe;

    // A plugin can name an executable that is not the game - Paradox titles report their
    // launcher bootstrapper, for instance. Rather than refusing to launch, fall back to
    // the same scan that picks an executable at install time; it scores the real game exe
    // far above a helper sitting in a subdirectory.
    if (!game.installPath.isEmpty())
        return findGameExecutableInTree(game.installPath, game.title);
    return {};
}

QString realSteamAppId(const LibraryGame& game)
{
    const QString explicitId = game.steamAppId.trimmed();
    if (!explicitId.isEmpty())
        return explicitId;
    static const QRegularExpression steamId(QStringLiteral("^steam-(\\d+)$"));
    const QRegularExpressionMatch match = steamId.match(game.id);
    return match.hasMatch() ? match.captured(1) : QString();
}

ResolvedLaunch resolveLaunch(const LaunchInfo& pluginInfo, const LibraryGame& game,
                             const SettingsStore& settings, ProtonManager* protonManager)
{
    ResolvedLaunch resolved;

    bool fromOverride = false;
    const QString executable = chooseLaunchExecutable(pluginInfo, game, &fromOverride);
    if (executable.isEmpty())
        return resolved;

    const int gameBits = peImageBits(executable);

    QString workDir = pluginInfo.workingDirectory;
    if (workDir.isEmpty() || fromOverride)
        workDir = QFileInfo(executable).absolutePath();

    const LaunchOptions globalOptions = parseLaunchOptions(settings.globalLaunchArgs());
    const LaunchOptions gameOptions = parseLaunchOptions(game.launchArgs);

    QStringList arguments = pluginInfo.arguments;
    arguments += globalOptions.arguments;
    arguments += gameOptions.arguments;

    const bool useProton = shouldUseProton(executable);

    if (useProton) {
        ProtonManager localManager;
        ProtonManager& manager = protonManager ? *protonManager : localManager;
        const QString protonId = settings.resolvedProtonId(game.protonId, manager);
        const QString proton = manager.executableForId(protonId);
        if (proton.isEmpty())
            return resolved;

        QStringList protonArgs = pluginInfo.argumentsPrefix;
        // `waitforexitandrun`, the verb Steam itself uses, not `run`: protonfixes
        // only runs when argv[1] contains "waitforexitandrun" (check_conditions() in
        // protonfixes/__init__.py). With `run` every launch logged "Skipping fix
        // execution. We are probably running a unit test." and got no game fixes and
        // no -pf_* launch-option support. The verb also waits for the prefix's
        // wineserver to go idle first - see LaunchController for the one case
        // (the Unsteam steam.exe shim) where that must not happen.
        protonArgs += QStringList{QStringLiteral("waitforexitandrun"), executable};
        protonArgs += arguments;

        // SOFL Online-Fix: optionally prefix with legacy steam-runtime/run.sh.
        // Skip legacy on NixOS / hosts where steam-runtime breaks /usr/bin/env.
        const QString runtimeMode =
            pluginInfo.environmentExtras.value(QStringLiteral("ARACHNEL_USE_STEAM_RUNTIME"));
        // Whether the launch really runs inside the Steam Linux Runtime - not merely
        // whether it was asked to (it falls back to bare Proton when unusable).
        bool insideSteamRuntime = false;
        const bool allowLegacy = runtimeMode == QStringLiteral("legacy")
                                 && !hostBreaksWithLegacySteamRuntime();
        if (allowLegacy) {
            const QString legacyRuntime = manager.findLegacySteamRuntime();
            if (!legacyRuntime.isEmpty()) {
                resolved.program = legacyRuntime;
                resolved.arguments = QStringList{proton} + protonArgs;
            } else {
                resolved.program = proton;
                resolved.arguments = protonArgs;
            }
        } else if (runtimeMode == QStringLiteral("1") && !hostBreaksWithLegacySteamRuntime()) {
            const QString steamRuntime = manager.findSteamLinuxRuntime();
            if (!steamRuntime.isEmpty() && manager.steamLinuxRuntimeUsable()) {
                resolved.program = steamRuntime;
                resolved.arguments = QStringList{proton} + protonArgs;
                insideSteamRuntime = true;
            } else if (!steamRuntime.isEmpty() && manager.canAaExecSteamProfile()) {
                insideSteamRuntime = true;
                resolved.program = QStringLiteral("/usr/bin/aa-exec");
                resolved.arguments =
                    QStringList{QStringLiteral("-p"), QStringLiteral("steam"), QStringLiteral("--"),
                                steamRuntime, proton}
                    + protonArgs;
            } else {
                resolved.program = proton;
                resolved.arguments = protonArgs;
            }
        } else {
            resolved.program = proton;
            resolved.arguments = protonArgs;
        }
        resolved.workingDirectory = workDir;
        resolved.environment =
            buildProtonEnvironment(game.id, manager.installDirForId(protonId), manager);

        if (!pluginInfo.wineDllOverrides.trimmed().isEmpty()) {
            const QString existing = resolved.environment.value(QStringLiteral("WINEDLLOVERRIDES"));
            const QString merged = existing.isEmpty()
                                       ? pluginInfo.wineDllOverrides
                                       : existing + QLatin1Char(';') + pluginInfo.wineDllOverrides;
            resolved.environment.insert(QStringLiteral("WINEDLLOVERRIDES"), merged);
        }

        // Skip internal launch hints when applying env extras.
        for (auto it = pluginInfo.environmentExtras.constBegin();
             it != pluginInfo.environmentExtras.constEnd(); ++it) {
            if (it.key().isEmpty() || it.key() == QStringLiteral("ARACHNEL_USE_STEAM_RUNTIME"))
                continue;
            if (it.key() == QStringLiteral("LD_PRELOAD")) {
                const QString existing = resolved.environment.value(QStringLiteral("LD_PRELOAD"));
                // pressure-vessel copies the overlay into the container and rewrites
                // LD_PRELOAD as /tmp/pressure-vessel-libs-*/${LIB}/gameoverlayrenderer.so,
                // where ${LIB} resolves per architecture. Filtering to the game's bitness
                // first hands it only half the pair; SOFL passes both.
                QString added = insideSteamRuntime
                                    ? it.value().trimmed()
                                    : filterOverlayPreloadForHost(it.value().trimmed(), gameBits);
                while (added.startsWith(QLatin1Char(':')))
                    added.remove(0, 1);
                if (added.isEmpty()) {
                    // Empty extra = strip host/Steam overlay preload for this launch.
                    resolved.environment.remove(QStringLiteral("LD_PRELOAD"));
                    continue;
                }
                resolved.environment.insert(QStringLiteral("LD_PRELOAD"),
                                            existing.isEmpty() ? added
                                                               : existing + QLatin1Char(':') + added);
            } else if (it.key() == QStringLiteral("LD_LIBRARY_PATH")) {
                // Never inherit Steam-runtime library paths from extras.
                continue;
            } else {
                resolved.environment.insert(it.key(), it.value());
            }
        }
        applyUserLaunchOptions(&resolved, globalOptions, gameOptions);
        return resolved;
    }

    resolved.program = executable;
    resolved.arguments = pluginInfo.argumentsPrefix + arguments;
    resolved.workingDirectory = workDir;
    resolved.environment = QProcessEnvironment::systemEnvironment();
    resolved.environment.remove(QStringLiteral("LD_PRELOAD"));
    for (auto it = pluginInfo.environmentExtras.constBegin();
         it != pluginInfo.environmentExtras.constEnd(); ++it) {
        if (it.key().isEmpty() || it.key() == QStringLiteral("ARACHNEL_USE_STEAM_RUNTIME"))
            continue;
        if (it.key() == QStringLiteral("LD_PRELOAD")) {
            QString added = it.value().trimmed();
            while (added.startsWith(QLatin1Char(':')))
                added.remove(0, 1);
            if (added.isEmpty()) {
                resolved.environment.remove(QStringLiteral("LD_PRELOAD"));
                continue;
            }
            resolved.environment.insert(QStringLiteral("LD_PRELOAD"), added);
        } else {
            resolved.environment.insert(it.key(), it.value());
        }
    }
    if (!pluginInfo.wineDllOverrides.trimmed().isEmpty()) {
        const QString existing = resolved.environment.value(QStringLiteral("WINEDLLOVERRIDES"));
        const QString merged = existing.isEmpty() ? pluginInfo.wineDllOverrides
                                                  : existing + QLatin1Char(';') + pluginInfo.wineDllOverrides;
        resolved.environment.insert(QStringLiteral("WINEDLLOVERRIDES"), merged);
    }
    applyUserLaunchOptions(&resolved, globalOptions, gameOptions);
    return resolved;
}

} // namespace arachnel::core
