#pragma once

#include "plugin_interface.h"

#include <QString>
#include <QVariantMap>

namespace arachnel::core {

/**
 * Unsteam: a second Steam-compatibility layer alongside Online Fix, for titles where
 * the Online Fix overlay does not work. It is a winmm.dll proxy that loads unsteam.dll
 * and reads unsteam.ini, and unlike Online Fix it does not ship inside repacks - the
 * payload is copied in from `unsteamPayloadDir()`.
 *
 * Only one layer may be active for a game at a time: both proxy the same entry points,
 * so enabling either turns the other off.
 */
struct UnsteamOverlayState {
    bool present = false;  // payload found in the game, active or renamed off
    bool enabled = false;  // proxy DLL active (not renamed *.arachnel-off)
    QString overlayDir;
    QString iniPath;
};

UnsteamOverlayState detectUnsteamOverlay(const QString& installPath);

/** Labels + flags for Game Settings / entryDetails. */
QVariantMap unsteamOverlayInfo(const QString& installPath);

/** Enable/disable an installed Unsteam overlay (renames the proxy + payload DLLs). */
bool setUnsteamOverlayEnabled(const QString& installPath, bool enabled, QString* error = nullptr);

/**
 * Copy the payload next to the game executable and write unsteam.ini for it. Picks the
 * 32- or 64-bit payload from the executable's PE bitness. Safe to re-run: an existing
 * unsteam.ini keeps the user's edits apart from the fields we own.
 */
bool installUnsteamOverlay(const QString& installPath, const QString& executablePath,
                           const QString& realAppId, const QString& playerName,
                           QString* error = nullptr);

/** Remove an installed Unsteam overlay (payload + ini) from a game. */
bool removeUnsteamOverlay(const QString& installPath, QString* error = nullptr);

/**
 * Proton / Wine launch extras: WINEDLLOVERRIDES for the winmm proxy, plus the
 * SteamAppId the ini asks Steam to see. No-op when the overlay is missing or disabled.
 */
void applyUnsteamLaunchInfo(const QString& installPath, LaunchInfo* info);

/**
 * Copy the steam.exe stub into a game's Wine prefix and return its Windows path, or
 * empty when it is unavailable. The stub registers itself in
 * HKCU\Software\Valve\Steam\ActiveProcess so Unsteam's "is Steam running" lookup
 * finds a live process; Proton leaves a placeholder PID there that belongs to nothing,
 * which is what makes Unsteam report "Unable to find steam process".
 */
QString ensureSteamShimInPrefix(const QString& compatDataPath);
/**
 * Kill any steam.exe stub left running from an earlier session. The stub outlives a
 * crash of Arachnel, and while it lives it holds the Wine session open and keeps
 * ActiveProcess pointing at a live PID - so Steam and Arachnel both go on believing a
 * game is running. Returns how many were stopped.
 */
int stopStraySteamShims();

/** Path of the built steam.exe stub, or empty when it was not shipped. */
QString steamShimSourcePath();

/**
 * Directory holding the Unsteam release (`x86/` and `x64/` subdirectories), or empty
 * when it has not been provisioned. Resolution order: the path set in settings, then
 * `<app data>/unsteam`, then `<app dir>/resources/unsteam`.
 */
QString unsteamPayloadDir();
void setUnsteamPayloadDirOverride(const QString& dir);
/** True when a usable payload (both architectures' DLLs) is available to install. */
bool hasUnsteamPayload();

} // namespace arachnel::core
