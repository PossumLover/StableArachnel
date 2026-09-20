# Arachnel — Rose's fork

Forked from [BadKiko/Arachnel](https://github.com/BadKiko/Arachnel) at `v0.1.47`.
Work lands on `main` (formerly `rose-fork`). The `master` branch still holds
upstream's tree untouched, so rebasing on a future release stays easy.

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

**9. Installing a plugin froze the whole app** (`plugin_host_async.cpp`,
`plugin_host_packages.cpp`, `core_wiring_services.cpp`)

Caught live with gdb on 2026-09-18 while the UI was wedged:

```
#6  PluginHost::waitForInFlightPluginWorkers()   plugin_host_async.cpp
#8  PluginHost::installFromArach()               plugin_host_packages.cpp:257
#9  CoreController::installPluginArachInternal() plugin_facade.cpp:153
#19 PluginCatalogService::finishInstallAttempt()
    ... on the main thread, inside signal delivery
```

Installing *any* plugin drained *every* plugin's in-flight workers on the GUI
thread. An owned download occupies its worker for the entire download, so
installing FreeTP blocked on an unrelated multi-hour steamidra depot download.
Upstream's watchdog then killed the app at 140s, which is what this looked like
from outside: "installing a plugin crashes it".

Workers are now tagged with the plugin that owns them, and the before-unload hook
only drains the plugin actually being unloaded — installing FreeTP no longer cares
what steamidra is doing. And when the plugin being replaced *does* have its own
work in flight, install and uninstall refuse with a message ("... is still
downloading or installing something") rather than blocking the UI. That matches
what `runOfficialPluginAutoUpdate` already did: defer, never block.

**10. The process ran out of file descriptors** (`src/app/main.cpp`)

At the moment of that freeze the process held exactly 1024 open descriptors
against a 1024 soft limit - **499 of them files under a single steamidra depot
download**, which opens a descriptor per written file and does not close them.
Systemd's user-app units set `LimitNOFILESoft=1024` with a hard limit of
1048576, so the app ran into the soft limit and every later `open()` failed.

That is why the hang report reached stderr but never reached disk: core, not the
plugin, was the thing that could no longer open a file. Settings and library saves
fail the same silent way.

The fork raises `RLIMIT_NOFILE` to the hard limit at startup and logs that it did.
The leak is in the plugin and stays the plugin's bug; this stops it taking core's
ability to write files down with it.

**11. Failed log writes are no longer silent** (`crash_log_internal.cpp`)

`appendToFile` / `writeTextFile` returned quietly when `open()` failed, which is
what hid fix 10. They now print the path and the reason.

**12. The real cause of issue #64: a moved virtual, not the struct size**
(`plugin_api.h`, `plugin_interface_rev1.h`, `plugin_host.cpp`, `docs/PLUGIN_SDK.md`)

Fix 5 refused ABI-mismatched plugins, which was right but incomplete — it treated
`sizeof(CatalogEntry)` as the problem. It wasn't.

Upstream `86b028f` ("changes filters and v4 plugin fix", 2026-08-19) did two things
at once. It dropped `genreTokens` + `genreKeys` from `CatalogEntry` — two
`QStringList` at 24 bytes each, which is exactly the 592 → 544 everyone was
looking at. And it **moved `updateMayBreakDlc`** from between `detectUpdate` and
`launchInfo` to the end of `ISourcePlugin`, without touching
`ARACHNEL_PLUGIN_API_VERSION`.

That second change is the crash. For a plugin built before it, `launchInfo` and
every slot after it shift by one, so the host calling

```
launchInfo(const LibraryGame&) -> LaunchInfo
```

lands in

```
updateMayBreakDlc(const LibraryGame&, const CatalogEntry&) -> bool
```

The `LaunchInfo` return travels through a hidden pointer the caller supplies; a
`bool` return never writes it, and the arguments shift a register, so the plugin
reads a `CatalogEntry&` out of whatever was there. Hence the issue's own title —
"crash upon updating or launching a game" — the `free(): invalid size`, and the
read of `0xffffffffffffffff`. The struct size was a co-traveller from the same
commit, which is why disabling `entryById` never helped.

Measured, not inferred: the SDK at `86b028f^` compiles `CatalogEntry` to exactly
592 bytes against Qt 6, and HEAD to 544.

The fork adds:

- `ARACHNEL_PLUGIN_INTERFACE_REVISION` — a vtable layout number, exported by the
  plugin and checked at load. Any add/remove/reorder/re-sign bumps it. The host
  refuses a revision it cannot speak instead of calling into the wrong slot.
- `arachnel_plugin_abi_sizes()` — every shared struct's size in one table, so
  drift is reported as "LibraryGame is 312 bytes in the plugin and 296 here"
  rather than as a crash mid-call. Non-`CatalogEntry` drift is fatal, since those
  structs cross on install and launch.
- `plugin_interface_rev1.h` — the pre-`86b028f` layout plus an adapter that routes
  each call to the slot a revision-1 plugin actually has. A plugin that does not
  export a revision is identified by its 592-byte `CatalogEntry`, since one commit
  caused both.

**This makes FreeTP v1.0.28 usable again.** It loads through the shim, its full
2,783-entry catalog comes across as JSON, and the app runs past the 140s mark that
used to kill it. `entryById` / `detectUpdate` / `updateMayBreakDlc` stay disabled
for it — its `CatalogEntry` really is a different shape — so FreeTP games do not
get update detection. Everything else works.

`docs/PLUGIN_SDK.md` gains a section explaining why the vtable, not the struct, is
the thing to version.

## Measured, same data and display

Both builds run against an identical copy of the real library under Xvfb:

| | upstream 0.1.47 | fork |
|---|---|---|
| "recursive rearrange" warnings | 6 | 0 |
| QML warnings total | 6 | 0 |
| FreeTP v1.0.28 (592 vs 544) | loaded, then crashed | loaded via revision-1 shim |
| steamidra v0.6.18 | loaded | loaded |
| open-file soft limit | 1024 | raised to hard limit |

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
