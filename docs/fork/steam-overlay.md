# The shift+tab overlay — solved

**Status (2026-09-21): working.** Verified on How to Fish, an Online Fix game,
through Arachnel with the per-game toggle on.

Four things all have to be true. Miss any one and the game still runs, so the
only symptom is "no overlay":

1. **Run inside `SteamLinuxRuntime_4`.** Not sniper — see the Python section below.
2. **Hand pressure-vessel BOTH overlay builds**, 32- and 64-bit. It copies them
   into the container and rewrites `LD_PRELOAD` as
   `/tmp/pressure-vessel-libs-*/${LIB}/gameoverlayrenderer.so`, where `${LIB}`
   resolves per architecture. Arch-filtering on the host hands it half the pair.
3. **`ENABLE_VK_LAYER_VALVE_steam_overlay_1=1`** — literally `1`. `true` is
   rejected.
4. **Do NOT set `PROTON_ENABLE_WAYLAND`.** This was the last blocker. Valve's
   `gameoverlayrenderer` hooks the X11/GL/Vulkan presentation path; on Proton's
   native Wayland backend there is nothing for it to hook. SOFL runs through
   XWayland (`nativeWayland=` empty in `Games.ini`, `PROTON_ENABLE_WAYLAND=` with
   no value in the live process).

`SteamOverlayGameId` is set; `SteamAppId`/`SteamGameId` are also set by Arachnel
and SOFL sets neither, which turned out not to matter.

Harmless noise in the launch log: the host-side processes inherit the raw
`LD_PRELOAD` before entering the container and ld.so reports
`wrong ELF class: ELFCLASS32` for the 32-bit path, four times. It is ignored and
the overlay still loads. SOFL avoids it by passing `--ld-preload=` as arguments
to the runtime rather than as an environment variable; Arachnel could do the same
if the noise ever gets in the way.

## How it was found

By diffing `/proc/<pid>/environ` between a live SOFL launch and a live Arachnel
launch **of the same game**, rather than reasoning about it. That is the move to
reach for first next time — two rounds were lost to inference from `strings`
before anyone looked at a running process.

## Why it never worked at all before (history)

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

## The Steam Linux Runtime: use steamrt4, never sniper

Wrapping in `SteamLinuxRuntime_sniper` kills the launch in ~1.3 s with no output.
The cause has nothing to do with the overlay: the `proton` script runs *inside*
the container, and current Proton builds do `from typing import Self` in
`vulkan.py`, which needs Python 3.11+.

```
ImportError: cannot import name 'Self' from 'typing' (/usr/lib/python3.9/typing.py)
```

| runtime | Python | runs GE-Proton11 / Proton-CachyOS |
|---|---|---|
| `SteamLinuxRuntime_sniper` | 3.9.2 | no — ImportError before Wine starts |
| `SteamLinuxRuntime_soldier` | older | no |
| `SteamLinuxRuntime_4` | 3.13.5 | **yes** |

`findSteamLinuxRuntime()` now prefers `SteamLinuxRuntime_4`. Verified by running
the exact failing command against steamrt4: ProtonFixes ran, Fossilize
initialised, the game stayed up.

Reproduce in one line (it exits in about a second when it is broken):

```sh
cd "<game dir>" && env STEAM_COMPAT_DATA_PATH=<prefix> \
  STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam \
  <runtime>/run "<proton>" run "<game>.exe" 2>&1 | tail
```

## Open: SOFL references sniper and bundles a Proton that cannot run in it

`FilesWorker.phb` contains `./steamapps/common/SteamLinuxRuntime_sniper/run`, and
SOFL ships `protons/GE-Proton11-7-x86_64`, whose `vulkan.py` has the same
`from typing import Self`. Those two cannot both be in play the way it is written,
so something about how SOFL actually invokes the runtime is still not understood.
Settle it with ground truth rather than more `strings`: launch a game through
SOFL, then

```sh
pgrep -af proton                      # what is actually in the process tree
tr '\0' '\n' < /proc/<pid>/environ    # the real environment
```

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
