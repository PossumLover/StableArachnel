# Sprout Windows installer

With [Inno Setup 6](https://jrsoftware.org/isinfo.php) installed, run:

```powershell
.\run.ps1 --installer
```

This builds `dist-win/` if needed and writes `SproutLauncher-Setup.exe` at the repo root. The version comes from `ARACHNEL_VERSION` or the nearest `v*` tag. To package an existing `dist-win/` directly, run `.\setup\inno\pack-inno.ps1 -Version <version>`.

The wizard starts in English. Users can choose Russian in the language dialog, or pass `/LANG=russian` on the command line. Fresh installs default to `%LOCALAPPDATA%\Programs\Sprout`. Upgrades reuse the previous installation folder, including the old Arachnel location. In-app updates use `/SILENT /DIR=<install dir>` and relaunch Sprout.

## Preview the wizard

```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" /DPreview .\setup\inno\Sprout.iss
.\setup\inno\output\Sprout-UI-Test.exe
```

The preview uses the shipping wizard with a temporary destination and no launcher payload, shortcuts or registration. The wizard bitmaps are checked in; regenerate them from the launcher artwork with `.\setup\inno\gen-wizard-assets.ps1`.
