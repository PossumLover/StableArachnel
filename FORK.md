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

## Known, not fixed

- `DownloadJobGroupCard.qml:109` — "Qt Quick Layouts: Detected recursive
  rearrange" fires several times per job update. Harmless (Qt aborts after two
  iterations) but it floods the 29-line recent-log window in crash reports.
- The stall itself. Fix 3 addresses the most likely cause, but until a report
  with a real hung stack shows up, that is inference, not proof. If it freezes
  again, `hang-report-latest.txt` now has the answer.

## Build

    ./run.sh              # configure + build + run
    ./run.sh --rebuild    # clean rebuild

Same org/app name as upstream, so it reads and writes the same
`~/.local/share/Arachnel/Arachnel/` library — no migration, but also don't run
both at once.
