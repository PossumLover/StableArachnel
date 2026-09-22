#pragma once

#include "plugin_interface.h"

#include <QString>
#include <QVariantMap>

namespace arachnel::core {

/** On-disk Online Fix (SteamFix / winmm, or older valve steam_api backups). */
struct OnlineFixOverlayState {
    bool present = false;  // active or disabled overlay / marker found
    bool enabled = false;  // overlay DLLs active (not renamed *.arachnel-off)
    QString overlayDir;
};

OnlineFixOverlayState detectOnlineFixOverlay(const QString& installPath);

/**
 * Valve's shift+tab overlay on top of an Online Fix game.
 *
 * Arachnel used to stand Valve's gameoverlayrenderer down for every install that
 * ships OnlineFix64.dll / SteamOverlay*.dll, which is every Online Fix game - so
 * the overlay could never appear. OnlineFix Linux Launcher (SOFL) runs the same
 * layers with the overlay on, by preloading both gameoverlayrenderer.so builds
 * AND running inside the Steam Linux Runtime with the Steam client up. The
 * earlier "Failed to load steam overlay dll (126)" attempts had neither.
 *
 * Opt-in per game, stored as a marker beside the install the way the Online Fix
 * enable/disable state already is.
 */
bool steamOverlayForced(const QString& installPath);
bool setSteamOverlayForced(const QString& installPath, bool forced);
/** Enable/disable Online Fix (SteamFix rename, or marker + Valve restore for old embeds). */
bool setOnlineFixOverlayEnabled(const QString& installPath, bool enabled, QString* error = nullptr);
/** Labels + flags for Game Settings / entryDetails. */
QVariantMap onlineFixOverlayInfo(const QString& installPath);

/**
 * Proton / Wine launch extras for SteamFix / Online-Fix overlays (SOFL-compatible).
 * Sets WINEDLLOVERRIDES, optional legacy steam-runtime/run.sh, and LD_PRELOAD
 * gameoverlayrenderer (+ SteamAppId/SteamGameId). Safe no-op when overlay is missing/disabled.
 */
/**
 * Repacks that nest the game one directory down leave the fix beside the install root
 * while the executable lives in a subdirectory. Windows resolves winmm.dll from the
 * executable's own directory, so the loader is never found there, dlllist.txt is never
 * read and the whole layer silently does nothing while reporting itself enabled. Copy
 * the loader, the DLLs it lists and the ini next to the executable. Returns how many
 * files were placed.
 */
int healOnlineFixLayoutForExecutable(const QString& installPath, const QString& executablePath);

void applyOnlineFixLaunchInfo(const QString& installPath, LaunchInfo* info,
                              const QString& realAppId = {});

bool isSteamClientRunning();
/** Best-effort: spawn Steam detached. Returns true if the process was started. */
bool tryStartSteamClient();

} // namespace arachnel::core
