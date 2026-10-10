# Playtime and achievements

Sprout records time for games launched through the launcher. Total playtime appears in the library and game details; game details also show the last completed session. Tracking begins with this update, so previous playtime is not recovered. Sessions use a monotonic clock and save checkpoints about every 30 seconds. Failed starts and automatic OnlineFix retries are excluded. Sprout needs to remain running to track a session.

The **Achievements** panel appears for installed games with a known Steam app ID. It shows names, icons, descriptions, and unlocked totals. Locked secret achievements hide their names and descriptions. Use **Refresh** to rescan local files and retry loading details. Details and discovered unlocks are cached for offline use.

This version reads these local formats:

- **OnlineFix:** `Public/Documents/OnlineFix/<appId>/Stats/Achievements.ini` and `<appId>/Achievements.ini`.
- **Goldberg / GSE:** `AppData/Roaming/Goldberg SteamEmu Saves/<appId>/achievements.json` and `GSE Saves/<appId>/achievements.json`, including object and array formats. Game-local saves at `<installPath>/steam_settings/<appId>/achievements.json` are also checked.
- **CODEX / RUNE:** `Public/Documents/Steam/<provider>/<appId>/achievements.ini`; CODEX is also checked under roaming AppData.
- Compatible `achievements.ini` and `achievements.json` files in the game's installation directory.

On Linux, these locations are checked inside the game's Sprout Proton prefix, including the `steamuser` and `Public` directories. Achievement files are read without modifying game saves. Sprout scans at launch, every 15 seconds during a tracked session, and when the session ends, even when the details page is closed.

The panel supports search and All/Unlocked/Locked filters. Newly unlocked achievements appear first, with their local unlock date when available. Secret achievements remain hidden until unlocked. Achievement identifiers are matched without case sensitivity across save files, cached progress, and metadata, so differences in capitalization do not hide unlocks or cause duplicate notifications.

Enable or disable **Achievement unlock notifications** in Settings > Launch. Notifications appear inside Sprout and in its notification list; they do not overlay a running game. Existing achievements are treated as a baseline at launch and are not announced again. Notifications do not change local saves.

Achievement names and icons come from Hydra's metadata API. Sprout does not require a Hydra login. A game without a supported local achievement file can show its achievement list, but unlock progress cannot be inferred. Steam account syncing and other local formats are not included.
