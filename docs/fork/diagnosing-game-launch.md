# Diagnosing a game that will not launch

The method below found a black screen, a 4.5 s quit and a UI freeze without
guessing once. Work top to bottom; each step is cheap and rules out a whole layer.

## 1. Separate the layers before touching any config

A launch has three independent layers, and they fail differently:

| Layer | Fails as | Where it says so |
|---|---|---|
| Proton / GPU / renderer | no window, driver errors | `Player.log` device banner |
| Steam identity / DRM / compat layer | quick exit, or init errors | the game's platform log |
| Game logic / assets | window opens, nothing renders | the game's own subsystem logs |

Cities: Skylines II printed `Direct3D 11.0 … GTX 1070 … 2560x1440` in its first
20 lines. That ruled out Proton and the GPU before a single setting was touched —
and the user had been about to spend the evening on Proton versions.

## 2. Read the game's own logs, not just Arachnel's

Arachnel's `run.log` and `launch-<id>.log` tell you what was *spawned*. They rarely
tell you why the game stopped. Unity titles write to the prefix:

```
<compatdata>/<id>/pfx/drive_c/users/steamuser/AppData/LocalLow/<Publisher>/<Game>/
  Player.log         console output, rotated to Player-prev.log each start
  Logs/              per-subsystem logs on better-behaved titles
```

CS2 splits out `SceneFlow`, `Steamworks`, `UI`, `FileSystem`, `InputManager`,
`Modding`. `UI.log` saying `UIView 0 url set to fatal://error` *is* the black
screen — the game is rendering its own error page.

Non-Unity engines differ; look for a log directory under `LocalLow`, `AppData/Local`
or beside the executable before assuming there isn't one.

## 3. Treat log size as a signal

A 52 MB `Player-prev.log` from a 279-second run is not a hang, it is a per-frame
exception loop. Collapse the varying parts and count:

```sh
sed 's/[0-9]\{2,\}/N/g' Player-prev.log | sort | uniq -c | sort -rn | head
```

That turned 52 MB into one line: 91,702 `NullReferenceException` from
`GameManager.OnGUI/Update/LateUpdate`, all downstream of one failure at startup.

## 4. Compare failure modes instead of chasing one

Two runs that both "didn't work" were not the same:

| | `SteamAPI.Init() failed` | `AppID does not match` |
|---|---|---|
| asset database | NullReferenceException | populated fine |
| how far it got | `fatal://error`, black screen | `Creating ECS world`, then clean exit |

The second is the *healthier* run, and it moved the question from "why is it
black" to "why is the AppId wrong" — a much smaller problem.

## 5. Diff against a known-good install

Before theorising about versions, check:

```sh
md5sum winmm.dll OnlineFix64.dll SteamOverlay64.dll StubDRM64.dll   # both installs
objdump -x <dll> | grep 'DLL Name:'                                  # imports
```

Identical checksums against a working game killed the "version mismatch" theory
instantly; an import list of only core Windows DLLs killed the "missing
dependency" theory. What was left was config — which is where the answer was.

## 6. Check the process, not just the logs, when it hangs

```sh
ps -o pid,stat,etime,pcpu,wchan:25 -p <pid>   # futex_wait = blocked on a lock
sudo gdb -p <pid> -batch -ex "thread 1" -ex "bt 25"
```

That is how the plugin-install freeze was pinned to `waitForInFlightPluginWorkers`
on the GUI thread in one shot.

## 7. Watch for the launcher rewriting your test

- Arachnel writes `steam_appid.txt` on every launch; editing it by hand does not stick.
- A quick exit triggers "Online Fix quit right after launch → disabling and relaunching",
  so the *next* run is not the configuration you set.
- Toggling Online Fix off renames files to `*.arachnel-off` — including any file you
  dropped in yourself.
- A second copy of Arachnel exits `0` immediately and silently: the single-instance
  guard hands off to the running one. Abstract-socket based, so `XDG_RUNTIME_DIR`
  does not isolate it — stop the running instance to test a build.
