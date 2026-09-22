# Steam compatibility layers under Proton

Two different things get called "the fix". They are not alternatives of equal
power.

| | Online Fix / SteamFix | Unsteam |
|---|---|---|
| What it is | a Steam emulator with its own backend | a detour hook over the *real* `steam_api` |
| Lobby calls | its own lobby backend | reach real Steam, which will not mint a lobby for an unowned app |
| Loader | `winmm.dll` reads `dlllist.txt` | `winmm.dll` too — they collide, only one at a time |
| Tells | `OnlineFix64.dll`, `OnlineFix.ini` | `failed to make trampoline`, `[SteamUtils] Unable to open the original library file` |

Unsteam is UPX-packed; `upx -d` it to read anything. It also needs a live
`steam.exe`: it reads `HKCU\Software\Valve\Steam\ActiveProcess` and checks the PID
exists, which Proton leaves as a placeholder. `resources/steam-shim/steam_stub.c`
registers itself there and sleeps — it **must** be linked `-static` or it fails to
start under Proton silently.

## The rule that governs everything: the fix sits beside the game EXECUTABLE

Windows resolves `winmm.dll` from the executable's own directory. Repacks that
nest the game one level down — `<install>/How to Fish/How to Fish.exe` — while
shipping the fix in the install root mean the loader is never found, `dlllist.txt`
is never read, and the layer silently does nothing **while Arachnel reports
"Online Fix: enabled"**.

`healOnlineFixLayoutForExecutable()` (61a0a95) copies the loader, the DLLs it
lists and the ini next to the executable. It must only fill in files that are
MISSING (d988f82): overwriting from the root replaces a working build of the layer
with the one that never worked. Weeks of AppId experiments were wasted on this.

## Missing `winmm.dll` is a silent no-op, not an error

Everything can be present — `dlllist.txt`, `OnlineFix64.dll`, `SteamOverlay64.dll`,
`StubDRM64.dll`, `OnlineFix.ini` — and the layer still never loads if `winmm.dll`
is absent, because `winmm.dll` is what reads the list. `WINEDLLOVERRIDES` carries
`winmm=n,b`, so Wine silently falls back to builtin winmm and nothing complains.

Check first:

```sh
for d in /media/rose/data/Games/*/; do
  [ -f "$d/dlllist.txt" ] && echo "$(basename $d): winmm=$([ -f "$d/winmm.dll" ] && echo YES || echo MISSING)"
done
```

Steam **depot** downloads do not ship it — it is a repack file. Cities: Skylines II
had every other fix file and no loader.

## The AppId split

- `RealAppId` is what the GAME is shown. It must be the game's own id, or titles
  that check quit with `AppId Reported = 480, AppId Expected = <real>`.
- `FakeAppId` is what STEAM is shown. `480` (Spacewar) is right — every account
  owns it.
- Do **not** export the real id as `$SteamAppId` from the launcher. Let the layer
  translate, which is what the Windows installs do.
- An ini shipping `FakeAppId` alone has nothing to translate from and needs
  `RealAppId` written in (f96c77d).

Without any layer, the game reads `steam_appid.txt` directly and compares against
its built-in id — `AppID does not match`, clean exit.

## A complete `OnlineFix.ini`

`[Main]` + `[Misc]` alone is not enough; a working config also carries
`[Interfaces]` (Apps/User/Utils/Storage/UserStats/Friends/UGC/Inventory/AppTicket)
and `[Hashes]`. A 68-byte ini produced `Failed to load OnlineFix64.dll from the
list. Error code 126`; filling in the structure cleared it.

## "Self-protection failed. Error code: 4" and `[Hashes]`

`OnlineFix64.dll` refuses some games with a blocking dialog,
`Self-protection failed.\nError code: 4`, before the game's engine starts.
It is tied to the per-game `[Hashes]` entry in `OnlineFix.ini`:

- Removing `[Hashes]` from a **working** game (How to Fish) reproduces the dialog
  exactly, and Unity never starts.
- The value is per game (How to Fish and Machine Party run the same 13,401,600-byte
  DLL with different hashes) and is **not derivable**: no match across 337 install
  files x SHA-512 / SHA3-512 / BLAKE2b-512, nor 132 derivations from app ids and
  titles. Best guess, unconfirmed because the DLL is packed: an authorization token
  Online Fix issues per game, checked for titles the DLL was not built to accept.
- **A missing hash does not by itself mean error 4.** Teardown, Satisfactory, BOKURA,
  Far Far West, Core Keeper and Enshrouded all run with Online Fix and no hash,
  multiplayer included. Never act on "no hash"; act on the error.

The only place the real value exists is the game's own Online Fix release (archive
password `online-fix.me`, per SOFL's extractor).

## SteamFix: the fallback when Online Fix refuses a game

SteamFix (`SteamFix64.dll` + its **own** 23 KB `winmm.dll`, which reads `winmm.txt`)
is a separate emulator with no `[Hashes]` and no self-protection. Its ini is generic:
app ids, `[Misc] Overlay`, `[Interfaces]`. Cities: Skylines II goes from the error
dialog to its main menu on it.

Arachnel does this automatically (8ebc26a):

1. `WINEDEBUG=-all,+msgbox` puts dialog text in `launch-<id>.log`.
2. For 60s after launch the watcher scans that log for `Self-protection failed`.
   The dialog *blocks* rather than exits, so no exit-based check ever saw it.
3. `convertOnlineFixToSteamFix()` moves every Online Fix file beside the exe and in
   the install root to `<AppData>/backups/<id>-onlinefix-<time>/` with a
   `RESTORE.txt`, installs the kit beside the exe, writes `winmm.txt` and a
   `SteamFix.ini` with the game's real app id, then relaunches.

The kit is **not** in the repo (third-party binary). Arachnel reads it from
`<AppData>/kits/steamfix/` and says where to put one if it is missing. The one on
Rose's box came from Big Walk (`SteamFix64.dll` md5 `5430fcd5`, 1,428,480 bytes).
64-bit only; 32-bit games are refused rather than half-converted.

**Target the directory of the executable that actually runs.** The first test
converted `Launcher/` because the watcher took the plugin's executable (Paradox's
bootstrapper) over the per-game override that `resolveLaunch()` really runs.

**Keep the loader and the DLLs version-matched.** A 13,820,928-byte
`OnlineFix64.dll` with a mismatched `winmm.dll`/ini gives error 126; the matching
set works. Checksum against a known-good install rather than guessing.

## Layer state on disk

Arachnel stores enable/disable as file renames, not settings:

- `*.arachnel-off` — the layer is toggled off. Includes any file you added yourself.
- `.arachnel-steam-overlay` — per-game opt-in to Valve's overlay (see
  [steam-overlay.md](steam-overlay.md)).
- `GameOverlayRenderer*.dll` — a Windows-side **alias** for the repack's own
  `SteamOverlay*.dll`. Unrelated to Valve's Linux `gameoverlayrenderer.so`; Online
  Fix installs strip it, everything else plants it. Do not cross the two.
