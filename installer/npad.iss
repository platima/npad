; npad interactive installer (Inno Setup 6)
; Build: ISCC /DAppVersion=x.y.z npad.iss   (see build-installers.ps1)
;
; Defaults to a per-user install; the dialog (or /ALLUSERS, or launching
; elevated) switches to a system-wide install. Fonts are optional components;
; file associations and the notepad alias takeover are tasks.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "npad"
#define AppPublisher "Platima"
#define AppURL "https://github.com/platima/npad"
#define AppExe "npad.exe"

[Setup]
AppId={{B7E5A2C4-9D31-4F8E-A6C0-3D2E71540A9B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=npad-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
MinVersion=10.0
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation (npad + bundled fonts)"
Name: "minimal"; Description: "npad only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "main"; Description: "npad text editor"; Types: full minimal custom; Flags: fixed
Name: "fonts"; Description: "Open-source fonts (SIL OFL licensed)"; Types: full
Name: "fonts\intelonemono"; Description: "Intel One Mono (monospace)"; Types: full
Name: "fonts\roboto"; Description: "Roboto (proportional)"; Types: full
Name: "fonts\opendyslexic"; Description: "OpenDyslexic (reading assistance)"; Types: full

[Tasks]
Name: "assoc"; Description: "Register npad as an editor for:"
Name: "assoc\txt"; Description: "Text files (.txt)"
Name: "assoc\log"; Description: "Log files (.log)"; Flags: unchecked
Name: "assoc\ini"; Description: "INI configuration files (.ini)"; Flags: unchecked
Name: "assoc\cfg"; Description: "Config files (.cfg)"; Flags: unchecked
Name: "assoc\conf"; Description: "Config files (.conf)"; Flags: unchecked
Name: "notepadalias"; Description: "Open 'notepad' with npad (Win+R and app launches; see docs for the Windows 11 Store alias)"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\npad.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\DOCUMENTATION.md"; DestDir: "{app}"; Flags: ignoreversion

; --- Fonts, system-wide install (admin): FontInstall registers + notifies ---
Source: "fonts\IntelOneMono\IntelOneMono-Regular.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono"; Components: fonts\intelonemono; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-Bold.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Bold"; Components: fonts\intelonemono; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-Italic.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Italic"; Components: fonts\intelonemono; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-BoldItalic.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Bold Italic"; Components: fonts\intelonemono; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Regular.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto"; Components: fonts\roboto; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Bold.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Bold"; Components: fonts\roboto; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Italic.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Italic"; Components: fonts\roboto; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-BoldItalic.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Bold Italic"; Components: fonts\roboto; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\OpenDyslexic-Regular.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic"; Components: fonts\opendyslexic; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\OpenDyslexic-Bold.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Bold"; Components: fonts\opendyslexic; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\OpenDyslexic-Italic.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Italic"; Components: fonts\opendyslexic; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\OpenDyslexic-BoldItalic.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Bold Italic"; Components: fonts\opendyslexic; Check: IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall

; --- Fonts, per-user install: Win10 1809+ user fonts dir + HKCU registration ---
Source: "fonts\IntelOneMono\*.ttf"; DestDir: "{localappdata}\Microsoft\Windows\Fonts"; Components: fonts\intelonemono; Check: not IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\*.ttf"; DestDir: "{localappdata}\Microsoft\Windows\Fonts"; Components: fonts\roboto; Check: not IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\*.otf"; DestDir: "{localappdata}\Microsoft\Windows\Fonts"; Components: fonts\opendyslexic; Check: not IsAdminInstallMode; Flags: onlyifdoesntexist uninsneveruninstall

; Font licenses always accompany the fonts
Source: "fonts\IntelOneMono\OFL.txt"; DestDir: "{app}\licenses\IntelOneMono"; Components: fonts\intelonemono; Flags: ignoreversion
Source: "fonts\Roboto\OFL.txt"; DestDir: "{app}\licenses\Roboto"; Components: fonts\roboto; Flags: ignoreversion
Source: "fonts\OpenDyslexic\OFL.txt"; DestDir: "{app}\licenses\OpenDyslexic"; Components: fonts\opendyslexic; Flags: ignoreversion

[Registry]
; Win+R "npad"
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\npad.exe"; ValueType: string; ValueData: "{app}\{#AppExe}"; Flags: uninsdeletekey
; 'notepad' alias takeover (Run box / ShellExecute). The Windows 11 Store
; Notepad's execution alias cannot be disabled programmatically - the
; installer offers the Settings page after install.
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\notepad.exe"; ValueType: string; ValueData: "{app}\{#AppExe}"; Tasks: notepadalias; Flags: uninsdeletekey

; Default Programs registration (Settings > Default apps lists npad)
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "npad"; ValueData: "Software\Platima\npad\Capabilities"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "npad"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Platima\npad\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Lightweight cross-platform text editor"; Flags: uninsdeletekey

; Per-extension ProgIDs + associations (one task per extension)
; .txt
Root: HKA; Subkey: "Software\Classes\npad.txt"; ValueType: string; ValueData: "Text File"; Tasks: assoc\txt; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.txt\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\txt
Root: HKA; Subkey: "Software\Classes\npad.txt\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\txt
Root: HKA; Subkey: "Software\Classes\.txt"; ValueType: string; ValueData: "npad.txt"; Tasks: assoc\txt; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "npad.txt"; ValueData: ""; Tasks: assoc\txt; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".txt"; ValueData: "npad.txt"; Tasks: assoc\txt; Flags: uninsdeletevalue
; .log
Root: HKA; Subkey: "Software\Classes\npad.log"; ValueType: string; ValueData: "Log File"; Tasks: assoc\log; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.log\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\npad.log\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\.log"; ValueType: string; ValueData: "npad.log"; Tasks: assoc\log; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.log\OpenWithProgids"; ValueType: string; ValueName: "npad.log"; ValueData: ""; Tasks: assoc\log; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".log"; ValueData: "npad.log"; Tasks: assoc\log; Flags: uninsdeletevalue
; .ini
Root: HKA; Subkey: "Software\Classes\npad.ini"; ValueType: string; ValueData: "INI Configuration File"; Tasks: assoc\ini; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.ini\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\ini
Root: HKA; Subkey: "Software\Classes\npad.ini\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\ini
Root: HKA; Subkey: "Software\Classes\.ini"; ValueType: string; ValueData: "npad.ini"; Tasks: assoc\ini; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.ini\OpenWithProgids"; ValueType: string; ValueName: "npad.ini"; ValueData: ""; Tasks: assoc\ini; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ini"; ValueData: "npad.ini"; Tasks: assoc\ini; Flags: uninsdeletevalue
; .cfg
Root: HKA; Subkey: "Software\Classes\npad.cfg"; ValueType: string; ValueData: "Config File"; Tasks: assoc\cfg; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.cfg\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\cfg
Root: HKA; Subkey: "Software\Classes\npad.cfg\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\cfg
Root: HKA; Subkey: "Software\Classes\.cfg"; ValueType: string; ValueData: "npad.cfg"; Tasks: assoc\cfg; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.cfg\OpenWithProgids"; ValueType: string; ValueName: "npad.cfg"; ValueData: ""; Tasks: assoc\cfg; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".cfg"; ValueData: "npad.cfg"; Tasks: assoc\cfg; Flags: uninsdeletevalue
; .conf
Root: HKA; Subkey: "Software\Classes\npad.conf"; ValueType: string; ValueData: "Config File"; Tasks: assoc\conf; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.conf\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\conf
Root: HKA; Subkey: "Software\Classes\npad.conf\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\conf
Root: HKA; Subkey: "Software\Classes\.conf"; ValueType: string; ValueData: "npad.conf"; Tasks: assoc\conf; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.conf\OpenWithProgids"; ValueType: string; ValueName: "npad.conf"; ValueData: ""; Tasks: assoc\conf; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".conf"; ValueData: "npad.conf"; Tasks: assoc\conf; Flags: uninsdeletevalue

; Per-user font registration (admin mode uses FontInstall above instead)
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Regular.ttf"; Check: not IsAdminInstallMode; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Bold (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Bold.ttf"; Check: not IsAdminInstallMode; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Italic.ttf"; Check: not IsAdminInstallMode; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Bold Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-BoldItalic.ttf"; Check: not IsAdminInstallMode; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Regular.ttf"; Check: not IsAdminInstallMode; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Bold (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Bold.ttf"; Check: not IsAdminInstallMode; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Italic.ttf"; Check: not IsAdminInstallMode; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Bold Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-BoldItalic.ttf"; Check: not IsAdminInstallMode; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Regular.otf"; Check: not IsAdminInstallMode; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Bold (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Bold.otf"; Check: not IsAdminInstallMode; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Italic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Italic.otf"; Check: not IsAdminInstallMode; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Bold Italic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-BoldItalic.otf"; Check: not IsAdminInstallMode; Components: fonts\opendyslexic

[Icons]
Name: "{autoprograms}\npad"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\npad"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch npad"; Flags: postinstall nowait skipifsilent
; Windows will not let installers set the default handler programmatically
Filename: "ms-settings:defaultapps"; Description: "Open Default Apps settings (make npad the default editor)"; Flags: postinstall shellexec skipifsilent unchecked
; The Windows 11 Store Notepad alias must be disabled by hand:
; Apps > Advanced app settings > App execution aliases > Notepad (off)
Filename: "ms-settings:advanced-apps"; Description: "Open Settings to disable the Windows 11 Notepad alias (App execution aliases)"; Flags: postinstall shellexec skipifsilent; Check: WizardIsTaskSelected('notepadalias')

[Code]
// Pre-set the bundled fonts in npad's settings.json - only when the file
// does not exist yet (never clobber an existing configuration).
procedure PresetFontConfig();
var
  SettingsDir, SettingsFile, Json: String;
begin
  if not (WizardIsComponentSelected('fonts\intelonemono') or
          WizardIsComponentSelected('fonts\roboto')) then
    Exit;
  SettingsDir := ExpandConstant('{userappdata}\Platima\npad');
  SettingsFile := SettingsDir + '\settings.json';
  if FileExists(SettingsFile) then
    Exit;
  Json := '{';
  if WizardIsComponentSelected('fonts\intelonemono') then
    Json := Json + '"monospace_font": "Intel One Mono"';
  if WizardIsComponentSelected('fonts\roboto') then
  begin
    if Length(Json) > 1 then
      Json := Json + ', ';
    Json := Json + '"proportional_font": "Roboto"';
  end;
  Json := Json + '}';
  if ForceDirectories(SettingsDir) then
    SaveStringToFile(SettingsFile, Json, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    PresetFontConfig();
end;
