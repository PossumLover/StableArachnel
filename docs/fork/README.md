# Fork notes

Working notes for this fork ([`FORK.md`](../../FORK.md) covers what the fork
changed and why). These exist so a later session does not re-derive what already
cost a day, and — more importantly — does not re-try what has already been ruled
out.

| | |
|---|---|
| [diagnosing-game-launch.md](diagnosing-game-launch.md) | How to find out why a game will not start on Linux. Read this first; it is the method the rest came from. |
| [steam-compat-layers.md](steam-compat-layers.md) | Online Fix / SteamFix / Unsteam: what each one is, the layout rule that governs all of them, and the AppId split. |
| [steam-overlay.md](steam-overlay.md) | The shift+tab overlay. **Open.** What SOFL does, what has been ruled out, what to try next. |

## House rules learned the hard way

- **One variable at a time.** Bundling the Steam Linux Runtime in with the
  overlay preload cost a round trip: the runtime changes the program Arachnel
  spawns, so a failure there is indistinguishable from the game quitting.
- **Diff against a known-good install** before theorising. `md5sum` across a
  working game's fix files settles version questions in one command.
- **Arachnel's auto-fallback corrupts experiments.** A quick exit makes it
  disable Online Fix and relaunch, so run N+1 is not the configuration you set.
  Check `Online Fix: enabled/disabled` in `run.log` for the run you are reading.
- **Rose's instance is usually running with a long download in flight.** Never
  `pkill` by pattern; take the PID first.
