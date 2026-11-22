#define MyAppName "Sprout"
#define MyAppExeName "SproutLauncher.exe"
#define LegacyAppName "Arachnel"
#define PrevAppName "JamesGames"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef DistDir
  #define DistDir "..\..\dist-win"
#endif

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppName}
AppPublisherURL=https://github.com/PossumLover/StableArachnel
DefaultGroupName={#MyAppName}
; Don't reuse the Arachnel or JamesGames Start Menu group.
UsePreviousGroup=no
UsePreviousAppDir=yes
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=yes
DisableReadyPage=yes
OutputDir=output
; The launcher icon retains its historical filename.
SetupIconFile=..\..\resources\icons\arachnel.ico
WizardStyle=modern
WizardImageFile=assets\wizard-image.bmp
WizardSmallImageFile=assets\wizard-small.bmp
LanguageDetectionMethod=none
UsePreviousLanguage=no
ShowLanguageDialog=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
VersionInfoDescription={#MyAppName} Setup
VersionInfoProductName={#MyAppName}
#ifdef Preview
AppId=SproutWizardPreview
DefaultDirName={tmp}\{#MyAppName}
Uninstallable=no
OutputBaseFilename=Sprout-UI-Test
#else
; Keep the shipping AppId for in-place upgrades, including pre-rename installs.
AppId={{A8E3C1B2-4D5F-6A70-8B9C-0D1E2F3A4B5C}
DefaultDirName={code:GetDefaultDir}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputBaseFilename=SproutLauncher-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=no
PrivilegesRequiredOverridesAllowed=dialog commandline
CloseApplications=yes
RestartApplications=no
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.CreateStartMenuIcon=Create a &Start Menu shortcut
russian.CreateStartMenuIcon=Создать ярлык в меню «Пуск»

[Tasks]
; Upgrades from the custom wizard have no saved Inno task state. Migrate their icon.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Check: HasDesktopShortcut
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Check: not HasDesktopShortcut; Flags: unchecked
Name: "startmenuicon"; Description: "{cm:CreateStartMenuIcon}"; GroupDescription: "{cm:AdditionalIcons}"

#ifndef Preview
[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Remove only obsolete launcher files and shortcuts, never libraries or settings.
Type: files; Name: "{app}\uninstall.exe"
Type: files; Name: "{app}\arachnel_setup.exe"
Type: files; Name: "{app}\arachnel_setup_launcher.exe"
Type: files; Name: "{app}\arachnel_app.exe"
Type: files; Name: "{userdesktop}\{#LegacyAppName}.lnk"
Type: files; Name: "{userprograms}\{#LegacyAppName}\{#LegacyAppName}.lnk"
Type: files; Name: "{userprograms}\{#LegacyAppName}\Uninstall {#LegacyAppName}.lnk"
Type: dirifempty; Name: "{userprograms}\{#LegacyAppName}"
Type: files; Name: "{app}\JamesGames.exe"
Type: files; Name: "{userdesktop}\{#PrevAppName}.lnk"
Type: files; Name: "{userprograms}\{#PrevAppName}\{#PrevAppName}.lnk"
Type: files; Name: "{userprograms}\{#PrevAppName}\Uninstall {#PrevAppName}.lnk"
Type: dirifempty; Name: "{userprograms}\{#PrevAppName}"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenuicon
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"; Tasks: startmenuicon
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Existing shared game links use the arachnel:// scheme.
Root: HKCU; Subkey: "Software\Classes\arachnel"; ValueType: string; ValueName: ""; ValueData: "URL:{#MyAppName} Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\arachnel"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\arachnel\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall unchecked skipifsilent
; The in-app updater expects /SILENT to relaunch automatically.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait skipifnotsilent runasoriginaluser
#endif

[Code]
function HasDesktopShortcut: Boolean;
begin
  Result := FileExists(ExpandConstant('{userdesktop}\{#LegacyAppName}.lnk')) or
            FileExists(ExpandConstant('{userdesktop}\{#PrevAppName}.lnk')) or
            FileExists(ExpandConstant('{userdesktop}\{#MyAppName}.lnk'));
end;

#ifndef Preview
const
  LegacyUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#LegacyAppName}';

function GetDefaultDir(Param: String): String;
var
  Dir: String;
begin
  if not RegQueryStringValue(HKLM, LegacyUninstallKey, 'InstallLocation', Dir) then
    RegQueryStringValue(HKCU, LegacyUninstallKey, 'InstallLocation', Dir);
  if (Dir <> '') and DirExists(Dir) then begin
    Result := Dir;
    Exit;
  end;
  Dir := ExpandConstant('{localappdata}\Programs\{#LegacyAppName}');
  if FileExists(Dir + '\{#MyAppExeName}') or FileExists(Dir + '\JamesGames.exe') or
     FileExists(Dir + '\arachnel_app.exe') then
    Result := Dir
  else
    Result := ExpandConstant('{localappdata}\Programs\{#MyAppName}');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    RegDeleteKeyIncludingSubkeys(HKLM, LegacyUninstallKey);
    RegDeleteKeyIncludingSubkeys(HKCU, LegacyUninstallKey);
  end;
end;
#endif
