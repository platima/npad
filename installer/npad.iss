; npad interactive installer (Inno Setup 6)
; Build: ISCC /DAppVersion=x.y.z npad.iss   (see build-installers.ps1)
;
; Defaults to a per-user install; the dialog (or /ALLUSERS) switches to a
; system-wide install. Launching setup elevated does NOT: with
; PrivilegesRequired=lowest that is still a per-user install, into the
; elevated account's profile. Fonts are optional components; file
; associations, PATH and the notepad alias takeover are tasks; re-running
; setup with the PATH or notepad alias task unticked removes that entry.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; The version in the file name: the release tag's spelling, which for a
; pre-release (0.36.0-rc1) is not a valid AppVersion (/DFileVersion=...)
#ifndef FileVersion
  #define FileVersion AppVersion
#endif
; Target architecture, x64 or arm64 (ISCC /DArch=arm64), and the npad.exe built
; for it (/DNpadExe=<path>). One AppId for both: installing the other
; architecture's setup over an install replaces it in place.
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef NpadExe
  #define NpadExe "..\npad.exe"
#endif
#if Arch != "x64" && Arch != "arm64"
  #error Arch must be x64 or arm64
#endif
#define AppName "npad"
#define AppPublisher "Platima"
#define AppURL "https://npad.dev"
#define RepoURL "https://github.com/platima/npad"
#define AppExe "npad.exe"

[Setup]
AppId={{B7E5A2C4-9D31-4F8E-A6C0-3D2E71540A9B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#RepoURL}/issues
AppUpdatesURL={#RepoURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; Add/Remove Programs shows just "npad" - the version has its own column
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=npad-v{#FileVersion}-setup-win-{#Arch}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
#if Arch == "arm64"
; Windows on Arm only: an arm64 npad.exe cannot run anywhere else
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
; x64compatible includes Windows on Arm, which runs x64 code under emulation
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
ChangesAssociations=yes
; The 'addtopath' task edits PATH via [Code]; broadcast the change at the end
ChangesEnvironment=yes
; Close a running npad so its exe can be replaced (npad shows no save prompt:
; it parks unsaved documents for the next start, and refuses only when it
; cannot park them), but do NOT let the Restart Manager bring it back. Setup
; relaunches it itself, and only when there was something to relaunch: a
; [Run] checkbox interactively, CurStepChanged(ssPostInstall) automatically
; for a per-user silent install. DeinitializeSetup covers a cancelled or
; failed setup.
CloseApplications=yes
RestartApplications=no
; Any Windows 10 or later. Per-user fonts need 1803 (10.0.17134), but only a
; non-admin install uses them: there the font files are skipped on older
; builds (FontsInstallable) instead of refusing the whole install, so an
; all-users install still works on Windows Server 2016 / Windows 10 1607
MinVersion=10.0
LicenseFile=..\LICENSE
#ifdef Sign
; Only when the build passes /DSign=1 and defines the tool with
; /Snpadsign="<sign one file>" $f (build-installers.ps1 -SignCmd). The
; uninstaller is a separate binary and needs its own signature, or Windows
; shows an unknown-publisher prompt on uninstall alone.
SignTool=npadsign
SignedUninstaller=yes
#endif

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
; "&&": a single & is an accelerator prefix in task descriptions, and rendered
; as an underscore under the following space
Name: "assoc\markdown"; Description: "Markdown && documents (.md, .markdown)"; Flags: unchecked
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
; /MERGETASKS="fontdefaults2". Per-user installs only: an all-users setup
; runs as the elevating account, whose settings.json is not the user's (see
; FontDefaultsOfferable).
Name: "fontdefaults2"; Description: "Set the bundled fonts as npad's default editor fonts (updates settings.json)"; Check: FontDefaultsOfferable; Flags: unchecked
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#NpadExe}"; DestDir: "{app}"; DestName: "{#AppExe}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\DOCUMENTATION.md"; DestDir: "{app}"; Flags: ignoreversion

; --- Fonts: {autofonts} + FontInstall in both install modes ---------------
; All users: C:\Windows\Fonts and HKLM. Per user: the user's own font store
; (%LOCALAPPDATA%\Microsoft\Windows\Fonts, HKCU), which only Windows 10 1803+
; has: on an older build a per-user install skips the fonts (FontsInstallable)
; and still installs npad. Either way FontInstall registers the font AND loads
; it (AddFontResource + WM_FONTCHANGE), so npad and the fontdefaults2 preset
; can use it straight away. Before 0.36.0 a per-user install only copied the
; files and wrote the registry values below, and the fonts appeared after the
; next sign-out.
; OpenDyslexic is OpenType (CFF): without fontisnttruetype Inno would append
; " (TrueType)" to the name, so the name carries Windows' own "(OpenType)"
; suffix instead. That is also the value name the per-user backstop rows (and
; every pre-0.36.0 per-user install) write, so each face is registered once.
Source: "fonts\IntelOneMono\IntelOneMono-Regular.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono"; Components: fonts\intelonemono; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-Bold.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Bold"; Components: fonts\intelonemono; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-Italic.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Italic"; Components: fonts\intelonemono; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\IntelOneMono\IntelOneMono-BoldItalic.ttf"; DestDir: "{autofonts}"; FontInstall: "Intel One Mono Bold Italic"; Components: fonts\intelonemono; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Regular.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto"; Components: fonts\roboto; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Bold.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Bold"; Components: fonts\roboto; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-Italic.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Italic"; Components: fonts\roboto; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Roboto\Roboto-BoldItalic.ttf"; DestDir: "{autofonts}"; FontInstall: "Roboto Bold Italic"; Components: fonts\roboto; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\OpenDyslexic\OpenDyslexic-Regular.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic (OpenType)"; Components: fonts\opendyslexic; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall fontisnttruetype
Source: "fonts\OpenDyslexic\OpenDyslexic-Bold.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Bold (OpenType)"; Components: fonts\opendyslexic; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall fontisnttruetype
Source: "fonts\OpenDyslexic\OpenDyslexic-Italic.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Italic (OpenType)"; Components: fonts\opendyslexic; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall fontisnttruetype
Source: "fonts\OpenDyslexic\OpenDyslexic-BoldItalic.otf"; DestDir: "{autofonts}"; FontInstall: "OpenDyslexic Bold Italic (OpenType)"; Components: fonts\opendyslexic; Check: FontsInstallable; Flags: onlyifdoesntexist uninsneveruninstall fontisnttruetype

; Font licenses always accompany the fonts
Source: "fonts\IntelOneMono\OFL.txt"; DestDir: "{app}\licenses\IntelOneMono"; Components: fonts\intelonemono; Check: FontsInstallable; Flags: ignoreversion
Source: "fonts\Roboto\OFL.txt"; DestDir: "{app}\licenses\Roboto"; Components: fonts\roboto; Check: FontsInstallable; Flags: ignoreversion
Source: "fonts\OpenDyslexic\OFL.txt"; DestDir: "{app}\licenses\OpenDyslexic"; Components: fonts\opendyslexic; Check: FontsInstallable; Flags: ignoreversion

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
; specific; the group task decides which set is written. Each ProgID has the
; open verb and a print verb ("npad /p", as notepad's txtfile has). The
; extension's own default value is written only when no other ProgID holds it
; (ExtDefaultClaimable) and carries no uninstall flag: [Code] hands it back
; on uninstall (SnapshotExtDefaults / RestoreExtDefaults).
; --- Text (assoc\text) ---
; .txt
Root: HKA; Subkey: "Software\Classes\npad.txt"; ValueType: string; ValueData: "Text File"; Tasks: assoc\text; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.txt\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\text
Root: HKA; Subkey: "Software\Classes\npad.txt\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\text
Root: HKA; Subkey: "Software\Classes\npad.txt\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\text
Root: HKA; Subkey: "Software\Classes\.txt"; ValueType: string; ValueData: "npad.txt"; Tasks: assoc\text; Check: ExtDefaultClaimable('.txt')
Root: HKA; Subkey: "Software\Classes\.txt\OpenWithProgids"; ValueType: string; ValueName: "npad.txt"; ValueData: ""; Tasks: assoc\text; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".txt"; ValueData: "npad.txt"; Tasks: assoc\text; Flags: uninsdeletevalue
; --- Markdown & documents (assoc\markdown) ---
; .md
Root: HKA; Subkey: "Software\Classes\npad.md"; ValueType: string; ValueData: "Markdown Document"; Tasks: assoc\markdown; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.md\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.md\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.md\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\.md"; ValueType: string; ValueData: "npad.md"; Tasks: assoc\markdown; Check: ExtDefaultClaimable('.md')
Root: HKA; Subkey: "Software\Classes\.md\OpenWithProgids"; ValueType: string; ValueName: "npad.md"; ValueData: ""; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".md"; ValueData: "npad.md"; Tasks: assoc\markdown; Flags: uninsdeletevalue
; .markdown
Root: HKA; Subkey: "Software\Classes\npad.markdown"; ValueType: string; ValueData: "Markdown Document"; Tasks: assoc\markdown; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.markdown\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.markdown\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\npad.markdown\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\markdown
Root: HKA; Subkey: "Software\Classes\.markdown"; ValueType: string; ValueData: "npad.markdown"; Tasks: assoc\markdown; Check: ExtDefaultClaimable('.markdown')
Root: HKA; Subkey: "Software\Classes\.markdown\OpenWithProgids"; ValueType: string; ValueName: "npad.markdown"; ValueData: ""; Tasks: assoc\markdown; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".markdown"; ValueData: "npad.markdown"; Tasks: assoc\markdown; Flags: uninsdeletevalue
; --- Data (assoc\data) ---
; .csv
Root: HKA; Subkey: "Software\Classes\npad.csv"; ValueType: string; ValueData: "CSV File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.csv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.csv\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.csv\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.csv"; ValueType: string; ValueData: "npad.csv"; Tasks: assoc\data; Check: ExtDefaultClaimable('.csv')
Root: HKA; Subkey: "Software\Classes\.csv\OpenWithProgids"; ValueType: string; ValueName: "npad.csv"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".csv"; ValueData: "npad.csv"; Tasks: assoc\data; Flags: uninsdeletevalue
; .tsv
Root: HKA; Subkey: "Software\Classes\npad.tsv"; ValueType: string; ValueData: "TSV File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.tsv\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.tsv\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.tsv\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.tsv"; ValueType: string; ValueData: "npad.tsv"; Tasks: assoc\data; Check: ExtDefaultClaimable('.tsv')
Root: HKA; Subkey: "Software\Classes\.tsv\OpenWithProgids"; ValueType: string; ValueName: "npad.tsv"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".tsv"; ValueData: "npad.tsv"; Tasks: assoc\data; Flags: uninsdeletevalue
; .json
Root: HKA; Subkey: "Software\Classes\npad.json"; ValueType: string; ValueData: "JSON File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.json\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.json\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.json\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.json"; ValueType: string; ValueData: "npad.json"; Tasks: assoc\data; Check: ExtDefaultClaimable('.json')
Root: HKA; Subkey: "Software\Classes\.json\OpenWithProgids"; ValueType: string; ValueName: "npad.json"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".json"; ValueData: "npad.json"; Tasks: assoc\data; Flags: uninsdeletevalue
; .xml
Root: HKA; Subkey: "Software\Classes\npad.xml"; ValueType: string; ValueData: "XML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.xml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.xml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.xml\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.xml"; ValueType: string; ValueData: "npad.xml"; Tasks: assoc\data; Check: ExtDefaultClaimable('.xml')
Root: HKA; Subkey: "Software\Classes\.xml\OpenWithProgids"; ValueType: string; ValueName: "npad.xml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".xml"; ValueData: "npad.xml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .yaml
Root: HKA; Subkey: "Software\Classes\npad.yaml"; ValueType: string; ValueData: "YAML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yaml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yaml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yaml\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.yaml"; ValueType: string; ValueData: "npad.yaml"; Tasks: assoc\data; Check: ExtDefaultClaimable('.yaml')
Root: HKA; Subkey: "Software\Classes\.yaml\OpenWithProgids"; ValueType: string; ValueName: "npad.yaml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".yaml"; ValueData: "npad.yaml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .yml
Root: HKA; Subkey: "Software\Classes\npad.yml"; ValueType: string; ValueData: "YAML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.yml\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.yml"; ValueType: string; ValueData: "npad.yml"; Tasks: assoc\data; Check: ExtDefaultClaimable('.yml')
Root: HKA; Subkey: "Software\Classes\.yml\OpenWithProgids"; ValueType: string; ValueName: "npad.yml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".yml"; ValueData: "npad.yml"; Tasks: assoc\data; Flags: uninsdeletevalue
; .toml
Root: HKA; Subkey: "Software\Classes\npad.toml"; ValueType: string; ValueData: "TOML File"; Tasks: assoc\data; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.toml\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.toml\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\npad.toml\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\data
Root: HKA; Subkey: "Software\Classes\.toml"; ValueType: string; ValueData: "npad.toml"; Tasks: assoc\data; Check: ExtDefaultClaimable('.toml')
Root: HKA; Subkey: "Software\Classes\.toml\OpenWithProgids"; ValueType: string; ValueName: "npad.toml"; ValueData: ""; Tasks: assoc\data; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".toml"; ValueData: "npad.toml"; Tasks: assoc\data; Flags: uninsdeletevalue
; --- Config (assoc\config) ---
; .ini
Root: HKA; Subkey: "Software\Classes\npad.ini"; ValueType: string; ValueData: "INI Configuration File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.ini\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.ini\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.ini\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.ini"; ValueType: string; ValueData: "npad.ini"; Tasks: assoc\config; Check: ExtDefaultClaimable('.ini')
Root: HKA; Subkey: "Software\Classes\.ini\OpenWithProgids"; ValueType: string; ValueName: "npad.ini"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ini"; ValueData: "npad.ini"; Tasks: assoc\config; Flags: uninsdeletevalue
; .cfg
Root: HKA; Subkey: "Software\Classes\npad.cfg"; ValueType: string; ValueData: "Config File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.cfg\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.cfg\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.cfg\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.cfg"; ValueType: string; ValueData: "npad.cfg"; Tasks: assoc\config; Check: ExtDefaultClaimable('.cfg')
Root: HKA; Subkey: "Software\Classes\.cfg\OpenWithProgids"; ValueType: string; ValueName: "npad.cfg"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".cfg"; ValueData: "npad.cfg"; Tasks: assoc\config; Flags: uninsdeletevalue
; .conf
Root: HKA; Subkey: "Software\Classes\npad.conf"; ValueType: string; ValueData: "Config File"; Tasks: assoc\config; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.conf\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.conf\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\npad.conf\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\config
Root: HKA; Subkey: "Software\Classes\.conf"; ValueType: string; ValueData: "npad.conf"; Tasks: assoc\config; Check: ExtDefaultClaimable('.conf')
Root: HKA; Subkey: "Software\Classes\.conf\OpenWithProgids"; ValueType: string; ValueName: "npad.conf"; ValueData: ""; Tasks: assoc\config; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".conf"; ValueData: "npad.conf"; Tasks: assoc\config; Flags: uninsdeletevalue
; --- Logs (assoc\log) ---
; .log
Root: HKA; Subkey: "Software\Classes\npad.log"; ValueType: string; ValueData: "Log File"; Tasks: assoc\log; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.log\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\npad.log\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\npad.log\shell\print\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" /p ""%1"""; Tasks: assoc\log
Root: HKA; Subkey: "Software\Classes\.log"; ValueType: string; ValueData: "npad.log"; Tasks: assoc\log; Check: ExtDefaultClaimable('.log')
Root: HKA; Subkey: "Software\Classes\.log\OpenWithProgids"; ValueType: string; ValueName: "npad.log"; ValueData: ""; Tasks: assoc\log; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Platima\npad\Capabilities\FileAssociations"; ValueType: string; ValueName: ".log"; ValueData: "npad.log"; Tasks: assoc\log; Flags: uninsdeletevalue
; Preferences > Associations can register groups the installer never did, so
; the uninstall log knows nothing about them. These entries create nothing
; (dontcreatekey, no value) but remove every npad ProgID on uninstall,
; whoever created it. OpenWithProgids and Capabilities entries left behind
; then name unregistered ProgIDs, which Windows ignores - its own guidance is
; not to chase them.
Root: HKA; Subkey: "Software\Classes\npad.txt"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.md"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.markdown"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.csv"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.tsv"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.json"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.xml"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yaml"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.yml"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.toml"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.ini"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.cfg"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.conf"; ValueType: none; Flags: dontcreatekey uninsdeletekey
Root: HKA; Subkey: "Software\Classes\npad.log"; ValueType: none; Flags: dontcreatekey uninsdeletekey
; On an all-users install HKA is HKLM, but Preferences > Associations writes
; the per-user hive by default. Clean the uninstalling account's per-user
; registrations as well. Other accounts' per-user entries are out of an
; uninstaller's reach; DOCUMENTATION.md says so.
Root: HKCU; Subkey: "Software\Classes\npad.txt"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.md"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.markdown"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.csv"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.tsv"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.json"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.xml"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.yaml"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.yml"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.toml"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.ini"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.cfg"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.conf"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\npad.log"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\npad.exe"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\*\OpenWithList\npad.exe"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Platima\npad\Capabilities"; ValueType: none; Check: IsAdminInstallMode; Flags: dontcreatekey uninsdeletekey

; Per-user font registration. FontInstall above writes these same value names
; (OpenDyslexic included, see fontisnttruetype there); they stay as a
; backstop for a font file that is already present, which onlyifdoesntexist
; skips together with its FontInstall. Not before Windows 10 1803, which has
; no per-user font store to register into.
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Regular.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Bold (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Bold.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-Italic.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Intel One Mono Bold Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\IntelOneMono-BoldItalic.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\intelonemono
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Regular.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Bold (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Bold.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-Italic.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "Roboto Bold Italic (TrueType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\Roboto-BoldItalic.ttf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\roboto
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Regular.otf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Bold (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Bold.otf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Italic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-Italic.otf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\opendyslexic
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\Fonts"; ValueType: string; ValueName: "OpenDyslexic Bold Italic (OpenType)"; ValueData: "{localappdata}\Microsoft\Windows\Fonts\OpenDyslexic-BoldItalic.otf"; Check: (not IsAdminInstallMode) and PerUserFontsSupported; Components: fonts\opendyslexic

[Icons]
Name: "{autoprograms}\npad"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\npad"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Only offered when setup actually had to close a running npad - see
; NpadWasRunning. RestartApplications=no above means the Restart Manager will
; not silently relaunch it behind this checkbox, so exactly one instance starts
; and only if the user leaves the box ticked.
Filename: "{app}\{#AppExe}"; Description: "Relaunch npad"; Flags: postinstall nowait skipifsilent; Check: NpadWasRunning
; A silent install has no Finished page, so the entry above never runs. That
; relaunch is not a [Run] entry: Inno runs non-postinstall entries BEFORE
; CurStepChanged(ssPostInstall), so npad would start before ApplyFontDefaults
; writes settings.json - and save its old settings over the preset on exit.
; See RelaunchSilently and CurStepChanged.
; Windows will not let installers set the default handler programmatically
Filename: "ms-settings:defaultapps"; Description: "Open Default Apps settings (make npad the default editor)"; Flags: postinstall shellexec skipifsilent unchecked
; The Windows 11 Store Notepad alias must be disabled by hand:
; Apps > Advanced app settings > App execution aliases > Notepad (off).
; Only offered when that alias stub actually exists on this machine -
; without it npad's App Paths entry already owns 'notepad' everywhere.
Filename: "ms-settings:advanced-apps"; Description: "Open Settings to disable the Windows 11 Notepad alias (App execution aliases)"; Flags: postinstall shellexec skipifsilent; Check: ShouldOfferAliasSettings

[Code]
var
  // See InitializeSetup
  GNpadWasRunning: Boolean;
  // Set at ssDone: the install finished. DeinitializeSetup relaunches npad
  // only when it did not (cancelled, aborted, failed).
  GInstallDone: Boolean;
  // Where a npad to relaunch can be found if setup stops early: the previous
  // installation's folder (known once the wizard exists), then the target
  // folder (known once installation starts)
  GPrevAppDir: String;
  GAppDir: String;
  // Each extension's default value as uninstall found it - see
  // SnapshotExtDefaults
  GExtBefore: array of String;

// The Components and Tasks check lists draw their check boxes at a raw,
// unscaled Offset of 2 and 4 pixels while the glyph itself scales with DPI.
// At 150% (ScaleX(100) = 167 with the default WizardSizePercent) the top-level
// boxes land half outside the list's left edge; the nested ones survive only
// because their indent pushes them right. Reproduced with Inno Setup 6.7.0
// and 6.7.3, in modern, classic and excludelightcontrols styles alike; fine
// at 96 DPI. Scaling the offset ourselves is the whole fix.
// RunList is the third one: the "Launch npad" boxes on the Finished page.
procedure InitializeWizard();
begin
  WizardForm.ComponentsList.Offset := ScaleX(8);
  WizardForm.TasksList.Offset := ScaleX(8);
  WizardForm.RunList.Offset := ScaleX(8);
  GPrevAppDir := WizardForm.PrevAppDir;
end;

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
function InitializeSetup(): Boolean;
begin
  GNpadWasRunning := (FindWindowByClassName('NpadMainWindow') <> 0) or
                     (ExpandConstant('{param:RELAUNCH|0}') = '1');
  GInstallDone := False;
  Result := True;
end;

function NpadWasRunning(): Boolean;
begin
  Result := GNpadWasRunning;
end;

function NpadWindowOpen(): Boolean;
begin
  Result := FindWindowByClassName('NpadMainWindow') <> 0;
end;

// The silent-install relaunch: a silent install has no Finished page, so bring
// npad back without asking, which restores the windows it parked when setup
// closed it. Done at the very end of CurStepChanged(ssPostInstall), after
// settings.json, PATH and the notepad alias are final. Never in administrative
// install mode: that process is an administrator, or SYSTEM under Intune and
// SCCM, and a npad started from it would run as that account, on its profile.
function RelaunchSilently(): Boolean;
begin
  Result := WizardSilent and (not IsAdminInstallMode) and GNpadWasRunning and
            (not NpadWindowOpen());
end;

// Setup closes npad (Install Now closes it before the wizard even appears,
// the Preparing page otherwise) but the only relaunch is on the Finished
// page. A cancelled wizard, an abort, or /SUPPRESSMSGBOXES answering a
// locked-file prompt with Abort would leave the user with no npad and their
// parked windows waiting for some later launch. Start whichever npad.exe is
// there - the old one if nothing was replaced - which restores those windows.
// As the original (unelevated) user, and never for a silent all-users run.
procedure DeinitializeSetup();
var
  Exe: String;
  ResultCode: Integer;
begin
  if GInstallDone or (not GNpadWasRunning) then
    exit;
  if WizardSilent and IsAdminInstallMode then
    exit;
  // Setup never got as far as closing it (cancelled on an early page)
  if NpadWindowOpen() then
    exit;
  Exe := '';
  if GAppDir <> '' then
    Exe := AddBackslash(GAppDir) + '{#AppExe}';
  if ((Exe = '') or (not FileExists(Exe))) and (GPrevAppDir <> '') then
    Exe := AddBackslash(GPrevAppDir) + '{#AppExe}';
  if (Exe <> '') and FileExists(Exe) then
    ExecAsOriginalUser(Exe, '', '', SW_SHOWNORMAL, ewNoWait, ResultCode);
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

// The hive this installation writes: HKA, as [Code] has to spell it
function ScopeRoot(): Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

// --- 'notepadalias' task, turned off --------------------------------------
// UsePreviousTasks offers the alias ticked again on every re-run, and the
// [Registry] entry only ever writes. Unticking it must take it away, or the
// only remedies are uninstalling or editing the registry. Only an alias that
// points at npad.exe is ours to remove.
procedure RemoveNotepadAlias();
var
  Key, Target: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\App Paths\notepad.exe';
  if not RegQueryStringValue(ScopeRoot(), Key, '', Target) then
    exit;
  if CompareText(ExtractFileName(RemoveQuotes(Target)), '{#AppExe}') = 0 then
    RegDeleteKeyIncludingSubkeys(ScopeRoot(), Key);
end;

// --- Extension default values (the assoc\* tasks) -------------------------
// Setting Software\Classes\.txt's default value is what makes npad the type's
// handler where Windows still lets a registration decide. Machine-wide that
// value is Windows' own (txtfile, inifile, xmlfile) or another app's
// (Excel.CSV), and the uninstall log could only delete it, never put it back:
// an all-users uninstall used to leave .txt with no type name, no icon and no
// New > Text Document. So setup claims an extension only when nothing else
// holds it, and uninstall hands back exactly what it found (below). npad is
// offered for the type either way, through OpenWithProgids and Capabilities.
function ExtDefaultClaimable(const Ext: String): Boolean;
var
  Current: String;
begin
  if not RegQueryStringValue(ScopeRoot(), 'Software\Classes\' + Ext, '', Current) then
    Current := '';
  Result := (Current = '') or (CompareText(Current, 'npad' + Ext) = 0);
end;

const
  AssocExtCount = 14;

function AssocExt(I: Integer): String;
begin
  case I of
    0: Result := '.txt';
    1: Result := '.md';
    2: Result := '.markdown';
    3: Result := '.csv';
    4: Result := '.tsv';
    5: Result := '.json';
    6: Result := '.xml';
    7: Result := '.yaml';
    8: Result := '.yml';
    9: Result := '.toml';
    10: Result := '.ini';
    11: Result := '.cfg';
    12: Result := '.conf';
    13: Result := '.log';
  else
    Result := '';
  end;
end;

// The ProgID Windows ships as the machine-wide default, matching
// src/platform/assoc_win32.c's table
function StockProgId(const Ext: String): String;
begin
  if (Ext = '.txt') or (Ext = '.log') then
    Result := 'txtfile'
  else if Ext = '.ini' then
    Result := 'inifile'
  else if Ext = '.xml' then
    Result := 'xmlfile'
  else
    Result := '';
end;

// Before the uninstall log runs. Logs written by installers before 0.36.0
// still carry an unconditional uninsdeletevalue for every extension, and an
// upgrade keeps those records, so the log may delete a value npad does not
// own; RestoreExtDefaults puts it back afterwards.
procedure SnapshotExtDefaults();
var
  I: Integer;
  V: String;
begin
  SetArrayLength(GExtBefore, AssocExtCount);
  for I := 0 to AssocExtCount - 1 do
  begin
    if not RegQueryStringValue(ScopeRoot(), 'Software\Classes\' + AssocExt(I), '', V) then
      V := '';
    GExtBefore[I] := V;
  end;
end;

// After the uninstall log has run
procedure RestoreExtDefaults();
var
  I: Integer;
  Ext, Key, Ours, Cur, Stock: String;
begin
  for I := 0 to GetArrayLength(GExtBefore) - 1 do
  begin
    Ext := AssocExt(I);
    Key := 'Software\Classes\' + Ext;
    Ours := 'npad' + Ext;
    if not RegQueryStringValue(ScopeRoot(), Key, '', Cur) then
      Cur := '';
    if CompareText(GExtBefore[I], Ours) = 0 then
    begin
      // npad's own claim: give the type back. Per-user, deleting it is the
      // restore (HKCU only overlays HKLM). Machine-wide, an older installer
      // may have overwritten Windows' own ProgID, so put that back when
      // Windows still has it.
      if (Cur = '') or (CompareText(Cur, Ours) = 0) then
      begin
        Stock := StockProgId(Ext);
        if IsAdminInstallMode and (Stock <> '') and
           RegKeyExists(HKEY_LOCAL_MACHINE, 'Software\Classes\' + Stock) then
          RegWriteStringValue(HKEY_LOCAL_MACHINE, Key, '', Stock)
        else if Cur <> '' then
          RegDeleteValue(ScopeRoot(), Key, '');
      end;
    end
    else if (GExtBefore[I] <> '') and (Cur = '') then
      // Another app's claim, deleted by an older npad's uninstall record
      RegWriteStringValue(ScopeRoot(), Key, '', GExtBefore[I]);
  end;
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

// Windows 10 1803 (build 17134) introduced the per-user font store that a
// non-admin install's {autofonts} and FontInstall use
function PerUserFontsSupported(): Boolean;
var
  Version: TWindowsVersion;
begin
  GetWindowsVersionEx(Version);
  Result := (Version.Major > 10) or
            ((Version.Major = 10) and (Version.Build >= 17134));
end;

// The font [Files] entries' Check: always for all users (C:\Windows\Fonts),
// per user only where the per-user font store exists. Gating the fonts alone
// keeps MinVersion at 10.0 for the rest of the install.
function FontsInstallable(): Boolean;
begin
  Result := IsAdminInstallMode or PerUserFontsSupported();
end;

function FontFilePresent(const FileName: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{localappdata}\Microsoft\Windows\Fonts\') + FileName) or
            FileExists(ExpandConstant('{win}\Fonts\') + FileName);
end;

function MonoFontAvailable(): Boolean;
begin
  Result := (WizardIsComponentSelected('fonts\intelonemono') and FontsInstallable()) or
            FontFilePresent('IntelOneMono-Regular.ttf');
end;

function PropFontAvailable(): Boolean;
begin
  Result := (WizardIsComponentSelected('fonts\roboto') and FontsInstallable()) or
            FontFilePresent('Roboto-Regular.ttf');
end;

// Per-user installs only. An all-users install relaunches setup elevated, and
// under over-the-shoulder UAC that process is the ADMINISTRATOR: {userappdata}
// would be their settings.json, so the user who ticked the box would still see
// Consolas while the admin's npad changed font. The fonts themselves are still
// installed for everyone and can be picked in Preferences.
function FontDefaultsOfferable(): Boolean;
begin
  Result := (not IsAdminInstallMode) and (MonoFontAvailable() or PropFontAvailable());
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
  if not FontDefaultsOfferable() then
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
var
  ResultCode: Integer;
begin
  if CurStep = ssInstall then
    GAppDir := ExpandConstant('{app}')
  else if CurStep = ssPostInstall then
  begin
    ApplyFontDefaults();
    // Both tasks' state is the state wanted: re-running setup with one
    // unticked removes what an earlier run added
    if WizardIsTaskSelected('addtopath') then
      EnvAddPath(ExpandConstant('{app}'))
    else
      EnvRemovePath(ExpandConstant('{app}'));
    if not WizardIsTaskSelected('notepadalias') then
      RemoveNotepadAlias();
    // Last, so the relaunched npad reads the final settings.json
    if RelaunchSilently() then
      ExecAsOriginalUser(ExpandConstant('{app}\{#AppExe}'), '', '', SW_SHOWNORMAL,
                         ewNoWait, ResultCode);
  end
  else if CurStep = ssDone then
    GInstallDone := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    EnvRemovePath(ExpandConstant('{app}'));
    SnapshotExtDefaults();
  end
  else if CurUninstallStep = usPostUninstall then
    RestoreExtDefaults();
end;
