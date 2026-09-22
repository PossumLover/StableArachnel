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

`[Hashes] 0=<128 hex>` is **not** a checksum of any shipped file — it does not
match the SHA-512 of `steam_api64.dll`, `steam_api.dll`, the game exe,
`OnlineFix64.dll` or `winmm.dll`. It is internal to Online Fix and cannot be
synthesised for a new game. Reference copies live in `~/Downloads/OnlineFix Example/`
and `~/Downloads/Unsteam Example/`.

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
