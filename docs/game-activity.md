# Playtime and achievements

Sprout records time for games launched through the launcher. Total playtime appears in the library and game details; game details also show the last completed session. Tracking begins with this update, so previous playtime is not recovered. Sessions use a monotonic clock and save checkpoints about every 30 seconds. Failed starts and automatic OnlineFix retries are excluded. Sprout needs to remain running to track a session.

The **Achievements** panel appears for installed games with a known Steam app ID. It shows names, icons, descriptions, and unlocked totals. Locked secret achievements hide their names and descriptions. Use **Refresh** to rescan local files and retry loading details. Details and discovered unlocks are cached for offline use.

This version reads these local formats:

- **OnlineFix:** `Public/Documents/OnlineFix/<appId>/Stats/Achievements.ini` and `<appId>/Achievements.ini`.
- **Goldberg / GSE:** `AppData/Roaming/Goldberg SteamEmu Saves/<appId>/achievements.json` and `GSE Saves/<appId>/achievements.json`, including object and array formats.
- **CODEX / RUNE:** `Public/Documents/Steam/<provider>/<appId>/achievements.ini`; CODEX is also checked under roaming AppData.
- Compatible `achievements.ini` and `achievements.json` files in the game's installation directory.

On Linux, these locations are checked inside the game's Sprout Proton prefix, including the `steamuser` and `Public` directories. Achievement files are read without modifying game saves. Unlocks refresh while the game's details page is open and after a tracked session ends.

Achievement names and icons come from Hydra's metadata API. Sprout does not require a Hydra login. A game without a supported local achievement file can show its achievement list, but unlock progress cannot be inferred. Other formats, Steam account syncing, friends, and achievement notifications are not included in this version.
