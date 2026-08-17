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
; Add/Remove Programs shows just "npad" - the version has its own column
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=npad-v{#AppVersion}-setup-win-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
; The 'addtopath' task edits PATH via [Code]; broadcast the change at the end
ChangesEnvironment=yes
; Close a running npad so its exe can be replaced (npad's own save prompt still
; runs), but do NOT let the Restart Manager silently bring it back: the [Run]
; entry offers a relaunch instead, and only when there was something to relaunch.
CloseApplications=yes
RestartApplications=no
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
Name: "addtopath"; Description: "Add npad to the PATH (run 'npad' from Command Prompt / PowerShell)"
Name: "assoc"; Description: "Register npad as an editor for:"
Name: "assoc\text"; Description: "Text files (.txt)"
Name: "assoc\markdown"; Description: "Markdown & documents (.md, .markdown)"; Flags: unchecked
Name: "assoc\data"; Description: "Data files (.csv, .tsv, .json, .xml, .yaml, .yml, .toml)"; Flags: unchecked
Name: "assoc\config"; Description: "Config files (.ini, .cfg, .conf)"; Flags: unchecked
Name: "assoc\log"; Description: "Log files (.log)"; Flags: unchecked
Name: "notepadalias"; Description: "Open 'notepad' with npad (Win+R and app launches; see docs for the Windows 11 Store alias)"
; Unchecked by default: out of the box npad should look like notepad.exe
; (Consolas 11), and this task rewrites settings.json to Intel One Mono /
; Roboto. The fonts are still installed and available in the pickers - this
; only decides whether they are imposed as the defaults.
; Renamed from "fontdefaults" in v0.28.5, deliberately. Inno's UsePreviousTasks
; defaults to yes, so it restores whatever was ticked on the PREVIOUS install -
; which meant anyone who enabled this before v0.27.0 made it unchecked kept
; getting it re-ticked on every upgrade, silently reapplying non-Notepad fonts.
; A task name with no history falls back to its declared Flags, so the rename
; is what actually makes "unchecked" stick. Silent installs opt in with
; /MERGETASKS="fontdefaults2".
Name: "fontdefaults2"; Description: "Set the bundled fonts as npad's default editor fonts (updates settings.json)"; Check: FontDefaultsOfferable; Flags: unchecked
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
; Icon for the Settings > Default apps entry
Root: HKA; Subkey: "Software\Platima\npad\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: "{app}\{#AppExe},0"; Flags: uninsdeletevalue

; --- "Open with" for ANY file type (always installed; purely additive) -------
; Applications\npad.exe is the shell's application registration - it makes npad
; nameable and launchable from the Open With chooser. A SupportedTypes subkey is
; deliberately NOT written: declaring it FILTERS the app out of "Open with" for
; every type it does not list, and npad wants to be offered for anything, like
; notepad.exe. NoOpenWith is likewise never written (it would suppress npad).
; None of these keys can make npad the default handler for any extension - they
; only add it to a chooser list - so they are not gated on the assoc tasks.
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "npad"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
; Offer npad for files of any type. Entries here are SUBKEYS named after the
; executable (the a/b/c + MRUList value shape belongs under Explorer\FileExts,
; not here). The parent is listed first so Inno's reverse-order uninstall
; removes it AFTER our subkey, and only if no other app also registered there.
Root: HKA; Subkey: "Software\Classes\*\OpenWithList"; ValueType: none; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\*\OpenWithList\{#AppExe}"; ValueType: none; Flags: uninsdeletekey

; Per-extension ProgIDs + associations, grouped into tasks (Text / Markdown /
; Data / Config / Logs). One ProgID per extension keeps Explorer's type names
; specific; the group task decides which set is written.
; --- Text (assoc\text) ---
; .txt
Root: HKA; Subkey: "Software\Classes\npad.txt"; ValueType: string; ValueData: "Text File"; Tasks: assoc\text; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.txt\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\text
Root: HKA; Subkey: "Software\Classes\npad.txt\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\text
Root: HKA; Subkey: "Software\Classes\.txt"; ValueType: string; ValueData: "npad.txt"; Tasks: assoc\text; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "npad.txt"; ValueData: ""; Tasks: assoc\text; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".txt"; ValueData: "npad.txt"; Tasks: assoc\text; Flags: uninsdeletevalue
; --- Markdown & documents (assoc\markdown) ---
; .md
Root: HKA; Subkey: "Software\Classes\npad.md"; ValueType: string; ValueData: "Markdown Document"; Tasks: assoc\markdown; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.md\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.md\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\.md"; ValueType: string; ValueData: "npad.md"; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "npad.md"; ValueData: ""; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".md"; ValueData: "npad.md"; Tasks: assoc\markdown; Flags: uninsdeletevalue
; .markdown
Root: HKA; Subkey: "Software\Classes\npad.markdown"; ValueType: string; ValueData: "Markdown Document"; Tasks: assoc\markdown; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.markdown\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.markdown\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\.markdown"; ValueType: string; ValueData: "npad.markdown"; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.markdown\OpenWithProgids"; ValueType: string; ValueName: "npad.markdown"; ValueData: ""; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".markdown"; ValueData: "npad.markdown"; Tasks: assoc\markdown; Flags: uninsdeletevalue
; --- Data (assoc\data) ---
; .csv
Root: HKA; Subkey: "Software\Classes\npad.csv"; ValueType: string; ValueData: "CSV File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.csv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.csv\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.csv"; ValueType: string; ValueData: "npad.csv"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.csv\OpenWithProgids"; ValueType: string; ValueName: "npad.csv"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".csv"; ValueData: "npad.csv"; Tasks: assoc\data; Flags: uninsdeletevalue
; .tsv
Root: HKA; Subkey: "Software\Classes\npad.tsv"; ValueType: string; ValueData: "TSV File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.tsv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.tsv\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.tsv"; ValueType: string; ValueData: "npad.tsv"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.tsv\OpenWithProgids"; ValueType: string; ValueName: "npad.tsv"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tsv"; ValueData: "npad.tsv"; Tasks: assoc\data; Flags: uninsdeletevalue
; .json
Root: HKA; Subkey: "Software\Classes\npad.json"; ValueType: string; ValueData: "JSON File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.json\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.json\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.json"; ValueType: string; ValueData: "npad.json"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.json\OpenWithProgids"; ValueType: string; ValueName: "npad.json"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".json"; ValueData: "npad.json"; Tasks: assoc\data; Flags: uninsdeletevalue
; .xml
Root: HKA; Subkey: "Software\Classes\npad.xml"; ValueType: string; ValueData: "XML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.xml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.xml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.xml"; ValueType: string; ValueData: "npad.xml"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.xml\OpenWithProgids"; ValueType: string; ValueName: "npad.xml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xml"; ValueData: "npad.xml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .yaml
Root: HKA; Subkey: "Software\Classes\npad.yaml"; ValueType: string; ValueData: "YAML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yaml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yaml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.yaml"; ValueType: string; ValueData: "npad.yaml"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.yaml\OpenWithProgids"; ValueType: string; ValueName: "npad.yaml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".yaml"; ValueData: "npad.yaml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .yml
Root: HKA; Subkey: "Software\Classes\npad.yml"; ValueType: string; ValueData: "YAML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.yml"; ValueType: string; ValueData: "npad.yml"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.yml\OpenWithProgids"; ValueType: string; ValueName: "npad.yml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".yml"; ValueData: "npad.yml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .toml
Root: HKA; Subkey: "Software\Classes\npad.toml"; ValueType: string; ValueData: "TOML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.toml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.toml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.toml"; ValueType: string; ValueData: "npad.toml"; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.toml\OpenWithProgids"; ValueType: string; ValueName: "npad.toml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".toml"; ValueData: "npad.toml"; Tasks: assoc\data; Flags: uninsdeletevalue
; --- Config (assoc\config) ---
; .ini
Root: HKA; Subkey: "Software\Classes\npad.ini"; ValueType: string; ValueData: "INI Configuration File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.ini\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.ini\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.ini"; ValueType: string; ValueData: "npad.ini"; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.ini\OpenWithProgids"; ValueType: string; ValueName: "npad.ini"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ini"; ValueData: "npad.ini"; Tasks: assoc\config; Flags: uninsdeletevalue
; .cfg
Root: HKA; Subkey: "Software\Classes\npad.cfg"; ValueType: string; ValueData: "Config File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.cfg\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.cfg\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.cfg"; ValueType: string; ValueData: "npad.cfg"; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.cfg\OpenWithProgids"; ValueType: string; ValueName: "npad.cfg"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".cfg"; ValueData: "npad.cfg"; Tasks: assoc\config; Flags: uninsdeletevalue
; .conf
Root: HKA; Subkey: "Software\Classes\npad.conf"; ValueType: string; ValueData: "Config File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.conf\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.conf\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.conf"; ValueType: string; ValueData: "npad.conf"; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.conf\OpenWithProgids"; ValueType: string; ValueName: "npad.conf"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".conf"; ValueData: "npad.conf"; Tasks: assoc\config; Flags: uninsdeletevalue
; --- Logs (assoc\log) ---
; .log
Root: HKA; Subkey: "Software\Classes\npad.log"; ValueType: string; ValueData: "Log File"; Tasks: assoc\log; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.log\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\npad.log\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\.log"; ValueType: string; ValueData: "npad.log"; Tasks: assoc\log; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.log\OpenWithProgids"; ValueType: string; ValueName: "npad.log"; ValueData: ""; Tasks: assoc\log; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".log"; ValueData: "npad.log"; Tasks: assoc\log; Flags: uninsdeletevalue

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
; Only offered when setup actually had to close a running npad - see
; NpadWasRunning. RestartApplications=no above means the Restart Manager will
; not silently relaunch it behind this checkbox, so exactly one instance starts
; and only if the user leaves the box ticked.
Filename: "{app}\{#AppExe}"; Description: "Relaunch npad"; Flags: postinstall nowait skipifsilent; Check: NpadWasRunning
; Windows will not let installers set the default handler programmatically
Filename: "ms-settings:defaultapps"; Description: "Open Default Apps settings (make npad the default editor)"; Flags: postinstall shellexec skipifsilent unchecked
; The Windows 11 Store Notepad alias must be disabled by hand:
; Apps > Advanced app settings > App execution aliases > Notepad (off).
; Only offered when that alias stub actually exists on this machine -
; without it npad's App Paths entry already owns 'notepad' everywhere.
Filename: "ms-settings:advanced-apps"; Description: "Open Settings to disable the Windows 11 Notepad alias (App execution aliases)"; Flags: postinstall shellexec skipifsilent; Check: ShouldOfferAliasSettings

[Code]
// --- Was npad already running when setup started? -------------------------
// The relaunch checkbox should only appear when setup actually interrupted a
// running instance - offering to start an app the user was not using is noise.
// Recorded in InitializeSetup, i.e. before the Preparing to Install page
// closes anything, so it reflects the state we found.
//
// Two ways in, because window detection alone misses the important case:
//   * a window exists  -> the user launched setup while npad was open
//   * /RELAUNCH=1      -> npad launched setup ITSELF for an in-app update and
//                         then closed, so by now there is no window to find
var
  GNpadWasRunning: Boolean;

function InitializeSetup(): Boolean;
begin
  GNpadWasRunning := (FindWindowByClassName('NpadMainWindow') <> 0) or
                     (ExpandConstant('{param:RELAUNCH|0}') = '1');
  Result := True;
end;

function NpadWasRunning(): Boolean;
begin
  Result := GNpadWasRunning;
end;

// The App-execution-aliases Settings page is only useful when the Windows
// 11 Store Notepad has planted its 'notepad.exe' alias stub, which can
// still win over npad's App Paths entry in some contexts (e.g. cmd.exe
// PATH lookup). Installers cannot disable that stub programmatically.
function ShouldOfferAliasSettings(): Boolean;
begin
  Result := WizardIsTaskSelected('notepadalias') and
            FileExists(ExpandConstant('{localappdata}\Microsoft\WindowsApps\notepad.exe'));
end;

// --- PATH entry ('addtopath' task) ---------------------------------------
// App Paths only serves Win+R / ShellExecute; cmd.exe and PowerShell resolve
// bare 'npad' from PATH alone, so the install dir must be added there for the
// user's real need ('npad' from a terminal). Per-user install edits HKCU\
// Environment; an admin (all-users) install edits the machine PATH. Inno's
// ChangesEnvironment=yes broadcasts WM_SETTINGCHANGE at the end so new shells
// pick it up without a sign-out. (RegQueryStringValue returns REG_EXPAND_SZ
// data unexpanded, so writing it straight back does not resolve %VARS%.)

function EnvPathRootKey(): Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function EnvPathSubKey(): String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

procedure EnvAddPath(const Dir: String);
var
  Paths: String;
begin
  if not RegQueryStringValue(EnvPathRootKey(), EnvPathSubKey(), 'Path', Paths) then
    Paths := '';
  // Already present (delimited, case-insensitive)? Nothing to do.
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    exit;
  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Dir + ';';
  RegWriteExpandStringValue(EnvPathRootKey(), EnvPathSubKey(), 'Path', Paths);
end;

procedure EnvRemovePath(const Dir: String);
var
  Paths: String;
  P: Integer;
begin
  if not RegQueryStringValue(EnvPathRootKey(), EnvPathSubKey(), 'Path', Paths) then
    exit;
  P := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';');
  if P = 0 then
    exit;
  if P > 1 then
    Dec(P); // account for the artificial leading ';'
  Delete(Paths, P, Length(Dir) + 1);
  RegWriteExpandStringValue(EnvPathRootKey(), EnvPathSubKey(), 'Path', Paths);
end;

// --- Default-font preset ('fontdefaults2' task) --------------------------
// A font family counts as available when its component is being installed
// OR it is already present on the system (user or machine font store).

function FontFilePresent(const FileName: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{localappdata}\Microsoft\Windows\Fonts\') + FileName) or
            FileExists(ExpandConstant('{win}\Fonts\') + FileName);
end;

function MonoFontAvailable(): Boolean;
begin
  Result := WizardIsComponentSelected('fonts\intelonemono') or
            FontFilePresent('IntelOneMono-Regular.ttf');
end;

function PropFontAvailable(): Boolean;
begin
  Result := WizardIsComponentSelected('fonts\roboto') or
            FontFilePresent('Roboto-Regular.ttf');
end;

function FontDefaultsOfferable(): Boolean;
begin
  Result := MonoFontAvailable() or PropFontAvailable();
end;

// Set "Key": "Value" in the flat JSON string npad uses for settings.json:
// replace the value when the key exists, append before the closing brace
// otherwise. String-level and whitespace-tolerant, matching npad's parser.
procedure UpsertJsonString(var Json: AnsiString; const Key, Value: AnsiString);
var
  P, C, Q1, Q2, B: Integer;
begin
  P := Pos('"' + Key + '"', Json);
  if P > 0 then
  begin
    C := P + Length(Key) + 2;
    while (C <= Length(Json)) and (Json[C] <> ':') do
      C := C + 1;
    Q1 := C;
    while (Q1 <= Length(Json)) and (Json[Q1] <> '"') do
      Q1 := Q1 + 1;
    Q2 := Q1 + 1;
    while (Q2 <= Length(Json)) and (Json[Q2] <> '"') do
      if Json[Q2] = '\' then Q2 := Q2 + 2 else Q2 := Q2 + 1;
    if Q2 <= Length(Json) then
      Json := Copy(Json, 1, Q1) + Value + Copy(Json, Q2, Length(Json));
  end
  else
  begin
    B := Length(Json);
    while (B > 0) and (Json[B] <> '}') do
      B := B - 1;
    if B = 0 then
    begin
      Json := '{' + #10 + '  "' + Key + '": "' + Value + '"' + #10 + '}';
      Exit;
    end;
    if Pos('":', Json) > 0 then
      Insert(',' + #10 + '  "' + Key + '": "' + Value + '"' + #10, Json, B)
    else
      Insert(#10 + '  "' + Key + '": "' + Value + '"' + #10, Json, B);
  end;
end;

procedure ApplyFontDefaults();
var
  SettingsDir, SettingsFile: String;
  Json: AnsiString;
begin
  if not WizardIsTaskSelected('fontdefaults2') then
    Exit;
  if not (MonoFontAvailable() or PropFontAvailable()) then
    Exit;
  SettingsDir := ExpandConstant('{userappdata}\Platima\npad');
  SettingsFile := SettingsDir + '\settings.json';
  Json := '';
  if FileExists(SettingsFile) then
    LoadStringFromFile(SettingsFile, Json);
  if Trim(Json) = '' then
    Json := '{' + #10 + '}';
  if MonoFontAvailable() then
    UpsertJsonString(Json, 'monospace_font', 'Intel One Mono');
  if PropFontAvailable() then
    UpsertJsonString(Json, 'proportional_font', 'Roboto');
  if ForceDirectories(SettingsDir) then
    SaveStringToFile(SettingsFile, Json, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    ApplyFontDefaults();
    if WizardIsTaskSelected('addtopath') then
      EnvAddPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
