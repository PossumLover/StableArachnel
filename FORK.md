# Arachnel — Rose's fork

Forked from [BadKiko/Arachnel](https://github.com/BadKiko/Arachnel) at `v0.1.47`.
Branch: `rose-fork`. Upstream `main` is untouched, so rebasing on a future
release stays easy.

The fork exists because the app kept dying on this machine — always at ~140s
uptime, always with a "UI hang / not responding (~25s)" report. Two earlier root
causes (an ABI-mismatched FreeTP plugin, a nested `processEvents()` re-entering
the threaded render loop) were fixed or worked around, and the 140s deaths came
back anyway. That pattern says the 140s number belongs to the *watchdog*, not to
any one bug.

## Why every crash landed on 140s

`startHangWatchdog()` (`src/app/crash_log.cpp`) loops: ping the main thread,
wait 25s for the ack, check. Each pass is 28s, so passes end at t≈28, 56, 84,
112, 140. A 120s startup grace period swallows the first four. **t=140s is
simply the first moment the watchdog is allowed to fire.** Any main-thread stall
that is still in progress then produces "Uptime: 140s", whatever caused it.

And what the watchdog did on firing was `_exit(1)` — it killed the app, and with
it every in-flight download. A frozen UI is not a dead process: the blocking
call nearly always returns.

## Changes

**1. A hang no longer kills the app** (`crash_log.cpp`, `crash_log_internal.cpp`)

`reportUiHang()` now writes the report and returns. The watchdog keeps watching
instead of breaking out, logs `[hang] main thread resumed after ~Ns` when the UI
comes back, and re-reports a still-frozen UI at most once every 5 minutes.
Hang reports go to `hang-report-latest.txt` plus a `HANG:` entry appended to
`crash.log` — they no longer clobber `crash-report-latest.txt`, which belongs to
real crashes, and no longer raise the crash dialog.

Set `ARACHNEL_HANG_ABORT=1` to restore the old kill-on-hang behaviour.

**2. The hung stack is actually captured** (`crash_log_internal.cpp`)

Linux hang reports used to say `Hung thread stack: (not captured on this
platform)`, which is why three separate investigations had to guess at the
cause. A thread cannot walk another thread's stack, so the fork interrupts the
main thread with `SIGRTMIN+4`; the handler records `backtrace()` frames and the
watchdog formats them. `backtrace()` is warmed up at startup so the handler
never has to dlopen or malloc.

The next freeze names the function that blocked the UI.

**3. Long plugin jobs no longer starve the thread pool**
(`src/core/plugins/plugin_host_async.cpp`)

`runInstallAsync`, `runAddonInstallAsync` and `runOwnedDownloadAsync` ran on the
**global** `QThreadPool` — 8 slots here — and each owned download holds its slot
for the entire download. Three resumed downloads plus a new one is half the
pool; catalog loads, cover fetches and install-kind probes queue behind them,
and a main-thread `waitForFinished()` on a future that has not started yet can
never return. They now use a dedicated 32-slot pool, matching the existing
`movePool` idiom in `core_wiring_services.cpp`.

**4. Empty launch-options binding** (`qml/app/GameSettingsSheet.qml`)

`availableLaunchOptions[0].title` / `.executable` threw
`TypeError: Cannot read property 'title' of undefined` on every open of the
settings sheet for a game with no launch options — QML bindings evaluate even
while their block is `visible: false`. Both are guarded now.

**5. ABI-mismatched plugins are refused, not merely distrusted**
(`src/core/plugins/plugin_host.cpp`) — upstream issue #64

This is the bug Rose reported on 2026-09-01 and that was never fixed. When a
plugin's `CatalogEntry` disagrees with core's, upstream logged the mismatch,
marked the plugin "untrusted", disabled `entryById` / `detectUpdate` — **and
loaded it anyway**. FreeTP v1.0.28 (592 bytes vs core's 544) then ingested ~2,783
entries through that layout, which is where the Windows access violation and the
Linux `free(): invalid size` in `~QQmlEngine` came from.

A layout disagreement is not something a host can work around, so the fork
rejects the plugin outright with a message naming it and both sizes. Note the
legacy (API < 4) path already did exactly this; only the API 4+ path let it
through.

Also honours a layout claim in the manifest's `abiToken` (`"api=4;entry=544"`)
and refuses the plugin *before* its code is mapped. Plain `"api=4"` tokens still
work and fall through to the post-load export check — this is issue #64's request
2 for the builds that adopt it.

Verified against the real FreeTP v1.0.28 binary from
`~/.local/share/Arachnel/freetp-removed-2026-08-31/`: upstream loads it,
the fork rejects it, and `steamidra` (544 == 544) still loads normally.

Issue #64's request 3 (null-check the resolver on the launch path) was not
needed: core already gates `entryById` behind `pluginCatalogEntryLayoutTrusted`,
and with fix 5 no mismatched plugin can be resident at all.

**6. "Detected recursive rearrange"** (`qml/components/DownloadJobCard.qml`)

`implicitWidth: embedded ? (parent ? parent.width : implicitWidth) : implicitWidth`
closed a loop: the layout sets the width from implicitWidth, implicitWidth read
it back off the parent. Both call sites put the card in a layout with
`Layout.fillWidth`, so the binding was never needed — and its other branch
referenced `implicitWidth` itself, so it contributed nothing. Removed.

**7. The updater pointed at upstream** (`src/core/settings/app_updater.cpp`)

Left alone it offers stock 0.1.47 as an "update"; installing that would silently
replace this build and bring every fixed bug back. Now points at
`PossumLover/StableArachnel`, as does the crash reporter's "file an issue" link.

**8. Icon fonts** (`scripts/setup-material-fonts.sh`)

QmlMaterial ships Material Symbols via Git LFS and a fresh `FetchContent` clone
gets pointer files, so a from-source build renders with no icons. The script that
fixes this only knew `apt-get`; it now handles `pacman` too.

## Measured, same data and display

Both builds run against an identical copy of the real library under Xvfb:

| | upstream 0.1.47 | fork |
|---|---|---|
| "recursive rearrange" warnings | 6 | 0 |
| QML warnings total | 6 | 0 |
| FreeTP v1.0.28 (592 vs 544) | loaded | rejected |
| steamidra v0.6.18 | loaded | loaded |

The watchdog fix was verified separately with a temporary 45s main-thread stall
(since reverted): the process survived, the report carried a real backtrace, and
the watchdog logged `[hang] main thread resumed after ~53s`.

## Known, not fixed

- The stall itself. `LaunchController::launchGame` does its prep in a
  `QTimer::singleShot` **on the GUI thread**, and that path calls
  `process.waitForFinished(600000)` inside
  `RuntimeDependencyService::installDepotIntoContainer` — up to a ten-minute
  main-thread block on a game's first launch. That is the prime suspect, but
  moving launch prep onto a worker thread is a real refactor of the most critical
  path in the app, and until a hang report names it, it is inference. Fix 2 means
  the next freeze will say so outright. Breadcrumbs (`runtime.install`) were added
  around the step in the meantime.

## Build

    ./run.sh              # configure + build + run
    ./run.sh --rebuild    # clean rebuild

Needs `boost` headers (libtorrent), `git-lfs` (icon fonts) and Qt 6.11+.
`~/.local/bin/arachnel` runs this build; the stock AppImage is still at
`~/Applications/Arachnel.AppImage` and the old wrapper at
`~/.local/bin/arachnel.bak-2026-09-18-appimage`.

Same org/app name as upstream, so it reads and writes the same
`~/.local/share/Arachnel/Arachnel/` library — no migration, but also don't run
both at once.
