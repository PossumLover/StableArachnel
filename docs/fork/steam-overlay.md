# The shift+tab overlay — open

**Status (2026-09-21): games launch with the toggle on, the overlay does not appear.**
No crash, no regression. The remaining question is why Valve's overlay does not
attach.

## Why it never worked at all before

`appendSteamOverlayEnvironment()` cleared `LD_PRELOAD` and set
`ENABLE_VK_LAYER_VALVE_steam_overlay_1=0` for any install shipping
`OnlineFix64.dll` or `SteamOverlay*.dll` — which is *every* Online Fix game. The
overlay could never fire. The code comment justified it with
`Failed to load steam overlay dll (126)`.

It is now per-game opt-in via a `.arachnel-steam-overlay` marker beside the
install (Game Settings → "Steam overlay (shift+tab)"), off by default.

## What OnlineFix Linux Launcher does

SOFL (`~/.local/share/OnlineFix Linux Launcher`, AUR `onlinefix-linux-launcher-bin`)
runs the same layers with the overlay working. It is a JPHP/Java app; the launch
environment is built in `app/modules/FilesWorker.phb` inside `OFMELauncher.jar`
(unzip it and run `strings` on that file):

```
LD_PRELOAD = <steam>/ubuntu12_32/gameoverlayrenderer.so
           : <steam>/ubuntu12_64/gameoverlayrenderer.so
ENABLE_VK_LAYER_VALVE_steam_overlay_1
SteamOverlayGameId
PROTON_ENABLE_WAYLAND, WINEDLLOVERRIDES, WINEDEBUG,
STEAM_COMPAT_DATA_PATH, STEAM_COMPAT_CLIENT_INSTALL_PATH
```

plus two things around the launch:

- `findSteamRuntime` + `array_unshift` — it **prepends the Steam Linux Runtime**
  to the command line.
- `runSteam` — it refuses to start without the Steam client (`FILESWORKER.STEAMNOTSTARTED`).

It does **not** set `STEAM_COMPAT_MOUNTS`, and it plants no Windows-side alias.

## Ruled out

- **Container visibility.** `SteamLinuxRuntime_sniper/run -- ls /media/rose/data/Games/steam-4001890`
  lists the files. The games path is visible inside the container without
  `STEAM_COMPAT_MOUNTS`.
- **Preloading the host overlay inside the container.**
  `LD_PRELOAD=<overlay.so> run -- /bin/echo` works.
- **The single 64-bit preload path is deliberate**, not a bug:
  `launch_resolver.cpp` calls `filterOverlayPreloadForHost(value, gameBits)`, which
  filters the list to the game's architecture. A 64-bit game correctly gets only
  `ubuntu12_64`.
- **Planting `GameOverlayRenderer64.dll` for Online Fix games.** Tried in e3ed670,
  reverted in 622e742: it is the Windows alias for the repack's *own* overlay DLL,
  nothing to do with Valve's Linux `.so`, and it left a stale file that broke
  Machine Party's next launch.
- **Bundling the Steam Linux Runtime with the toggle.** Also e3ed670 → 622e742:
  How to Fish exited in 2.5 s under it. The runtime changes the program Arachnel
  spawns, so its failures look exactly like the game quitting. Now opt-in
  separately via `ARACHNEL_OVERLAY_STEAM_RUNTIME=1`.

## Current behaviour with the toggle on

Sets `LD_PRELOAD` (arch-filtered), `ENABLE_VK_LAYER_VALVE_steam_overlay_1=true`,
`SteamOverlayGameId`, starts the Steam client if it is not up. Program is Proton
directly. Both How to Fish and Machine Party launch; no overlay.

## What to try next, one at a time

1. `ARACHNEL_OVERLAY_STEAM_RUNTIME=1` with the toggle on — the container is the
   biggest remaining difference from SOFL, and the one most likely to matter for a
   `.so` Steam built for it.
2. Launch a game **through SOFL** and dump `/proc/<pid>/environ` of the running
   Proton process. That is ground truth for the full env, rather than inferring it
   from `strings` on the jar.
3. Compare what Steam itself sets for a real Steam game: same dump, same fields.
   `SteamOverlayGameId` alone may not be enough if the client also has to be
   tracking the process.
4. Check whether the Vulkan overlay layer is even being loaded —
   `VK_LOADER_DEBUG=all` in the game env, then look for
   `VK_LAYER_VALVE_steam_overlay` in the output. That distinguishes "layer never
   loaded" from "layer loaded, no hotkey".

## Do not re-try

Forcing the overlay while also stripping/planting the Windows alias, or while
wrapping the runtime, without changing one variable at a time. That is how two
rounds were already lost.
