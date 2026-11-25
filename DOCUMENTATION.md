# npad Documentation

Reference for npad's settings, keyboard shortcuts, status bar, command line
and behaviour. For per-release notes see [CHANGELOG.md](CHANGELOG.md); the raw
commit history lives in `git log`.

**Defaults principle**: out of the box npad mimics Windows 10 notepad.exe.
Non-destructive enhancements that don't change that core behaviour (crash
recovery, status bar, recent files, atomic saves) are on by default;
anything that alters or could destroy it (auto-save, system colour scheme,
Markdown list tools) is opt-in. The name is always lowercase: **npad**.

## Installation (Windows)

Three ways to run npad; all default to the current user and need no admin
rights:

| Artifact | Use |
|----------|-----|
| `npad-v<v>-setup-win-<arch>.exe` | Interactive installer (Inno Setup) |
| `npad-v<v>-msi-win-<arch>.msi` | Silent/managed deployment (`msiexec`) |
| `npad-v<v>-portable-win-<arch>.exe` | Portable - run from anywhere, no install |

`<arch>` is **`x64`** for ordinary Intel/AMD PCs and **`arm64`** for Windows
on Arm (Snapdragon and other Arm laptops). The arm64 builds are native and
install only on Windows on Arm; the x64 ones also run there, under emulation.
Both architectures install to the same place under the same identity, so
installing one over the other replaces it. The in-app updater downloads the
installer for the architecture npad was built for, so an arm64 npad stays
arm64 (and an x64 npad under emulation stays x64 until you install the arm64
one).

**Use one installer per machine.** The setup EXE and the MSI are separate
products, each with its own entry in Installed apps, but in the same scope
they install to the same folder and write the same App Paths, alias, PATH and
file-type keys. Installing both would list npad twice, and uninstalling
either one would remove the program and the entries the other still points
at, so each installer refuses to install while the other's installation is
there:
- **Setup EXE**: finds an npad MSI through Windows Installer (by the MSI's
  UpgradeCode, per-user for the account running setup or all-users), says
  so and exits without changing anything. A silent run (`/SILENT`,
  `/VERYSILENT`) shows no message: it writes the reason to its log
  (`/LOG=<file>`) and exits with code 1.
- **MSI**: finds the setup EXE's installation by its uninstall key (HKCU and
  HKLM), says so and installs nothing. A quiet `msiexec /qn` shows no message:
  it ends with exit code 1603 and writes the reason to its log (`/l*v
  <file>`). Repairing or removing an MSI install is never blocked.

To switch, uninstall the one you have first (Settings > Apps > Installed
apps). Neither can see another account's per-user installation. (A per-user
and an all-users installation of the same installer can also exist side by
side; setup then asks which of them to upgrade.) The in-app updater always
updates a copy with the installer that installed it - see [Updates](#updates-preferences--updates---all-opt-in-off-by-default).

**SmartScreen / Defender**: releases from v0.32.4 are code-signed (Azure
Artifact Signing; check Properties > Digital Signatures). A publisher's
SmartScreen reputation still builds over its first signed releases, so Windows
may block or warn about the downloaded installer or portable exe. Fix:
right-click the file → **Properties** → tick **Unblock** → **Apply** (this
clears the mark-of-the-web), or choose "More info" → "Run anyway" on the
SmartScreen prompt. When unsure, check the file's SHA-256 before unblocking:
`Get-FileHash <file>` in PowerShell (or `certutil -hashfile <file> SHA256`)
must match the file's line in the release's `CHECKSUMS.txt` (or its own
`<file>.sha256`).

**Install scope**: both installers default to a **per-user** install
(`%LOCALAPPDATA%\Programs\npad`, HKCU registry), whoever runs them. An
all-users install (`Program Files`, HKLM; needs administrator rights) has to
be asked for:
- **Setup EXE**: choose the all-users option in its install-mode dialog, or
  pass `/ALLUSERS` (`/CURRENTUSER` forces per-user). Running setup elevated
  is **not** enough: without `/ALLUSERS` it is still a per-user install, into
  the elevated account's own profile (SYSTEM's, under Intune or SCCM).
- **MSI**: `ALLUSERS=1`, or the equivalent `ALLUSERS=2 MSIINSTALLPERUSER=""`.
  Both give the machine PATH and the bundled fonts. Without either, the MSI
  installs per-user even when `msiexec` runs elevated.

Setup runs on Windows 10 and later. Fonts installed for one user are
registered and loaded during setup, so they work without signing out; a
per-user install on a build older than 1803, which has no per-user font
store, leaves the fonts out and installs npad anyway.

**Setup EXE options** (interactive, or `/VERYSILENT /SUPPRESSMSGBOXES`). The
names to pass on the command line:

| Name | Kind | Default | What it is |
|------|------|---------|------------|
| `full` | type (`/TYPE=`) | selected | npad and all the bundled fonts |
| `minimal` | type | | npad only |
| `main` | component | always | npad itself |
| `fonts\intelonemono`, `fonts\roboto`, `fonts\opendyslexic` | components | on (type `full`) | Intel One Mono (monospace), Roboto (proportional), OpenDyslexic (reading assistance); all SIL OFL licensed, under the parent `fonts` |
| `addtopath` | task | on | Add npad to the PATH (see below) |
| `assoc\text` | task | on | Text files (`.txt`) |
| `assoc\markdown` | task | off | Markdown & documents (`.md`, `.markdown`) |
| `assoc\data` | task | off | Data files (`.csv`, `.tsv`, `.json`, `.xml`, `.yaml`, `.yml`, `.toml`) |
| `assoc\config` | task | off | Config files (`.ini`, `.cfg`, `.conf`) |
| `assoc\log` | task | off | Log files (`.log`) |
| `notepadalias` | task | on | Open 'notepad' with npad (see below) |
| `fontdefaults2` | task | off | Set the bundled fonts as npad's default editor fonts |
| `desktopicon` | task | off | Desktop shortcut |

- **`/MERGETASKS="..."` changes only the tasks it names**, on top of the
  defaults (and, on an upgrade, on top of what the previous install chose,
  which setup remembers); a `!` in front turns a task off. **`/TASKS="..."`
  selects exactly the tasks it lists and turns every other one off**, so
  `/TASKS="assoc\data"` also drops the PATH entry, the alias and the `.txt`
  association. A name setup does not know is ignored without a word. Examples:
  `/VERYSILENT /SUPPRESSMSGBOXES /MERGETASKS="!notepadalias,!assoc\text"` (no
  alias, no `.txt` association); `/VERYSILENT /SUPPRESSMSGBOXES /TYPE=minimal`
  (no fonts); `/VERYSILENT /SUPPRESSMSGBOXES /ALLUSERS
  /MERGETASKS="assoc\data"` (all users, Data files as well).
- **Re-running setup with `addtopath` or `notepadalias` unticked removes**
  that PATH entry, or the alias (only while it still points at `npad.exe`).
  Unticking an association group does not unregister it: use
  **Preferences > Associations** for that.
- **`fontdefaults2` is off by default** - out of the box npad should look
  like notepad.exe, and this rewrites the editor fonts. It is offered only for
  a per-user install (an all-users setup runs as the administrator, whose
  settings are not yours; pick the fonts in Preferences > Appearance instead),
  and only when the fonts are being installed or already present. It updates
  `settings.json` in place, keeping every other setting. Tick it, or pass
  `/MERGETASKS="fontdefaults2"`. The task was renamed in v0.28.5 so that
  setup's memory of the previous install's tasks could no longer keep
  re-ticking it.
- **A silent install closes running npad windows** (their unsaved work is
  parked - see *Installing over a running npad* below). A per-user silent
  install starts npad again when it finishes, which brings those windows
  back. An all-users silent install (`/ALLUSERS`, run as an administrator or
  SYSTEM) does not start npad, since it would run as that account; the
  documents come back the next time the user starts npad.

**MSI features** (silent-deployment oriented, no UI - use `/qn` or `/qb`,
always with `/norestart`):

| Feature | Default | What it installs |
|---------|---------|------------------|
| `Main` | on | npad itself, its Start menu shortcut, App Paths and the Default apps registration |
| `PathEnv` | on | The install folder on PATH (see below) |
| `OpenWith` | on | npad in **Open with** for any file type (see below) |
| `AssocText` | on | Text files (`.txt`) |
| `NotepadAlias` | on | Open 'notepad' with npad (see below) |
| `Fonts` | on, all-users only | The bundled fonts. Skipped on a per-user install: MSI resolves the fonts folder to `C:\Windows\Fonts` whatever the scope (the setup EXE installs fonts per user) |
| `AssocMarkdown`, `AssocData`, `AssocConfig`, `AssocLog` | off | The other association groups, as in the setup EXE |

**`ADDLOCAL` replaces the default set; it does not add to it.** List every
feature you want, `Main` and the defaults included - a list without `Main`
does not install `npad.exe` at all. The defaults plus Data files:

```
msiexec /i npad-v<v>-msi-win-x64.msi /qn /norestart ADDLOCAL=Main,PathEnv,OpenWith,AssocText,NotepadAlias,Fonts,AssocData
```

Leave a default out of the list to skip it (`Fonts` is ignored on a per-user
install). On an installed copy, `REMOVE=NotepadAlias` or `REMOVE=PathEnv`,
run with the same `.msi`, removes just that feature.

**`/norestart`, and exit code 3010**: if Windows Installer cannot close a
running npad (an npad running as administrator during a per-user install, for
example), `npad.exe` can only be replaced at the next restart. Without
`/norestart`, a `/qn` install then restarts the computer without asking, and
every program's unsaved work goes with it. With it, `msiexec` finishes with
exit code **3010** (installed; a restart completes it) and the restart is
yours to schedule.

**npad on PATH**: App Paths (below) only serves Win+R and ShellExecute;
cmd.exe and PowerShell resolve a bare `npad` from PATH alone. The `addtopath`
task / `PathEnv` feature adds the install folder to PATH so `npad` and
`npad file.txt` work from a terminal - the user PATH for a per-user install,
the machine PATH for an all-users install. A new terminal session picks it
up; uninstall removes the entry, and so does re-running setup with the task
unticked (MSI: `REMOVE=PathEnv`).

**The 'notepad' alias**: the task points `notepad` (Win+R, ShellExecute) at
npad via the App Paths registry key - this mechanism is invisible on the
App execution aliases page, which is normal. Windows 11's Store Notepad
additionally owns a `notepad.exe` *app execution alias*, and the System32
`notepad.exe` also precedes npad on PATH, so typing bare `notepad` in a
terminal keeps launching classic Notepad; installers cannot change that.
When the Store alias stub is detected, the setup offers the Settings page
after install - turn off **Notepad** under Apps > Advanced app settings >
App execution aliases for as full a takeover as Windows allows. (Running
`npad` from a terminal is the reliable path - see **npad on PATH** above.)
To give `notepad` back, re-run setup with the task unticked, or
`REMOVE=NotepadAlias` for the MSI.

**Default editor**: Windows does not let installers set the default app for
a file type. The association tasks (setup EXE) and `Assoc*` features (MSI)
register npad for the chosen extensions, so it appears in "Open with" and
Default Apps, **and they also take each extension's default value when
nothing else holds it**: where `Software\Classes\.txt` (and so on) is empty
or already npad's, they set it to npad's ProgID (`npad.txt`, ...) - in HKCU
for a per-user install, in HKLM for an all-users one. That value is what
Windows uses for an account that has made no choice for the type in Default
apps, so for such an account the files open in npad. An extension that
Windows or another program already owns is left alone: on a stock Windows,
`.txt`, `.log`, `.ini` and `.xml` belong to Windows' own types machine-wide,
so an all-users install does not claim them, while a per-user install
usually claims `.txt` for its account (that account's own value is normally
empty). Uninstalling with the setup EXE hands every claimed type back -
per-user by removing npad's value, so the machine's shows through again;
all-users by putting Windows' own type (`txtfile`, `inifile`, `xmlfile`) back
where npad's was. The MSI removes the value it wrote. Each registered type
also gets Explorer's **Print** command, which runs `npad /p` (see
[Command line](#command-line)). The setup EXE offers the Default Apps
Settings page after install to make npad the default for .txt. To change
which types are registered later, use **Preferences > Associations** rather
than re-running setup.

**"Open with" for any file type**: separately from the association groups, the
installers always register npad as a shell application
(`Software\Classes\Applications\npad.exe` plus an entry under
`Software\Classes\*\OpenWithList`), so npad is offered in the right-click
**Open with** list for *any* file - the same way notepad.exe is - even for
extensions you did not associate. This only adds npad to a chooser list; it
never changes which application owns a file type. Two things are deliberately
omitted: a `SupportedTypes` list (declaring one *filters* an app out of
"Open with" for every type it does not list) and `PerceivedType` /
`Content Type` values (they sit on the shared extension keys, contribute
nothing to "Open with", and uninstall would delete rather than restore values
another app may have set). Suppress the whole thing with the MSI's
`OpenWith` feature if you deploy silently.

**Installing over a running npad**: both installers have npad closed so that
`npad.exe` can be replaced, through Windows' Restart Manager (the setup EXE
on its *Preparing to Install* page, or silently). npad treats that as it
treats a Windows restart: **it shows no save prompt**. Every unsaved document
is parked in a recovery slot and comes back, still unsaved, the next time npad
starts; windows with nothing unsaved are not reopened. Only if a document
cannot be parked (a full disk, a profile folder that cannot be written) does
npad refuse to close, telling Windows why; the installer then reports npad as
an application it could not close, and you can switch to npad, save, and try
again.
- **Setup EXE**: when it closed npad, the last page offers **Relaunch npad**;
  if npad was not running, no launch option is shown. Windows' Restart Manager
  is prevented from restarting it automatically, so the checkbox is the single
  place that decides (a per-user silent install relaunches it by itself, as
  above). If setup is cancelled or fails after closing npad, it starts the
  existing npad again, which restores the parked windows (not after a silent
  all-users run, which is not the user's). Setup spots a running
  npad by its window, and additionally accepts `/RELAUNCH=1` - which npad
  passes when it launches the installer for an in-app update, since by then it
  has closed itself and there is no window left to find.
- **MSI**: nothing offers to start npad afterwards. The parked documents
  return the next time npad is started.

**Uninstall** removes the program, its shortcuts and the registry entries the
installer wrote, and hands back the file types it claimed (see **Default
editor** above). It keeps:
- **Your settings folder**, `%APPDATA%\Platima\npad`: `settings.json` and the
  files beside it, and the `recovery\` folder, which can still hold snapshots
  of **unsaved text** (crash recovery, or documents parked by an update).
  Delete the folder yourself if you want none of it kept.
- **The installed fonts.**
- **Registrations out of the uninstaller's reach.** An all-users uninstall
  with the setup EXE also removes the per-user registrations that
  Preferences > Associations made - but only for the account that runs the
  uninstaller, which under an over-the-shoulder UAC prompt is the
  administrator who elevated, not the signed-in user. For that account it
  also removes the `HKCU\Software\RegisteredApplications` value `npad` (the
  Default apps entry), when it is npad's. Other accounts' per-user entries
  remain, pointing at the removed program; a `RegisteredApplications` value
  left behind names a key that no longer exists, which Windows ignores. The
  MSI does not remove groups added on Preferences > Associations (see
  [Associations](#associations-preferences--associations)).

**Portable exe**: the same program, run from wherever you put it, with
nothing installed. It keeps its settings, Recent Files and crash recovery in
the same `%APPDATA%\Platima\npad` folder as an installed npad, so the two
share them. It never updates itself: Help > Check for Updates still finds new
releases, but **Install Now** runs the setup EXE, which upgrades the installed
npad if there is one, and otherwise makes a **new per-user installation** with
setup's default choices (the PATH entry, the `.txt` association, the notepad
alias and the fonts, unless you untick them in the wizard). Setup's Relaunch
then starts the installed copy; the portable exe stays at its old version. To
update the portable copy itself, download the new portable exe. On a PC where
npad is installed from the MSI, setup refuses (see **Use one installer per
machine** above): the portable copy's unsaved documents reopen the next time
it starts. Update an MSI install from the npad it installed.

**Building the installers** (repo checkout, Windows): `pwsh
installer/build-installers.ps1` - needs Inno Setup 6, **WiX 6** (`dotnet tool
install --global wix --version 6.0.2`, the version CI uses; the script refuses
another major version and warns on another 6.x) and a built `npad.exe`; fonts
are downloaded from SHA256-pinned upstream releases by
`installer/fetch-fonts.ps1`. Add `-Arch arm64` for the Windows on Arm set,
from `npad-arm64.exe` (`make windows-arm64 LLVM_MINGW=/opt/llvm-mingw`, with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) installed there by
`scripts/install-llvm-mingw.sh`, which fetches the pinned release CI uses;
putting llvm-mingw's `bin` on `PATH` also works, appended rather than
prepended). Both architectures build on an x64 PC. CI builds all six
deliverables via `.github/workflows/installers.yml` (run by hand with
`-f version=<tag>`, which only names the files).

## File locations

| What | Where (Windows) |
|------|-----------------|
| Settings | `%APPDATA%\Platima\npad\settings.json` - the same folder for an installed npad (either installer, either scope) and the portable exe, so they share settings, Recent Files and crash recovery. Created at startup if it is missing. |
| Crash-recovery snapshots | `%APPDATA%\Platima\npad\recovery\<slot>.txt` / `.meta` (and `.handoff` for documents parked by an update or a restart). While a window restores a slot, its meta is renamed `<slot>.meta.claim-<id>`; a claim left by a window that died is handed back at the next start. |
| Update downloads | `%TEMP%\npad-update-<random>-<asset name>`; the updater deletes its own leftovers older than 7 days |

Linux: `~/.config/npad/settings.json`, with snapshots in
`~/.config/npad/recovery/`. If `$XDG_CONFIG_HOME` is set to an absolute path,
the directory is `$XDG_CONFIG_HOME/npad` instead (an empty or relative value
is ignored). When that directory has no `settings.json` yet and
`~/.config/npad/settings.json` exists, npad starts from the old settings and
writes the new file on the next save; the old file is never moved or deleted.
The settings folder and `recovery/` are private to your account (folders
0700, `settings.json`, snapshots and their companions 0600); an existing
recovery folder is tightened the next time npad looks into it.

Files that can appear beside `settings.json`:

| File | What it is |
|------|------------|
| `settings.json.lock` | Serialises saves between npad windows (each waits up to about 2 seconds for it). Windows: a hidden file that exists only during a save. Linux: an empty file left in place. Safe to delete while no npad is running. |
| `settings.json.corrupt` (`.corrupt.1`, `.2`, ...) | A copy of a `settings.json` that npad could not read in full - malformed, cut short, not JSON, over 10,000 keys, or over 8 MB (that one is moved aside rather than copied) - kept before a repaired file replaces it. An earlier copy is never overwritten; the next one takes the next number. |
| `settings.json.bak` | Your settings as they were just before the last **Import Settings** (replaced by the next import). |

Files a save can leave behind: a save writes `.npad-<8 hex>.tmp` beside the
file (hidden on Linux by its leading dot) and, on Windows, may briefly keep
`.npad-<8 hex>.bak` there. A save that has to overwrite a file in place keeps
a safety copy of the original, `npad-backup-<hex>.bak`, in the `recovery`
folder (the temp folder only if that cannot be used). All of these are
deleted when the save finishes; one that is still there was left by an
interrupted save (a crash, a power cut) and can be deleted once you have
checked the file itself. See *Atomic saves* under
[Behaviour notes](#behaviour-notes).

## Linux

The Linux desktop build (GTK 3; one binary for X11 and Wayland) has every
feature of the Windows build and is in beta while it is tested on real
desktops. It shares the settings keys and behaviour described here; its
status, and the deliberate differences (window position on Wayland,
colour schemes against a dark GTK theme, the Text Documents filter, a Check
for Updates that notifies but never installs), are tracked in
[LINUX_PORT.md](LINUX_PORT.md).

Find, Find Next / Previous, Replace, Replace All and Go To Line (Ctrl+F,
F3, Shift+F3, Ctrl+H, Ctrl+G, or a click on Ln/Col) behave as described
elsewhere in this document, with the same settings keys. Differences: the
dialogs are positioned by the window manager rather than at a fixed offset
and a remembered position, and Whole word only demands a word boundary
beside a term edge that is itself a letter, digit or underscore.

### Preferences and Font on Linux

Edit > Preferences (Ctrl+,) opens a dialog with the same pages, wording,
settings keys and defaults as the Windows build: General, Editor, Appearance,
Defaults, Markdown, Backup, Updates and Associations, plus the hidden Debug
page (Ctrl+Shift+., or Shift+click Preferences). Differences:

- **OK / Cancel / Apply** work as on Windows (see
  [Settings reference](#settings-reference)): Apply stays greyed until
  something has changed, writes only the settings you changed, and every
  other open npad window picks the change up live. As on Windows, the font
  buttons, Clear Recent Files Now, Export / Import, the xdg-mime button and
  Reset All Preferences act immediately. Reset keeps the same things as on
  Windows and closes the dialog. **Import Settings** follows the same rules as
  on Windows, but afterwards the dialog stays open with its controls reloaded
  from the imported file (Windows closes Preferences).
- **Updates checks and advises; it never installs.** See
  [Check for Updates on Linux](#check-for-updates-on-linux).
- **Associations** is one explicit action, *Make npad the default text
  editor*, which runs `xdg-mime default io.github.platima.npad.desktop
  text/plain` for your account. Nothing runs until you click it. The page
  shows which application currently handles plain text, asked in the
  background when the page is first shown, and the button greys while
  `xdg-mime` runs, so neither ever stalls the dialog.
- **No application-icon choice** on Appearance: the icon comes from the icon
  theme. The window and taskbar icon is the standard one under the Light and
  Solarized Light schemes and a dark one (`io.github.platima.npad-dark`) under
  Dark, Solarized Dark and a dark System scheme, switching live; the launcher
  keeps the standard icon.
- **Markdown** edits the shared keys the list tools read (see
  [Markdown list tools on Linux](#markdown-list-tools-on-linux)). Turning Basic
  Markdown support on also adds "Interpret escapes" to Find / Replace.
- **Debug** shows the Linux diagnostics: version, executable, GTK version and
  display backend, session type, locale, settings and recovery paths, the
  number of open windows and the startup profile, with Copy Diagnostics.
- **Format > Font...** is GTK's font chooser. It edits the font for the font
  type the window is showing (monospace, proportional or OpenDyslexic) and
  the shared size, weight and italic keys, exactly as on Windows; Appearance
  has buttons for the monospace and proportional fonts directly. The size
  is kept to a tenth of a point and stored as the Windows build stores it
  (`"11"`, `"10.5"`), so a half-point size imported from Windows shows,
  prints and saves back unchanged.

### Check for Updates on Linux

Help > Check for Updates and Preferences > Updates work as on Windows, with
the same settings keys and defaults (all off: npad makes no network request
until you ask or opt in), except that npad on Linux never downloads or
installs anything. When a newer release exists, the dialog - and the "How
this copy updates" box on the Updates page - says how to update the way this
copy was installed:

| How npad was installed | What npad suggests |
|---|---|
| From the npad apt repository (pkg.npad.dev) | Your system's updater, or `sudo apt update && sudo apt upgrade` |
| From the npad dnf repository | Your system's updater, or `sudo dnf upgrade npad` |
| From the `[npad]` pacman repository | `sudo pacman -Syu` |
| From the AUR (`npad-bin`, `npad-git`) | Your AUR helper, e.g. `yay -Syu` |
| A downloaded .deb or rpm, no npad repository | The new package from the release page, or adding the repository once: `curl -fsSL https://pkg.npad.dev/install.sh \| sudo sh` |
| An Arch package installed by hand | The new package, or the `[npad]` repository (see <https://pkg.npad.dev>) |
| The portable tarball, or built from source | The release page, <https://github.com/platima/npad/releases/latest> |

npad finds this out without root and without blocking the window: it asks
`dpkg -S`, `rpm -qf` or `pacman -Qqo` which package owns its executable (only
when it runs from `/usr/bin`), and reads the apt sources, `/etc/yum.repos.d`
or `/etc/pacman.conf` for the npad repository. The dialog offers *View the
Release Notes*, *Skip This Version* and *Close*; there is no Install Now and
no "Download and install automatically" mode (a settings file that has
`update_mode` set to `auto`, say imported from Windows, behaves as `prompt`).
The launch check runs only from the first npad window you open, since each
Linux window is its own process. The check uses GLib's TLS support, which
comes from the glib-networking package (installed on practically every
desktop); without it Help > Check for Updates says to install it (the launch
check, as on Windows, fails silently).

### Editing behaviours on Linux

These match the Windows build; the differences are noted.

- **Context menu** (right-click, Shift+F10 or the Menu key): Undo, Redo, Cut,
  Copy, Paste, Delete, Select All, greyed exactly as in the Edit menu - Undo and
  Redo by history, Cut / Copy / Delete without a selection, Paste with no text on
  the clipboard. It replaces GTK's stock text menu (no emoji or input-method
  entries).
- **Drag & drop**: drop a file on the window to open it (after the usual save
  prompt); hold **Ctrl** and its contents are inserted at the drop point
  instead, as one undo step, after the same questions as on Windows (see
  *Drag & drop* under [Behaviour notes](#behaviour-notes)). Only the first file
  of a multi-file drop is used, as on Windows. Dropped *text* is inserted as
  text. Whether Ctrl is seen while dragging depends on the desktop and on the
  file manager you drag from.
- **External changes** (`watch_file_changes`, off by default; Preferences >
  General > **Tell me when an open file is changed by another program**):
  when the window becomes active, a file changed or deleted by another program
  offers *Reload from disk*, *Save as a different file* or *Keep editing* (the
  default; Esc also keeps), with a **Stop telling me about this file** box.
  Reload discards unsaved edits, which is why it is never the default. A change
  is noticed even when it keeps the size and lands within the same second as
  npad's own save (a formatter, `sed -i`): npad compares nanosecond times and
  the file's identity.
- **Undo** keeps up to 10,000 steps or 256 MB of text, whichever comes first,
  dropping the oldest (Windows keeps 100,000). Typing is undone a word at a
  time, like gedit (Windows undoes a whole typing run at once). A paste, cut,
  Delete, Replace, Replace All, file insert, middle-click paste or text drop is
  always a step of its own, however short, so pasting "x" after typing "abc"
  undoes just the "x".
- **Plain paste** turns Windows line breaks in the clipboard into ordinary
  ones and brings none of Highlight all's colouring along. Shift+Insert is
  always a plain paste - the same as Ctrl+V while the Markdown tools are off.
- **Keys that do nothing**: Insert (there is no overtype mode, as in Notepad),
  and Ctrl+. and Ctrl+; (GTK's emoji chooser is turned off).
- **Select All** leaves the caret at the end of the document, as in Notepad.
- **Time/Date (F5)** inserts the locale's time without seconds and its short
  date with a four-digit year, as Notepad does (built from the locale's own
  formats, so `%T` and `%r` lose their seconds and `%y` becomes `%Y`).
- **File > Open** starts in the current document's folder, or for an untitled
  document in the folder of the most recent file.
- **Recent Files** shows up to ten files; the tenth opens with Alt+0. A file
  that cannot be reached at the moment (an unmounted drive, a sleeping NAS) is
  left out of the menu but kept in the list, and returns when it can be
  reached. (Windows lists every entry and reports a missing one when you pick
  it.)
- **Input methods** (CJK input, compose keys) get Enter and Tab first, so
  confirming a composition never continues a Markdown list or inserts spaces.
- **File names that are not valid UTF-8** are shown readably in the title bar,
  Recent Files, prompts and printouts, and Save As keeps such a name
  byte for byte.
- **Logging out, shutting down or restarting keeps your unsaved work**, as it
  does on Windows: npad parks every unsaved document as a handoff snapshot,
  without a prompt, and restores it silently the next time it starts. That
  covers a logout through the desktop's session manager (GNOME, Cinnamon,
  MATE and Budgie through `org.gnome.SessionManager`, and Xfce), a shutdown or
  reboot (npad holds a logind *delay* lock, listed by `systemd-inhibit --list`
  as npad, until its work is parked), SIGTERM, closing the terminal npad was
  started from (SIGHUP), Ctrl+C in that terminal (SIGINT) and, on X11, the
  display going away. It happens even with crash recovery turned off. A window
  closed during a logout parks its work instead of asking to save. npad
  started with `nohup` or in the background of a script keeps ignoring SIGHUP
  and SIGINT, as it was started. Wayland gives no warning when the display
  goes, and KDE Plasma's logout reaches npad only as SIGTERM (or, on X11, as
  the display going away).
- **Ctrl + mouse wheel** zooms in 10% steps, like Ctrl+Plus / Ctrl+Minus. High
  resolution wheels and touchpads send many small steps; they are added up,
  one zoom step per whole notch, so a swipe is not a runaway.
- **Scroll bars** are always shown and greyed out when the text fits, as in
  Notepad, instead of appearing and disappearing (GTK's overlay scroll bars are
  turned off for the text area). With Word Wrap on there is no horizontal bar.

### Markdown list tools on Linux

Everything under *Markdown list tools* in the behaviour notes below applies,
with the same settings keys, defaults and rules, and none of it exists until
**Basic Markdown support** (`list_tools_enabled`, Preferences > Markdown) is on.
Switching it on or off takes effect at once in every open window: the
**Markdown** menu appears between Format and View (or goes), and Tab, Enter,
Ctrl+X and the paste shortcuts change behaviour. Linux specifics:

- **Menu and right-click.** Markdown > Sort (Ascending, Descending, Case
  Sensitive), Unique, Convert Delimiters..., Indent (with its formats and
  Custom...), Unindent and Paste as Markdown; the right-click menu repeats them
  after Select All. They are greyed while the document is empty (Paste as
  Markdown while the clipboard is). Cut stays available with nothing selected,
  because Ctrl+X then cuts the line.
- **Keys.** Tab and Shift+Tab, or Ctrl+] and Ctrl+[ when
  `list_indent_shortcut_brackets` is on (the menu shows whichever is active;
  Ctrl+[ and Ctrl+] need a keyboard layout where the brackets are typed without
  AltGr). Enter and Ctrl+X behave as on Windows.
- **Cut line / paste above** remembers the cut line, and pastes it above while
  the clipboard still holds exactly that text. (Windows compares clipboard
  sequence numbers; Linux has none.)
- **Rich paste** reads the clipboard's `text/html`, which sources offer as
  UTF-8 or UTF-16; Ctrl+V uses `markdown_paste_primary` (default Lists) and
  Ctrl+Shift+V `markdown_paste_alt` (default Plain), both plain while the tools
  are off.
- **Dialogs.** Convert Delimiters and Custom Indent are plain GTK dialogs with
  the Windows wording; the selection you started from survives them.
- Every transform is one undo step, and a transform that changes nothing is not
  an edit (the document stays unmodified).

### Installing on Linux

Packages are built for three architectures. The .deb spells them differently
from the .rpm and the Arch package:

| Architecture | .deb | .rpm / Arch | Oldest supported |
|--------------|------|-------------|------------------|
| x86-64 (Intel/AMD) | `amd64` | `x86_64` | Debian 12, Ubuntu 22.04 |
| 64-bit Arm (Raspberry Pi 4/5 on a 64-bit OS, Arm laptops and servers) | `arm64` | `aarch64` | Debian 12, Ubuntu 22.04 |
| 64-bit RISC-V | `riscv64` | `riscv64` | Debian 13, Ubuntu 24.04 |

riscv64 starts later because no older Debian or Ubuntu has an official
riscv64 port. `dpkg --print-architecture` (or `uname -m`) tells you which one
you have; the apt, dnf and pacman repositories pick the right one by
themselves. Arch Linux itself is x86_64 only: aarch64 is [Arch Linux
ARM](https://archlinuxarm.org) and riscv64 the [Arch RISC-V
port](https://riscv.mirror.pkgbuild.com), and the pacman packages and
repository serve all three. Arch is rolling, so it has no "oldest supported"
release.

To check a download from the release page, save the release's
`CHECKSUMS.txt` beside it and run `sha256sum -c --ignore-missing
CHECKSUMS.txt` in that folder: it checks every file you downloaded and skips
the ones you did not.

- **.deb** (Debian 12/13, Ubuntu 22.04+, Mint): `sudo apt install
  ./npad_<version>_amd64.deb` (or `_arm64` / `_riscv64`). It depends only on GTK 3 (`libgtk-3-0` or
  `libgtk-3-0t64`) and installs `/usr/bin/npad`, the launcher
  (`io.github.platima.npad.desktop`, offered for text, Markdown, JSON, XML and
  similar files in "Open with"), hicolor icons from 16 to 256 px (standard
  and dark) and the AppStream metainfo for software centres.
- **.rpm** (Fedora, openSUSE): `sudo dnf install ./npad-<version>-1.x86_64.rpm`
  (or `.aarch64` / `.riscv64`). It requires `gtk3` and installs the same files
  as the .deb.
- **Arch package** (Arch Linux, Arch Linux ARM, Arch RISC-V; also Arch
  derivatives such as Manjaro or EndeavourOS): `sudo pacman -U
  ./npad-<version>-1-x86_64.pkg.tar.zst` (or `-aarch64` / `-riscv64`). It
  depends on `gtk3` (and `glib2`, `cairo`, `pango`, which the binary links
  directly) and installs the same files as the .deb plus
  `/usr/share/licenses/npad/LICENSE`; pacman's hooks refresh the icon and
  desktop caches. `make pacman` builds one into `dist/`.
- **AUR: `npad-bin`** (Arch and derivatives; coming - the package is ready to
  publish but not on the AUR yet, so use the pacman repository below for
  now): once it is published, an AUR helper builds a package
  from the release's portable tarball, `yay -S npad-bin` (or `paru -S
  npad-bin`, or `git clone https://aur.archlinux.org/npad-bin.git && cd
  npad-bin && makepkg -si`). It provides and conflicts with `npad`. It is for
  people who want their packages AUR-managed; the pacman repository below is
  built from the same release and needs no build step.
- **Portable tarball** (any glibc distro with GTK 3, including ones with no
  .deb or .rpm: `npad-v<version>-portable-linux-<arch>.tar.gz`, with the
  architecture as `uname -m` prints it (`x86_64`, `aarch64`, `riscv64`) and
  the same oldest supported releases as the table above. It is the Linux counterpart of the
  Windows portable exe: the very binary the packages install, dynamically
  linked against the system's GTK 3 (GTK cannot usefully be linked
  statically, and bundling it would undo npad's lightness, so there is no
  static build or AppImage). It unpacks to `npad-v<version>-portable-linux-<arch>/`
  holding `npad`, a `README.txt`, and `share/` with the launcher, the hicolor
  icons and the AppStream metainfo, laid out as under `/usr/share`.

  ```bash
  tar -xzf npad-v<version>-portable-linux-x86_64.tar.gz
  cd npad-v<version>-portable-linux-x86_64
  ./npad
  ```

  To get a menu entry for your account, copy it under `~/.local` (from that
  directory; the `sed` points the launcher at the copy, since `~/.local/bin`
  is not always on the desktop's `PATH`):

  ```bash
  mkdir -p ~/.local/bin ~/.local/share
  cp npad ~/.local/bin/
  cp -r share/. ~/.local/share/
  sed -i "s|^Exec=npad |Exec=$HOME/.local/bin/npad |" ~/.local/share/applications/io.github.platima.npad.desktop
  ```

  `update-desktop-database ~/.local/share/applications` adds it to "Open
  with"; README.txt also lists the commands that remove it. If GTK 3 is
  missing, install `libgtk-3-0t64` (Debian 13 / Ubuntu 24.04 and newer),
  `libgtk-3-0` (Debian 12 / Ubuntu 22.04, openSUSE) or `gtk3` (Fedora, Arch).
  A portable copy never updates itself; the repositories below do (Help >
  Check for Updates still tells you when there is a new release). `make
  tarball` builds one into `dist/`.
- **apt / dnf / pacman repository (recommended; updates arrive with the rest
  of the system).** The repositories at <https://pkg.npad.dev> are rebuilt from
  the GitHub releases and signed with the npad packaging key
  (<https://pkg.npad.dev/npad.asc>); the key lives only in the separate
  `platima/npad-packages` repository.

  Debian, Ubuntu, Mint:

  ```bash
  sudo install -d -m 0755 /etc/apt/keyrings
  sudo curl -fsSL https://pkg.npad.dev/npad.asc -o /etc/apt/keyrings/npad.asc
  sudo curl -fsSL https://pkg.npad.dev/npad.sources -o /etc/apt/sources.list.d/npad.sources
  sudo apt update && sudo apt install npad
  ```

  Fedora:

  ```bash
  sudo curl -fsSL https://pkg.npad.dev/npad.repo -o /etc/yum.repos.d/npad.repo
  sudo dnf install npad
  ```

  Arch Linux, Arch Linux ARM, Arch RISC-V (pacman verifies the signed
  database and packages against a key you add and trust once):

  ```bash
  curl -fsSL https://pkg.npad.dev/npad.asc -o /tmp/npad.asc
  sudo pacman-key --add /tmp/npad.asc
  sudo pacman-key --lsign-key "$(gpg --show-keys --with-colons /tmp/npad.asc | awk -F: '$1=="fpr"{print $10; exit}')"
  printf '\n[npad]\nSigLevel = Required DatabaseRequired\nServer = https://pkg.npad.dev/arch/$arch\n' | sudo tee -a /etc/pacman.conf
  sudo pacman -Sy npad
  ```

  `curl -fsSL https://pkg.npad.dev/install.sh | sudo sh` does the right one of
  these (on Arch it appends `[npad]` to `/etc/pacman.conf` only if it is not
  there already). To remove the repository, delete
  `/etc/apt/sources.list.d/npad.sources` and `/etc/apt/keyrings/npad.asc` (or
  `/etc/yum.repos.d/npad.repo`; on Arch the `[npad]` section of
  `/etc/pacman.conf`, then `sudo pacman-key --delete <fingerprint>`).
  Pre-releases are not published there; use the release assets for those.
- **From source**: `make linux-gtk`, then `sudo make install`. `PREFIX`
  (default `/usr/local`) and `DESTDIR` are honoured; `make uninstall` reverses
  it. `make deb`, `make rpm` and `make pacman` build the packages into
  `dist/`; all use
  [nfpm](https://nfpm.goreleaser.com) (`scripts/install-nfpm.sh` fetches the
  pinned release), and `make deb` falls back to `dpkg-deb` without it. Build
  in a Debian 12 container for packages that run on every supported distro
  (Debian 13 for riscv64). The package architecture is read from the binary,
  so a binary compiled elsewhere (CI compiles riscv64 under QEMU) packages on
  any host with nfpm; `PKG_ARCH=arm64` makes it an error if the binary is
  anything else. The Arch package comes out of the same `nfpm.yaml` (its own
  `depends`, a one-line description and the licence file); `packaging/arch/`
  holds the AUR package and its README how it is published.
- The desktop file name, icon name and window class are all the application
  id `io.github.platima.npad`; the shell matches windows to the launcher and
  icon through it.

### Printing on Linux

File > Page Setup, Print (Ctrl+P) and Print Preview (Ctrl+Shift+P) behave as
described under *Behaviour notes* for Windows - same header/footer codes, same
`print_*` settings, the document rather than the window - with these
differences:

- **Print opens GTK's print dialog** (printers known to CUPS, plus *Print to
  File* for a PDF). The paper and orientation from Page Setup seed it. While
  the job is sent, GTK's progress window shows, with Cancel.
- **Page Setup** is npad's own dialog for the margins (inches or millimetres,
  following your locale) with a *Paper and Orientation...* button into GTK's
  paper dialog. Margins run from the edge of the sheet, as in Notepad.
- **Print Preview is npad's own window** (Print, Page Setup, Previous / Next,
  Zoom In / Out, with the same keys as on Windows - see *Print Preview* under
  [Behaviour notes](#behaviour-notes)), so it works without a PDF viewer
  installed. It is also what the print dialog's *Preview* button opens.
- **Date and time codes** (`&d`, `&t`) use the locale's short date with a
  four-digit year and its time without seconds, the same text as Time/Date
  (F5).
- **`npad --print FILE`** (or `-p`) prints to the default printer with no
  dialog and exits, as on Windows (see [Command line](#command-line)). With no
  printer available it says "There is no printer to print to." and exits
  with code 1.
- Setting the environment variable `NPAD_PRINT_EXPORT` to a file path makes
  Print (and `--print`) write a PDF there without showing the dialog (used to
  test printing on a machine with no printer).

## Settings reference

All settings live in `settings.json` and are editable in Preferences
(Edit menu, Ctrl+,) unless noted. npad writes each value it sets as a JSON
string; see [Editing settings.json by hand](#editing-settingsjson-by-hand)
for what it reads.

**Preferences** has OK, Cancel and Apply on both platforms. Apply (or OK)
writes only the settings whose controls you changed - a page you only looked
at writes nothing - saves once, and every other open npad window picks the
change up live; Cancel writes nothing. If the save fails, npad says so and
the window stays open with your changes, so OK can be tried again. The font
buttons, Clear Recent Files Now, Export / Import and Reset All Preferences act
at once. A number outside a box's range is brought into range when it is
applied, and the box then shows the value actually saved.

### General (Preferences > General)

File handling: saving, opening, recent files, recovery, and what happens when a file changes underneath you.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `auto_save_enabled` | bool | `false` | Silently save named documents on a timer. Off by default: overwriting the file from a timer is destructive, so it is opt-in. Untitled documents are never auto-saved. See *Auto-save* under [Behaviour notes](#behaviour-notes) for what it skips. |
| `auto_save_interval` | int | `300` | Auto-save period in seconds (10-86400). |
| `large_file_warning_mb` | int | adaptive | Confirm before opening (or Ctrl+drop inserting) files larger than this (MB, 0-1048576). `0` disables the prompt. When never set, the default scales with installed RAM (1/64th, clamped 50-1024 MB; 100 MB when memory size is unknown). |
| `recent_files_max` | int | `10` | Recent Files menu length (0-10). |
| `watch_file_changes` | bool | `false` | **Tell me when an open file is changed by another program.** Notice when the open file is changed on disk by another program and offer Reload / Save As / Keep editing, with a session-scoped "stop telling me about this file" option. Checked when the npad window is activated, not on a timer; while it is on, Ctrl+S also asks before replacing another program's version (see *External changes* under [Behaviour notes](#behaviour-notes)). Off by default: classic Notepad does not do this. |
| `session_resume_enabled` | bool | `true` | Crash recovery: snapshot unsaved work on the first edit and then on a timer; offer to restore after an unclean exit. On by default: snapshots never touch the user's file. All windows are restored, extras in their own cascaded windows. Turning it off removes the snapshots of every open window; turning it on snapshots unsaved work at once. Does **not** gate handoff snapshots taken when an update, a Windows restart or a Linux logout closes npad - those would otherwise be silently discarded. |
| `session_interval` | int | `30` | Snapshot period in seconds (5-86400). A tick writes only when the document has changed since the last snapshot. |
| `ctrl_n_new_window` | bool | `false` | Swap Ctrl+N / Ctrl+Shift+N between "New" (clear this window) and "New Window" (open another instance). |

### Editor (Preferences > Editor)

How typing and Find behave.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `find_seed_selection` | bool | `true` | **Fill Find with the selected text.** Opening (or re-focusing) Find or Replace with Ctrl+F / Ctrl+H puts the current selection in "Find what", selected so typing replaces it. On by default because Windows 10 notepad.exe (1809+) does exactly this, so it is the behaviour a Notepad user expects. Only a non-empty selection that contains no line break and is at most 255 characters seeds the box; anything else leaves the last search term as it was. The same key and rule apply on every platform. Unlike the other `find_` keys it is a preference, so Reset All Preferences resets it. |
| `find_wrap_around` | bool | `false` | **Wrap around by default.** Whether Find and Replace wrap around. Off, matching notepad.exe. The dialogs' own **Wrap around** box changes the same key, and the change reaches every open window at once. A preference, so Reset All Preferences resets it. |
| `tab_inserts_spaces` | bool | `false` | **Insert spaces up to the next tab stop** (Tab key group). Off by default, because Windows 10 notepad.exe types a tab character. See *Tab inserts spaces* in the behaviour notes. Same key and wording on every platform; independent of Basic Markdown support. |
| `tab_width` | int | `4` | **Tab width** (columns, 1-16; anything outside is clamped). Used when `tab_inserts_spaces` is on, and then also as the step of the Markdown tools' Spaces indent format. It does not change how wide a tab character is drawn (8 columns, as in Notepad). |

### Appearance (Preferences > Appearance)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `theme` | string | `light` | Colour scheme: `light`, `dark`, `system` (follow OS), `solarized-light`, `solarized-dark`. On Linux, `system` follows the desktop's light/dark preference live (the xdg-desktop-portal `color-scheme` setting, or GTK's own when there is no portal). On Windows with a High Contrast theme on, `light` and `system` use the High Contrast window and text colours, as Notepad does, and follow it when it is turned on or off; Dark and the Solarized schemes keep their own colours. |
| `monospace_font` | string | `Consolas` (Linux: `Monospace`) | Face used in monospace mode (picker button). On Linux the default is fontconfig's `Monospace`, since Consolas is not there. |
| `proportional_font` | string | `Segoe UI` (Linux: `Sans`) | Face used in proportional mode (picker button). |
| `font_size` / `font_weight` / `font_italic` | number/int/bool | `11`/`400`/`false` | Shared font metrics, set via the font pickers or Format > Font. `font_size` is 6-72 points; it may be a half point (`"10.5"`; the Linux font chooser keeps any tenth), and whole sizes are stored as plain integers (`"11"`), which older versions also read. `font_weight` is 100-900 on both platforms. Printing uses the same weight and italic, on Windows at the nearest whole point (10.5 prints at 11); the Linux build prints the size as it is. |
| `opendyslexic_enabled` | bool | `false` | Use the OpenDyslexic font (reading assistance). Requires the font to be installed; reverts with a hint if it is not. |
| `opendyslexic_font` | string | `OpenDyslexic` | Face used when OpenDyslexic mode is on. |
| `status_bar_visible` | bool | `true` | Show the status bar (also View > Status Bar). |
| `icon_style` | string | `system` | Which app icon to show. `system` (**Automatic**) matches each surface to the theme it sits on: the **taskbar** icon follows Windows, the **window caption** icon follows npad's own colour scheme - they are separate slots, so they can differ. `npad` makes both follow npad; `light` / `dark` pin both to one variant; `classic` restores the original pre-0.28 icon. Preferences > Appearance. |
|  |  |  | *Caveats on Automatic:* Windows draws a **grouped** taskbar button (several npad windows combined, the Windows 11 default) from the executable's icon rather than the window's, so the taskbar variant is not consulted there; and with **small taskbar buttons** the taskbar reads the caption slot instead, collapsing the two to the same artwork. Both fall back to a sensible icon, never a missing one. |
| `sync_view_state` | bool | `false` | **Sync view (font type and zoom) across all instances.** Mirror per-window view changes (font type, zoom) live to every open npad window. |
| `auto_update_defaults` | bool | `false` | **Auto-update defaults in real time.** View changes (font type, zoom) immediately become the new defaults on the Defaults page. |
| `status_show_counts` | bool | `false` | Show word / character / line counts in the leftmost status bar segment. Refreshed live while typing (coalesced ~8x/second); documents over ~1 MB fall back to a settle-then-count debounce. Shared with transient messages, which win until the next change. Numbers are grouped using your Windows regional settings (e.g. `1,234,567`), including any separator customised in Control Panel. |

### Defaults (Preferences > Defaults) - initial state for new windows/files, and the print header/footer

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `default_encoding` | int | `0` (UTF-8) | Encoding for new files: 0 UTF-8, 1 UTF-8 BOM, 2 UTF-16 LE, 3 UTF-16 BE, 4 ANSI. |
| `default_line_ending` | int | platform: `0` (CRLF) on Windows, `1` (LF) on Linux | Line endings for new files, and for opened files with no line breaks: 0 CRLF, 1 LF, 2 CR. |
| `default_font_mono` | bool | `true` | New windows start in monospace (`true`) or proportional (`false`) mode. **Monospace by default**, which is what mirrors notepad.exe: it has a single font setting and ships it as Consolas 11 - the same face and size as npad's monospace default. When it is not set, the pre-0.7 key `monospace_enabled` is read instead (npad no longer writes that key). Also the font type `npad /p` prints with. |
| `default_zoom` | int | `100` | New windows' zoom percent (10-500). |
| `print_header` | string | `&f` | Printed page header. Uses the codes below; edited in Preferences > Defaults (**Print header**, Alt+H). |
| `print_footer` | string | `Page &p` | Printed page footer. Same codes as the header. |
| `print_margin_left` / `_right` | int | `750` | Print margins in **thousandths of an inch**, set in File > Page Setup. Stored in that unit rather than the locale's, so a settings file means the same thing on a metric and an imperial machine; Page Setup shows them in millimetres where the system measures in metric. |
| `print_margin_top` / `_bottom` | int | `1000` | As above. |
| `print_orientation` | int | `0` | `0` printer default, `1` portrait, `2` landscape. Set in Page Setup. |
| `print_paper_size` | int | `0` | Paper, as a `DMPAPER_*` code (`1` Letter, `5` Legal, `9` A4, `256` custom). `0` leaves the printer's own. Set in Page Setup. |
| `print_paper_width` / `_length` | int | `0` | Custom paper only, in tenths of a millimetre. |

The **Use Current** button copies the active window's font type and zoom into
the fields.

### Markdown (Preferences > Markdown) - optional Markdown list tools

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `list_tools_enabled` | bool | `false` | **Basic Markdown support** (the Preferences checkbox): show the **Markdown** menu and matching right-click items (Sort, Unique, Convert Delimiters, Indent/Unindent), the indent shortcuts, cut-line Ctrl+X, and Enter list continuation; also enables the Find/Replace "Interpret escapes" option. Off by default for classic-Notepad compatibility. |
| `list_default_indent_format` | int | `0` (Spaces) | Format used by the default indent shortcut: 0 spaces, 1 tab, 2 `* `, 3 `- `, 4 ` * `, 5 ` - `, 6 custom. Markers include a trailing space. |
| `list_custom_indent` | string | (unset) | The custom indent prefix (format 6), stored as typed; escapes (`\t` `\\` `\uXXXX`) are interpreted when used. Prompted for on first use if empty. |
| `list_indent_shortcut_brackets` | bool | `false` | Use Ctrl+] / Ctrl+[ for indent/unindent instead of the default Tab / Shift+Tab. |
| `list_sort_case_sensitive` | bool | `false` | Sort compares case-sensitively (toggle from the Sort submenu). |
| `markdown_paste_primary` | int | `1` | What **Ctrl+V** / Edit > Paste does with rich (HTML) clipboard content: `0` plain, `1` convert lists to the current bullet style, `2` full Markdown conversion. Only applies when Basic Markdown support is on; plain-text clipboards always paste plain. |
| `markdown_paste_alt` | int | `0` | What **Ctrl+Shift+V** does with rich clipboard content: `0` plain, `1` lists, `2` full Markdown. Only applies when Basic Markdown support is on. |

### Updates (Preferences > Updates) - all opt-in, off by default

npad never checks for updates in the background. A manual **Help > Check for
Updates** always works; these settings govern any *automatic* surfacing.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `update_mode` | string | `"off"` | How a found update surfaces: `off` (manual only), `notify` (a **●** on the Help menu title + an "Update Available" item, no dialog), `prompt` (show the update dialog), `auto` (download + SHA-256-verify silently, then one confirmation before installing; Windows only - Linux treats it as `prompt`). |
| `update_check_on_launch` | bool | `false` | Run a single check once after the window first paints (primary window only; on Linux, the first npad window open). Automatic/launch checks fail silently. |
| `update_skipped_version` | string | (unset) | A version the user chose to **Skip**; suppresses the dot/prompt for exactly that version until a newer release appears. |
| `update_latest_version` | string | (unset) | Read-only state: the latest release tag seen by the last successful check (shown on the Updates tab). |
| `update_last_checked` | string | (unset) | Read-only state: local `YYYY-MM-DD HH:MM` of the last check (shown on the Updates tab). |

**Which installer.** Install Now (Windows) uses the installer that installed
this copy: the MSI (`npad-v<v>-msi-win-<arch>.msi`, run as `msiexec /i ...
/passive /norestart`, with `ALLUSERS=1` for an all-users install) when
Windows Installer installed npad into the folder this `npad.exe` runs from,
and otherwise the setup EXE (`npad-v<v>-setup-win-<arch>.exe`), which is also
what a portable copy gets (see **Portable exe** under
[Installation](#installation-windows)). An MSI from before 0.36.0 did not
record its folder; such an install counts as this copy's only when `npad.exe`
runs from that MSI's default folder for its scope (`Program Files\npad` for
all users, `%LOCALAPPDATA%\Programs\npad` per user), so a portable copy
elsewhere never updates through it. An MSI update keeps the association
groups you unticked on Preferences > Associations unticked. It is always the
download for the architecture npad was compiled for: an arm64 npad is never
offered the x64 installer, and reports an error if a release has none for it.

**What Install Now does.** A fully seamless no-close update is not possible,
because the installer must replace the running `npad.exe`. npad first starts
the installer; if Windows refuses to (antivirus, Smart App Control, a
policy), nothing closes and npad says why, naming where the verified
installer was saved. Then every npad window closes **without a save
prompt**: unsaved documents are parked and reopen as they were - when setup
relaunches npad at the end, or (MSI) the next time you start npad. Windows
with nothing unsaved are not reopened. A window that does not close (one
waiting in a dialog, say) is named, with **Try again** and **Install
anyway**. If this window's own work cannot be parked, it falls back to the
usual save prompt rather than lose it. In `prompt` and `auto` modes, declining
(Cancel on the "newer version" dialog, or No on Install now) is remembered for
that version until you sign out: automatic checks in other windows stay quiet about it (the
Help-menu dot still shows), and only Help > Check for Updates asks again.
Only one npad downloads or installs at a time.

**Verification.** The installer and its published `.sha256` are fetched to
`%TEMP%`, saved under a name of npad's own (`npad-update-<random>-<asset
name>`). The SHA-256 is checked in memory, then again on the saved file
through a handle that stays open, refusing writers, until the installer has
started. A digitally signed npad also runs only an installer validly signed
by the same publisher; an unsigned build relies on the SHA-256 but refuses an
installer whose signature is present and broken. A mismatch deletes the
download and stops.

**Pre-release builds** (a version such as `v0.37.0-rc2`) are told about newer
pre-releases as well as final releases; a final build is only ever offered
final releases. *View the release notes* opens that release's own page.

### Associations (Preferences > Associations)

Which file types npad is **offered** for. There are no settings keys: the page
reads and writes the registry directly, so it always shows what is actually
registered - including anything the installer registered.

| Group | Extensions |
|-------|-----------|
| Text files | `.txt` |
| Markdown & documents | `.md`, `.markdown` |
| Data files | `.csv`, `.tsv`, `.json`, `.xml`, `.yaml`, `.yml`, `.toml` |
| Config files | `.ini`, `.cfg`, `.conf` |
| Log files | `.log` |

- **Apply changes only the groups whose boxes you toggled.** A group you left
  alone is never rewritten, even when you press OK for a change on another
  page. (A property sheet sends Apply to every page you have visited.)
- **Ticking a group** registers a ProgID per extension (`npad.txt`, ...),
  with the open command and Explorer's **Print** command (`npad /p`), lists it
  under the extension's `OpenWithProgids`, and adds it to npad's
  `Capabilities\FileAssociations` - the same keys and values the installer
  writes. npad then appears in **Open with** for those types and on Windows
  11's **Default apps** page.
- **It never sets the extension's default value**, which the installers set
  when no other program holds it (see **Default editor** under
  [Installation](#installation-windows)). It would not make npad the default
  anyway: that is a hash-protected per-user choice only Windows Settings can
  make.
- **Unticking a group** removes npad's ProgIDs and entries for it. If an
  extension's default value names npad's own ProgID - as the installer leaves
  it - npad gives the type back: for your account it removes the value, so
  the machine's own shows through; for all users it puts Windows' own type
  (`txtfile` for `.txt` and `.log`, `inifile`, `xmlfile`) back where Windows
  still has it. A value naming any other application is left alone.
- **Set Default Apps...** opens Windows Settings > Default apps, straight to
  npad's entry on Windows 11 (`registeredAppUser` or `registeredAppMachine`,
  whichever matches where npad is registered).
- **Save for all users (requires elevated permissions)** switches which set
  the boxes show and edit: unticked, your account's
  (`HKEY_CURRENT_USER\Software\Classes`); ticked, every account's
  (`HKEY_LOCAL_MACHINE`). Edits in both are kept while Preferences is open,
  and Apply writes each. Changes to the all-users set go through a consent
  prompt; declining it changes nothing. It is enabled only when `npad.exe` is
  under Program Files as the system records it (`ProgramFilesDir` in HKLM,
  not the environment variable): a per-user or portable copy lives where
  other accounts could not run it from, and where its owner could replace it.
- **Updates keep your choices.** npad's in-app updater tells setup which
  groups are *not* registered (`/MERGETASKS="!assoc\markdown,..."`), so an
  upgrade does not re-register what you unticked. It never selects a group:
  the installer's tasks also set the extension's default value when it is
  free, which this page never does. If both a per-user and an all-users
  install exist, setup asks which to upgrade, and nothing is passed. An
  in-app MSI update likewise names its features itself, leaving out the
  association groups that are not registered, and never adds one.
- **Uninstalling with the setup EXE removes npad's file type
  registrations**, including groups added here after install. On an
  all-users install that covers the machine and the per-user registrations
  of the account running the uninstall - which, under an over-the-shoulder
  UAC prompt, is the administrator who elevated, not the signed-in user.
  Other accounts' per-user entries cannot be reached and remain, pointing at
  the removed program. **The MSI does not remove groups added here**: an MSI
  upgrade uninstalls the old product before installing the new one, so a
  removal rule would wipe them on every upgrade instead.

### Other persisted state (no dedicated UI)

| Key | Description |
|-----|-------------|
| `word_wrap` | Word wrap on/off (Format > Word Wrap, Alt+Z). |
| `window_x/y/width/height/maximized` | Window geometry, saved on exit by the window closed last, as in Notepad. The position is in screen coordinates on both platforms. First run uses ~56% x ~60% of the monitor work area, centred. On Windows, a saved position whose title bar would be off the screen is moved back onto it; a window saved maximized opens maximized at once. |
| `recent_file_0..9` | Recent files list. |
| `find_match_case`, `find_whole_word`, `find_search_down`, `find_interpret_escapes`, `find_highlight_all` | Find/Replace options (checkboxes in the dialogs; `find_search_down` is the Find dialog's Up/Down - Replace always searches down). Saved as soon as you change them, and they reach every open window live. `find_wrap_around` is the dialogs' Wrap around box too, but it is a preference with a checkbox of its own on Preferences > Editor (see above). |
| `find_hist_0..9`, `replace_hist_0..9` | Recent search/replace terms. |
| `delim_from_hist_0..9`, `delim_to_hist_0..9` | Convert Delimiters' recent *From* and *To* values, newest first; the dialog reopens with `_0` (default `,` to `\n`). Unlike the Find history, Reset All Preferences clears them. |
| `monospace_enabled` | Legacy (before 0.7): read only when `default_font_mono` is not set. |

### Editing settings.json by hand

npad reads the file as JSON: an object of keys and values.
- `\uXXXX` escapes (surrogate pairs included), `\b`, `\f`, `\/` and the usual
  escapes work in keys and values, and a UTF-8 byte order mark is tolerated.
- Bare numbers and `true` / `false` work, and keep their JSON type when npad
  saves the file. `null` means "not set": the default applies.
- A value that is not valid for its setting (`"ten"` for a number, `"yes"` for
  a checkbox, an empty number) uses that setting's default, rather than
  reading as 0 or off. Checkbox settings take `true` / `false` in any case, or
  `1` / `0`. Numbers with a range are brought into it.
- A nested object or array is ignored by npad but kept, and written back
  unchanged. A value over 64 KB or a key over 256 bytes is dropped.
- Keys keep their order in the file; new ones are added at the end.

A file npad cannot read in full is kept as `settings.json.corrupt` before a
repaired one replaces it (see [File locations](#file-locations)). If
`settings.json` cannot be read at a given moment (another program has it
open, say), npad keeps the settings it already has rather than falling back
to defaults and saving those.

## View state vs settings

Font type (mono/proportional) and zoom are **per-window view state**, like
the caret position: changing them affects only that window and is not saved.
New windows start from the Defaults-tab values. Two preferences extend this,
both on the **Appearance** page:

- **Sync view (font type and zoom) across all instances**: view changes
  mirror live to every open npad window.
- **Auto-update defaults in real time**: view changes are saved as the new
  defaults for future windows (the Defaults page's font type and zoom).

Everything else (theme, word wrap, status bar, fonts, timers, defaults) is a
shared setting: changing it anywhere saves `settings.json` immediately and
propagates live to all open npad windows. This includes Clear Recent Files
and File > Page Setup.

**How windows share the file.** Each window remembers which settings it has
changed since it last saved (setting a value it already holds is not a
change). A save re-reads `settings.json` and writes back what is there now
with only those changes applied, so one window never undoes another's
change - closing a window that has been open for days does not put back the
settings it started with. Recent Files and each search history are saved
whole, from the window that changed them last, so a file is never listed
twice and Clear Recent Files sticks. Window position, size and the maximized
state always come from the window closed last, as in Notepad. Saves from
different windows take turns through `settings.json.lock`.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N | New (or New Window, per preference) |
| Ctrl+Shift+N | New Window (or New, per preference) |
| Ctrl+O / Ctrl+S / Ctrl+Shift+S | Open / Save / Save As |
| Ctrl+P | Print (File > Page Setup sets margins, orientation and paper) |
| Ctrl+Shift+P | Print Preview |
| Ctrl+W / Ctrl+Shift+W | Close this window / Close all windows (each save-checked) |
| Ctrl+Z / Ctrl+Y | Undo / Redo (100,000-step history on Windows; on Linux 10,000 steps or 256 MB of text) |
| Ctrl+X / Ctrl+C / Ctrl+V / Del | Cut / Copy / Paste / Delete |
| Shift+Insert / Ctrl+Insert / Shift+Delete | Paste / Copy / Cut. Insert on its own does nothing: there is no overtype mode, as in Notepad |
| Ctrl+Shift+V | Alternate paste (Markdown tools): the mode configured in Preferences > Markdown > Paste (default plain). Plain otherwise. |
| Ctrl+A | Select All |
| Ctrl+F / F3 / Shift+F3 / Ctrl+H | Find / Find Next / Find Previous / Replace |
| Ctrl+G | Go To Line |
| F5 | Insert time/date |
| Alt+Z | Toggle word wrap |
| Ctrl+M | Toggle monospace (this window) |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0 | Zoom in / out / reset (this window; Ctrl+Scroll also zooms) |
| Ctrl+, | Preferences |
| Ctrl+Shift+. | Preferences opened on the hidden Debug page (also: Shift+click the Preferences menu item) |
| Tab | Types a tab character (as Notepad does) - or, with *Insert spaces up to the next tab stop* on (Preferences > Editor), the spaces up to the next tab stop, replacing any selection. The Markdown tools' indent on a selection takes precedence over that while they are on. |
| Tab / Shift+Tab | Indent / Unindent (Markdown tools). Tab indents any selection; without one it types a tab (or spaces, see above). Shift+Tab always unindents - the selected lines, or the current line. A preference switches this binding to Ctrl+] / Ctrl+[. |
| Ctrl+X (no selection) | Cut the whole current line (Markdown tools); Ctrl+V then pastes that line above the current line, keeping the caret in place, until the clipboard changes |
| Enter (on a list line) | Continue the list: the new line starts with the same indent + marker (Markdown tools). On an empty bullet, removes the marker and inserts a plain newline (ends the list). |
| Alt+F, R, then 1-9 or 0 | Open a file from File > Recent Files; the tenth entry is labelled `10` with 0 as its access key |

The line ending is changed from Format > Line Endings or by clicking it in the
status bar. Ctrl+E does nothing (it once cycled the line ending, a change Undo
could not reverse, on the key Windows 10 Notepad uses for Search with Bing).

## Status bar

Segments left to right; several are clickable:

| Segment | Shows | Click action |
|---------|-------|--------------|
| Message | Transient messages (e.g. "Match 3 of 7", or an auto-save note), and the optional word / character / line counts (`status_show_counts`). A message clears as soon as you type, and Find's match count also when Find closes. An auto-save note comes back at each interval while its reason lasts, and goes with the next successful save | - |
| Ln, Col | Caret position. Ln counts lines as the file has them, whatever Word Wrap does, and Col counts from the start of that line | Open Go To Line |
| Zoom % | This window's zoom | Reset to 100% |
| Mono/Prop | This window's font type | Toggle monospace |
| Line ending | Windows (CRLF) / Unix (LF) / Mac (CR) | Line-ending picker (converts on save) |
| Encoding | UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / ANSI | Encoding picker (applies on save) |

Encoding can be set two ways: this status-bar picker, or the Encoding dropdown
inside the Save As dialog. A pure-ASCII file always
reports **UTF-8** on open, because ASCII bytes are identical whether the file
was written as UTF-8 or ANSI; the encoding only diverges once the text contains
non-ASCII characters.

## Command line

```
npad [options] [filename]
  -h, --help        Show help
  -v, --version     Show version
  /p FILE           Print FILE to the default printer and exit (Windows; also /P)
  -p, --print FILE  The same, on every platform
```

- **A file name** opens that file. Any Unicode name works, whether it comes
  from a console, Explorer (double-click, Open with, Send To) or a shortcut.
  A relative name is taken from the current folder. **If the file does not
  exist but its folder does**, npad asks, as Notepad does, "Cannot find the
  *name* file. Do you want to create a new file?": Yes creates it empty (in
  the default encoding and line ending) and opens it, so Save goes straight
  back to that name; No starts an Untitled document. A folder that does not
  exist gives the usual "Could not open file" error. This applies only to a
  name given on the command line, not to Recent Files or a drop. Unlike
  Notepad there is no Cancel, and no `.txt` is added to a name without an
  extension.
- **Only one file is opened**: given several names, npad opens the last one.
  Quote a name that contains spaces (`npad "my notes.txt"`); npad does not
  join unquoted words into one name as Notepad does.
- **A name that starts with `-`** is read as an option (there is no `--` to
  end the options), so write it with a folder in front: `npad ./-notes.txt`,
  or `npad .\-notes.txt` on Windows. An unknown option makes npad exit with
  code 1 without opening a window.
- **On Windows npad is a windowed program**, so `--help`, `--version` and the
  unknown-option message print nothing in a console window. Help > About
  shows the version.
- **`/p FILE`** (Windows: also `/P`, `-p` and `--print`; Linux: `-p` and
  `--print`, since `/p` is an ordinary path there) prints FILE straight to the
  default printer, with no print dialog, then exits - Notepad's `/p`, and what
  Explorer's **Print** command runs for the types npad is registered for. No
  document window opens. It prints in the font a new window would use (the
  Defaults font type, `default_font_mono`, with `font_size`, `font_weight` and
  `font_italic`) with Page Setup's margins, paper, orientation, header and
  footer. A progress window with Cancel shows while it prints. Exit code: 0
  printed, 2 cancelled, 1 failed (no file given, the file could not be read,
  no default printer, a print error), with a message box (Windows) or a dialog
  and a line on stderr (Linux). Nothing is written to `settings.json`, no
  recovery snapshot is taken and no Recent Files entry is added. On Linux
  `NPAD_PRINT_EXPORT=out.pdf` turns it into a PDF instead. Notepad's `/pt`
  (print to a named printer) is not supported.

Internal options used by npad itself: `--register-machine <changes>` (run by
npad, elevated, to apply the Associations page's all-users changes: a list
such as `+text,-log`, where only the five group names are accepted; it
refuses unless npad is under Program Files, writes the machine hive only, and
exits without opening a window), `--recover <slot>` or `--recover
<slot>:<claimer>` (reopen a specific recovery slot; a slot id is two or three
hexadecimal parts joined by `-`, `<time>-<nonce>-<pid>` since 0.36 and
`<time>-<pid>` before it, and anything else is ignored with an error logged,
so npad starts as a plain launch; a window that finds nothing left to restore
closes itself, without saving its position) and `--cascade <n>` (offset the
window by n steps; without it, a launch cascades past however many npad
windows are already open, so windows opened from Start > Run or Explorer
stagger just as New Window does). Not intended for direct use.

Environment variables: `XDG_CONFIG_HOME` (Linux, see
[File locations](#file-locations)); `NPAD_PRINT_EXPORT` (Linux, see
[Printing on Linux](#printing-on-linux)); `NPAD_TEST_CONFIG_DIR`, honoured
only by test builds compiled with `-DNPAD_TEST_HOOKS` (the settings and
recovery folder then live there; on Windows such a build has no settings
folder at all without it) and ignored by the npad you install.

## Behaviour notes

- **Find is filled from the selection**: with text selected, Ctrl+F / Ctrl+H
  open the dialog with that text in "Find what" (selected, so typing replaces
  it), as Windows 10 notepad.exe does. A selection that spans lines, is empty
  or is longer than 255 characters is not used; the last search term stays.
  With "Interpret escapes" on, backslashes in the seeded text are doubled so it
  still matches the selection literally. Preferences > Editor > **Fill Find
  with the selected text** (`find_seed_selection`, on by default) turns it off.
- **Find and Replace**:
  - **Replace always searches down**, as in Notepad: its Find Next and Replace
    ignore the Up/Down choice, which belongs to the Find dialog and is
    remembered there.
  - Replace replaces exactly what Find Next found, including a match that
    differs in the case of an accented or other non-English letter, or (Linux)
    one written another way in Unicode.
  - **Replace All is one edit**: one Ctrl+Z undoes all of it, on both
    platforms. Matches are found in the text as it was before replacing. If
    it cannot be done (out of memory), the document is left as it was and the
    status bar says "Replace All failed - the document was not changed".
  - **F3 / Shift+F3 after the dialog closes** search for whatever was in the
    Find box when it closed, whether or not Find Next was pressed; with the
    box emptied, F3 opens Find.
  - Pressing Ctrl+F or Ctrl+H for the dialog that is already open brings it
    forward with what you typed still in the box (a qualifying selection still
    replaces it), and switching between Find and Replace carries the term
    across.
  - The options (Match case, Whole word, Up/Down, Wrap around, Highlight all,
    Interpret escapes) are saved as soon as you change them, reach every open
    window at once, and an open dialog follows a change made in another
    window, Reset All Preferences included.
- **Go To Line** (Ctrl+G, or a click on Ln/Col) opens with the current line
  filled in, counted as the file's lines even with Word Wrap on. A number of
  0, a line past the last one, or text that is not a number shows "The line
  number is beyond the total number of lines" and leaves the dialog open, with
  the text selected for correcting.
- **Atomic saves**: a save never destroys or truncates the existing file.
  npad writes a temporary file beside it (`.npad-<8 hex>.tmp`, with a random
  name, created so that nothing planted in its place is written through),
  gives it the original's identity, flushes it to disk, reads it back to
  check it, and only then puts it in the original's place - so a power cut
  just after Save cannot leave an empty or zero-filled file. Crash-recovery
  snapshots are written the same way.
  - **The file keeps what it was**: on Windows its permissions (ACL), owner,
    Hidden/System attributes, creation date and alternate streams such as the
    Mark-of-the-Web (a downloaded script still triggers SmartScreen after you
    edit it); on Linux its mode (set-uid and set-gid bits included), owner,
    group, ACLs and other extended attributes, so a private 0600 file stays
    private and a script keeps its execute bit. A new file gets the usual
    permissions (`0666` less the umask on Linux).
  - **Links**: saving through a symbolic link edits the file it points to and
    keeps the link (a link to a missing file creates that file); saving one
    name of a hard-linked file updates every name.
  - **When the file cannot be replaced** - a folder you cannot write to (a web
    root, `C:\ProgramData`, a Docker bind mount), hard links, a file another
    program holds open, an owner or ACL that a new file could not be given -
    npad overwrites it in place instead, as Notepad does. It first copies the
    original to `npad-backup-<hex>.bak` in the recovery folder (the temp
    folder if that cannot be used), puts the original back if anything goes
    wrong, and deletes the copy afterwards. Only if even putting it back
    fails is the copy kept, and the error message names it.
  - On Windows, paths longer than 260 characters work - in File > Open, Save
    As, drag and drop and saving - when Windows' long paths are enabled
    (`LongPathsEnabled`, or the *Enable Win32 long paths* policy); npad
    declares itself long-path aware. On Linux, names up to the file system's
    limit save (the temporary name is short whatever the file's name).
- **Unsaved-changes prompt**: closing or replacing a modified document asks
  "Do you want to save changes to ...?" with **Save / Don't Save / Cancel**,
  matching notepad.exe. Esc, the close box, and anything other than an explicit
  Save or Don't Save all cancel - i.e. they abort the close and keep your
  document, never discard it. On Windows a second close request while the
  prompt is up (another click on the close box, Close All from another window)
  does not open a second prompt. Don't Save followed by New or Open also discards the
  crash-recovery snapshot of the text you dropped.
- **External changes and saving** (`watch_file_changes` on): Ctrl+S on a file
  another program has rewritten since npad read it asks "... has been changed
  by another program since npad opened it. Do you want to replace it with
  your version?"; **No** opens Save As, so both versions can be kept. "Stop
  telling me about this file" skips this question too. With watching off,
  Ctrl+S saves without asking, as Notepad does.
- **Reload from disk** (the "This file has changed on disk" question) asks
  the same questions as Open first - for a file that now looks binary or is
  over the large-file size - and says why when the file cannot be read (for
  example while another program has it locked). A declined or failed reload
  counts as *Keep editing*: the document stays as it is.
- **Auto-save** (`auto_save_enabled`, off by default) is silent: it never
  opens a dialog, and never writes while one of npad's own questions or its
  Open / Save As dialogs is open, so *Don't Save* really means nothing was
  saved. Before every write it checks the file on disk itself, whatever
  `watch_file_changes` says: it never overwrites another program's change and
  never recreates a file that was deleted or moved. Whatever stops it is
  said in the status bar - "Auto-save paused: the file was changed outside
  npad. Save to resume.", "Auto-save skipped: some characters cannot be saved
  in ANSI. Save to choose." or "Auto-save failed: *reason*" - and a manual
  save then asks or explains as usual. After *Keep editing* in the
  changed-on-disk question it stays paused until you save. It also leaves
  alone documents that Save would divert to Save As or ask about (below).
  Changing settings in another window, or pressing OK in Preferences, does
  not restart its countdown (or crash recovery's) unless the interval or the
  on/off state changed.
- **Save As**: the encoding chosen in the dialog applies only once the save
  succeeds; a cancelled, refused or failed Save As leaves the document's
  encoding as it was.
- **.LOG**: as in Notepad, a file whose first line is exactly `.LOG` (upper
  case, nothing else on the line) gets the time and date (as Time/Date, F5)
  on a new line at its end each time it is opened - from File > Open, Recent
  Files, a drop or the command line - with the caret after it, ready for the
  next entry. It is one undoable edit that marks the document modified;
  nothing is written until you save. Reload from disk and crash recovery add
  no stamp.
- **Open with the default app** (offered for a binary-looking file): when
  the file is a program, script, shortcut or installer - by its extension
  (`.exe`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.msi`, `.lnk`, `.reg`,
  `.jar`, `.sh`, `.desktop`, `.AppImage`, `.py` and many more), by an MZ, ELF
  or `#!` start, or on Linux by its execute bit - npad first asks
  '"*name*" is a program, script or shortcut. Opening it with the default app
  may run it. Do you want to run it?'.
- **Scroll bars are always shown** (greyed out when the content fits), as in
  classic Notepad, rather than appearing and disappearing. This also keeps the
  text area a constant width, so the wrapped layout does not jump when the
  document grows past one screen. With word wrap on there is no horizontal bar
  at all; with it off, the horizontal bar follows the same always-shown rule.
- **Clicking an unfocused window acts immediately**: the click that activates
  npad also places the caret, and a click-drag starts selecting - no need to
  click once to focus and again to act.
- **Print Preview** (Ctrl+Shift+P) shows the pages exactly as they will print.
  The layout is measured once against the real printer, in the printer's own
  units, and the preview then draws those same coordinates - so a line that
  wraps on screen wraps identically on paper. Toolbar: Print, Page Setup,
  Previous / Next, One / Two Page, Zoom In / Out, Close. Keys, the same on
  Windows and Linux: Esc closes; Ctrl+P prints; Page Up / Page Down turn the
  page; Home / End go to the first / last page; `+` (also `=` and the keypad's
  `+`) zooms in and `-` (also the keypad's) zooms out. Page Up / Down, Home /
  End and the zoom keys work with or without Ctrl. On Windows, Enter presses
  the focused toolbar button, the arrow keys and the mouse wheel scroll a
  zoomed page, and at the Fit zoom the wheel turns one page per notch (two in
  two-page view) - a touchpad's small movements add up to a notch first, so a
  swipe does not skip pages.
  - The preview opens centred on the monitor its document is on, and follows a
    Windows light/dark switch under the System colour scheme.
  - The document window is disabled while the preview is open. The preview is a
    snapshot, and letting the text change behind it would make it a lie.
    Close All (or an update that is ready to install) closes the preview first
    and then asks about the document; one that arrives while Page Setup, the
    Print dialog or a print job is running waits until printing is over.
  - **Page Setup** from inside the preview re-paginates immediately.
  - The preview measures against the **default printer**. Printing to a
    different printer re-paginates against that one, so a device with a
    different resolution or hardware margin can wrap differently. The paper
    size and orientation chosen in Page Setup are forced onto whichever printer
    is used, so those at least never differ.
  - The paper is drawn white in every colour scheme, because it is paper. Only
    the backdrop follows npad's scheme.
  - With **no printer installed at all** the preview still works, against the
    locale's paper size at 600 dpi.
  - npad opens the **classic** Windows print dialog, deliberately: Windows
    11's modern one ignores the orientation and paper an application passes
    in, so it would show "Portrait" for a landscape job. The classic dialog
    honours Page Setup. It has no preview pane; File > Print Preview is the
    preview.
- **Printing** (Ctrl+P) prints the *document*, not the window. The page has its
  own width, so the editor's word-wrap setting does not affect the output:
  lines too long for the printable width are re-wrapped there, broken at a
  space rather than mid-word. The page uses the font that window is currently
  showing, with its weight and italic, at the same point size (on Windows a
  half-point size prints at the nearest whole point, 10.5 at 11; on Linux it
  prints as it is). An emoji or other character outside the Basic
  Multilingual Plane is never split across lines.
  - While a job is prepared and sent, a small window shows which page is being
    sent ("Printing *name*", "Sending page N of M") and has a **Cancel**
    button; the document is disabled meanwhile. Cancelling there, or in a
    printer's own save prompt (Microsoft Print to PDF), is not reported as a
    failure. **Print to file** in the Print dialog asks where to save the file.
    (Linux: GTK's own progress window.)
  - **Page Setup** (File menu) sets margins, orientation and paper, and the
    change reaches every open npad window. Margins are persisted in
    thousandths of an inch, so they mean the same thing on a metric and an
    imperial machine; on Windows the dialog shows them in millimetres when
    Windows' measurement system is metric, as Notepad does, and a margin you
    leave untouched keeps its exact stored value.
  - **Header and footer** are set on Preferences > Defaults, and use the same
    codes as notepad.exe:

    | Code | Prints |
    |------|--------|
    | `&f` | The file name (`Untitled` for an unsaved document) |
    | `&p` | The page number |
    | `&d` | The date, in your regional short-date format |
    | `&t` | The time, in your regional format |
    | `&l` `&c` `&r` | Left-align / centre / right-align what follows |
    | `&&` | A literal `&` |

    Text before any alignment code is left-aligned. The defaults are `&f` for
    the header and `Page &p` for the footer, as in notepad.exe; clearing a
    field omits that line entirely and gives the text more of the page.
- **Encodings & line endings**: detected on open (BOM + heuristics) and kept
  on save. Line endings are changeable via the status bar or Format > Line
  Endings; encoding via the status bar or the Save As dialog's Encoding
  dropdown. Pure-ASCII files always report UTF-8 (ASCII bytes are identical
  across UTF-8 and ANSI).
  - **One kind of line break per document**: npad detects the file's most
    common line ending and saves every line break in that style, so a file
    with mixed endings comes back normalised. A tie goes to a style the file
    actually uses - the platform's own if it is one of them, else CRLF, LF,
    then CR - never to a third kind.
  - **UTF-16 without a byte order mark** (mostly Latin-script text) is
    detected at any length and saved back without one. Choosing another
    encoding, and new UTF-16 documents, get a BOM, as in notepad.exe.
  - **Short CJK files**: a short GBK, UHC or Big5 file whose bytes also happen
    to be valid UTF-8 opens as UTF-8, as in Windows 10 notepad.exe. If such a
    file shows garbled text, check the status bar and reopen it, or Save As
    with the right encoding.
  - **ANSI** means the system's ANSI code page on Windows (or, when Windows'
    *Beta: Use Unicode UTF-8* option is on, the locale's legacy ANSI code
    page, 1252 if it has none), and Windows-1252 on Linux, so curly quotes,
    dashes and the euro sign in a file from Windows read correctly there.
- **Lossy ANSI saves warn first**: manually saving as ANSI when the document
  contains characters the code page cannot represent (emoji, most
  non-Latin scripts) asks before replacing them with `?`, and they are saved
  as `?` - never as a look-alike that changes the meaning (∞ as 8, ł as l);
  declining cancels the save. Auto-save skips such documents silently rather
  than prompting from a timer (session snapshots still protect the content).
- **Bytes npad cannot save back exactly**: where part of a file cannot be
  decoded exactly - invalid bytes after a UTF-8 BOM, a UTF-16 file with an odd
  last byte or an unpaired surrogate, and on Windows an ANSI file whose bytes
  do not convert back to themselves - the invalid bytes show as replacement
  characters (U+FFFD), and the first Save over the original asks: "Some bytes
  in this file could not be read, so npad cannot save them back exactly as
  they were. Save over the original anyway?". No keeps
  the file as it was; Save As to another file needs no question; once saved,
  the document is clean. Auto-save leaves such a document alone. (On Windows,
  cp932 files using the NEC-selected IBM extension characters also get this
  question, because Windows writes those back with their other codes.)
- **Characters the Windows edit control cannot show exactly**: Unicode line
  and paragraph separators (U+2028, U+2029, shown as line breaks) and BEL,
  U+FFFC, U+FFFD and some noncharacters (shown as spaces). A file containing
  them opens with substitutes, so Save does not write over the original: it
  explains why and opens Save As, and auto-save leaves the document alone.
  Saving it under any name clears this. The same applies on both platforms
  when invalid UTF-8 in the file had to be replaced with U+FFFD for display.
  Vertical tabs (U+000B) are kept on save.
- **A file too large to show**: if the editor cannot hold a file at all (too
  large, or out of memory), Open says "The file is too large to show, or there
  is not enough memory to open it." and Reload "Could not reload file: ...",
  and the current document, its name, encoding and line ending stay as they
  were. Files of 2 GB or more are refused before reading ("too large to
  open"). On Linux the editor's limit is also about 2 GB of text, which applies
  to a Ctrl+drop insert as well. If npad cannot read a document's text back
  to save it (a very large document, low memory), Save says so instead of
  writing anything.
- **Emoji & mixed scripts**: characters the configured font lacks (emoji,
  CJK, etc.) render via Windows font fallback - when typed, when a file is
  opened, across font or theme changes, and when the window moves to a
  monitor with another scaling. A font or colour-scheme change adds no undo
  step and does not mark the document modified.
- **Tab characters** line up every 8 characters, as in Notepad - on Windows at
  8 average character widths of the editor font, at every font size, zoom and
  display scale. `tab_width` does not change this; it is only how many spaces
  *Tab inserts spaces* types.
- **Tab inserts spaces** (Preferences > Editor, `tab_inserts_spaces`, off by
  default; width `tab_width`, 4 unless changed, 1-16): a plain Tab types the
  spaces that reach the next tab stop instead of a tab character. The stop is
  the next multiple of the width counted in characters from the start of the
  line, so at width 4 a Tab types four spaces on an empty line, three after
  one character, one after three and a whole four again after four. A tab
  character already on the line advances to the next stop too, and each
  character counts as one column whatever its UTF-8 / UTF-16 length (an emoji
  is one). Wide East Asian glyphs are still one column each, since the rule
  is about characters, not about how wide the font draws them; the on-screen
  width of a literal tab character is the editor's own and `tab_width` does
  not change it. Over a selection Tab replaces the selection with the spaces,
  as Notepad replaces it with a tab. One press is one undo step. Shift+Tab
  (outside the Markdown tools), Ctrl+Tab and Alt+Tab are not touched. With the
  Markdown list tools on, Tab over a selection still indents the selected
  lines (it does not replace them), exactly as before; and with this option on
  their *Spaces* indent format steps by `tab_width` instead of four (Tab and
  Ctrl+] / Ctrl+[ then agree), while the nesting step of marker formats
  stays two spaces and the *Tab* format stays a tab character. Changing
  either setting applies to every open window at once.
- **Markdown list tools** (Preferences > Markdown, off by default): add a
  **Markdown** menu and right-click items. In the right-click menu they read
  Convert De*l*imiters..., I*n*dent, Un*i*ndent and Paste as *M*arkdown (the
  underlined access key in italics), so no two items there share a key.
  - *Sort / Unique* treat each line as an entry. If the selection spans more
    than one line, only those lines are affected (extended to whole lines);
    otherwise the whole document is. Unique keeps the first occurrence. Sort
    is stable (equal lines keep their order), and both handle large documents
    quickly - a 100,000-line log in a fraction of a second.
  - *Convert Delimiters…* is a find/replace for delimiters; both fields
    interpret `\n \r \t \\ \uXXXX`, so e.g. From `,` To `\r\n` turns a
    comma-separated line into separate lines. A character outside the Basic
    Multilingual Plane can be written as its two `\u` halves (`\uD83D\uDE00`
    is 😀); a lone half, or `\u0000`, gives U+FFFD. Any other backslash pair
    gives the character after the backslash, so `\0` is just `0`. The same
    rules apply to Find/Replace's *Interpret escapes* and the custom indent
    prefix. It applies to the selection (when "Selection only" is ticked) or
    the whole document.
    Line breaks are matched as `\n` whatever the file's line ending is - the
    document is normalised internally and the file's own ending restored on
    save - so a **From** of `\r\n` is accepted and treated as `\n` rather than
    silently matching nothing.
    Both fields remember recent values and reopen with your last conversion; a
    **Swap** button reverses the direction without retyping.
  - *Indent / Unindent* (Tab / Shift+Tab on a selection by default - a
    preference switches to Ctrl+] / Ctrl+[ - or the Indent submenu) prefix
    each target line. The formats are spaces, tab, `* `, `- `, ` * `, ` - `
    (markers include a trailing space) and *Custom…*, which prompts for any
    prefix (escapes `\t \\ \uXXXX` allowed) and remembers it. Indenting a
    line that already has its marker adds two spaces instead of a second
    marker (markdown-style nesting); unindent removes one unit - two nesting
    spaces, then the marker itself. With the *Custom* format, unindent
    removes the exact custom prefix, leading spaces or tab included (so
    `  - ` or a tab and `* ` come off cleanly). A bullet nested behind a tab
    counts as a bullet, and unindent takes one tab off it. Whitespace formats
    add/remove a tab or up to four spaces (up to `tab_width` spaces while *Tab
    inserts spaces* is on). Each operation is a single undo step. Line scoping
    follows logical lines, so word wrap does not fragment the target.
  - *Enter continues lists*: pressing Enter on a line starting with a marker
    (`* `, `- `, or the custom prefix, at any nesting depth) starts the next
    line with the same indent and marker; pressing Enter on an empty bullet
    removes the marker and inserts a plain newline, ending the list. When
    the text after the caret already starts with a marker (two items typed
    on one line), Enter splits with a plain newline instead of doubling the
    marker.
  - *Marker detection is style-agnostic*: deepening and unindent act on
    whatever bullet a line actually carries (`* `, `- `, or the custom
    prefix), regardless of the configured default format, so mixed lists
    never gain a stacked `- -` marker and always unindent.
  - *Cut line / paste above*: with nothing selected, Ctrl+X cuts the whole
    current line (including its line break). Ctrl+V then pastes that line
    above the current line while the caret stays put; this holds for repeat
    pastes until something else is copied to the clipboard.
  - *Rich-text paste*: when the clipboard carries HTML (copied from a browser,
    word processor, etc.), paste can convert it. Two depths:
    - **Lists** converts only when the copied HTML really contains a list
      (`<li>`, or Word's list paragraphs): it turns the lists (with nesting)
      into list lines in your current bullet style, keeping each bullet on the
      same line as its text, and flattens the rest to plain text. Anything
      else pastes as the clipboard's own plain text, so code from VS Code,
      terminal output and spreadsheet cells keep their indentation and
      columns.
    - **Markdown** does a full conversion: headings, bold and italic (from the
      document's real formatting - Google Docs' wrapper is not taken for
      bold), inline code and fenced code blocks (also from VS Code or Windows
      Terminal, whose lines are styled `white-space: pre`), links, images,
      block quotes, rules, and lists - Word's bullet and numbered lists
      included, numbered lists keep their starting number, items nested under
      a numbered item line up with its text, and a quote inside a list item
      stays in it. Tables, cells copied from Excel included, become
      GitHub-style pipe tables (`|` in a cell is escaped; merged cells keep
      the columns after them aligned). Text that Markdown would read as
      formatting is backslash-escaped, a code span or fence is made longer
      than any run of backticks inside it, and a link or image address with
      spaces or parentheses is written in angle brackets, with line breaks
      removed. Formatting is written only around real text, so empty bold
      never becomes a rule.

    Preferences > Markdown > **Paste** sets what **Ctrl+V** (default *Lists*) and
    **Ctrl+Shift+V** (default *Plain*) do; **Paste as Markdown** (Markdown
    menu / right-click) always does the full conversion. A plain-text
    clipboard (no HTML) always pastes plain, and with Basic Markdown support
    off every paste stays plain. The conversion runs in a bounded, self-
    contained core module (`html_md.c`), so hostile or huge clipboard input
    cannot run away.
- **Find/Replace escapes**: an "Interpret escapes" checkbox in the Find and
  Replace dialogs makes both fields interpret `\n \r \t \\ \uXXXX` (with the
  same rules as Convert Delimiters above), so line
  breaks can be searched for and inserted. Search history records the text
  exactly as typed. This is part of the Basic Markdown support feature set, so
  the checkbox (the last option in each dialog) appears only when Markdown
  support is enabled and stays inert when it is off.
- **Check for Updates** (Help menu) and the **Updates** preferences tab:
  off by default - npad makes no network calls unless you opt in. A manual
  Help > Check for Updates always works. When a newer, non-skipped version is
  known, that single Help item transforms in place into **Update Available
  (v…)...** (which opens the Updates tab) and reverts to **Check for
  Updates...** afterwards. The Updates tab adds optional automatic surfacing
  via a mode picker (Off / Notify silently with a Help-menu dot + the
  transformed item / Prompt / Download and install automatically) plus an
  on-launch check toggle, a Skip this version action, and an **Install Now**
  button beside the latest version - enabled whenever a newer, non-skipped
  version is known, whatever the notification mode, so arriving here from the
  silent notification gives you a way to act on the update without running a
  second check (see the Updates settings table above). Every check queries the GitHub
  releases API on a worker thread (the UI never blocks); when a newer release
  exists you can download and install it (verified before anything runs, see
  *Verification* under the Updates settings), open the release notes, skip the
  version, or do nothing. Even the automatic mode asks once before
  installing; installing closes npad's windows without a save prompt, parking
  unsaved documents (see *What Install Now does* above). A portable copy's
  Install Now installs npad rather than updating the portable exe (see
  **Portable exe** under [Installation](#installation-windows)).
  Launch/automatic checks fail silently; a manual check reports errors,
  naming what went wrong - GitHub's rate limit (HTTP 403 or 429: try again in
  an hour), a missing release (404), a server error - and says "Could not
  reach github.com" only when no answer came back. Help > Check for Updates
  while the launch check is still running waits for it and reports its
  result. Preferences > Updates refreshes after Check Now, and the messages it
  starts appear over the Preferences window.
- **Highlight all matches**: a checkbox in the Find and Replace dialogs
  washes every match of the search text with a translucent amber overlay
  (capped at 10,000 matches). It is drawn over the text rather than stored in
  it - it never marks the document modified, never enters the undo history,
  and clears when the dialog closes or the box is unticked. It lights up as
  soon as the dialog opens with the box ticked, and follows a term picked from
  the search history list. The overlay tracks
  live (coalesced ~8x/second) as you type in either the search box or the
  document, and re-appears correctly after the match count drops to zero and
  back.
- **Debug diagnostics**: a hidden Preferences page (Ctrl+Shift+. or
  Shift+click the Preferences menu item) shows the startup phase profile,
  settings/recovery paths and counts, and live paint/selection counters,
  with a Copy Diagnostics button - useful when reporting performance issues.
- **Reset All Preferences** (Preferences > Backup): restores every
  preference to its default; recent files, window position and the
  Find/Replace dialogs' options and history are kept. The two Find
  preferences on the Editor page, *Fill Find with the selected text* and
  *Wrap around by default*, are reset with the rest, and so is Convert
  Delimiters' remembered history. Works by removing every setting except those
  preserved categories, so preferences added in future rounds reset too (the
  list of what is kept is the same on Windows and Linux). Applies live to all
  open windows, and closes Preferences. If the settings cannot be saved,
  npad says so and nothing is reset.
- **Import Settings** (Preferences > Backup) accepts only an npad settings
  file: a JSON object, in UTF-8 (with or without a BOM) or UTF-16, of at most
  4 MB, with no nested objects or arrays, and at least half of whose keys are
  npad's own. Anything else - another program's `settings.json`, a text file
  - is refused with "That file is not an npad settings file, so nothing was
  imported." and nothing changes. Before importing, npad saves your current
  settings and keeps them beside the live file as `settings.json.bak`
  (replaced by the next import); the new file is written safely, so a full
  disk cannot leave it half written. On Windows, Preferences then closes, so
  OK cannot put the old values back. **Export Settings** writes a copy of
  `settings.json`.
- **Startup**: the window appears before any deferred work (crash-recovery
  scanning) runs, so launch is not delayed by recovery-slot checks.
- **Crash recovery**: with session resume enabled, each window snapshots
  unsaved work to its own recovery slot - on the first edit, then on each
  interval when the document has changed since the last snapshot. After an
  unclean exit, npad asks "npad may have closed unexpectedly. Restore your
  unsaved work?". **Yes** restores; **No** discards the snapshots; anything
  else - Esc or the close box, or the question being ended by an update,
  Close All, signing out or shutting down - keeps them, and they are offered
  again at the next launch. Recovery runs on every launch, including one
  that opens a file (a double-click in Explorer, say): the file keeps its own
  window and recovered documents open in new, cascaded windows; otherwise the
  first one opens in the current window. A restored document is snapshotted
  again at once, and keeps what npad knew about it: whether it came from a
  file npad could only partly load (Save still asks for a new name), whether
  its text could not be shown exactly, and the file's time stamp, so no false
  "changed on disk" question follows while a real change made in the
  meantime is still reported. If a kept document cannot be read (antivirus has
  it locked, say), npad says so and keeps it for the next launch. Slots are
  cleared on clean save/exit. Snapshots belonging to a still-running npad
  instance are ignored, so opening a new window while others are open never
  offers to "restore" their live work: a running npad is recognised by its
  process (on Windows, by its window or an `npad*.exe` name, the portable
  download included) and its start time, so a process number reused after a
  reboot does not hide a crash. Each leftover is claimed by one window before
  it is offered, so two npads started together never both offer it. At
  startup npad also tidies the folder: it hands back documents a window
  claimed but died holding, offers a snapshot that lost its `.meta` as an
  untitled document, and removes empty leftovers. Recovery ids accepted on the
  command line are only npad's own (see [Command line](#command-line)).
- **Handoff (update / restart / logout)**: when something other than the user
  closes npad - installing an in-app update, an installer replacing
  `npad.exe`, Windows restarting or signing out, or on Linux a logout,
  shutdown or a closed terminal - every unsaved document is parked in a
  recovery slot flagged as a *handoff* and reopened automatically on the next
  launch, still unsaved, with **no prompt in either direction** (crash
  leftovers found in that launch are still asked about). A normal quit (X,
  Ctrl+W, Exit) is unaffected and still asks Save / Don't Save / Cancel.
  Installing an update starts the installer first and then closes every npad
  window, not only the one that started it, waiting for them (see *What
  Install Now does* under the Updates settings). On Windows, npad parks its
  work when it gets `WM_QUERYENDSESSION` and then lets the restart go ahead.
  If a document cannot be parked (a full disk, an unwritable profile), it
  holds the restart instead: Windows lists npad among the apps preventing
  shutdown, with the reason "npad has unsaved changes that could not be kept
  safe. Switch to npad to save them." (you can still force it), and an
  installer reports it as an application it could not close. npad also
  registers for relaunch via `RegisterApplicationRestart` while it has unsaved
  work (subject to Windows' own "restart my apps" setting - the parked
  documents restore on the next launch either way). Handoff snapshots
  deliberately ignore `session_resume_enabled`: that setting suppresses
  *crash* nagging, and honouring it here would silently destroy the buffer;
  a document restored from a handoff stays protected even with crash recovery
  off. Only modified documents are parked, since restored content always
  returns flagged as unsaved.
- **Dark schemes**: the scroll bars are themed along with the text area. The
  **status bar and menu bar stay light in every scheme** - Windows offers no
  supported way to recolour either without also losing its modern rendering. Scroll-bar theming needs Windows 10 1809 or later (npad asks
  uxtheme to force it); on older builds they stay light. The mode follows
  **npad's own colour scheme**, not the Windows one, so a dark npad on a
  light-themed Windows still gets dark chrome and a light npad on a
  dark-themed Windows stays light throughout. The light schemes keep the
  system default status-bar painting, so their appearance is unchanged from
  earlier versions.
- **Drag & drop**: drop a file to open it; hold Ctrl to insert its contents
  at the caret instead. Ctrl+drop asks the same questions as File > Open: it
  confirms a file that does not look like text, a file over the large-file
  size (`large_file_warning_mb`, 0 turns that off), and a file of which npad
  can insert only the part before binary data, and it says so when the file
  cannot be read.
- **Large files**: opening a file over the configured threshold asks for
  confirmation first.
- **Folders**: opening a folder (from the command line, say) reports
  "'*path*' is a folder, not a file".
- **Recent Files** (File menu, up to ten): on Windows the list is shown as
  stored, so an entry on an unplugged drive or a sleeping network share stays
  visible (and never slows the menu down), and picking one that is missing
  reports "Could not open file". Opening a file never drops entries that are
  unreachable at that moment; they age out only as newer files are opened.
  Clear Recent Files reaches every open window at once.
- **Binary files and saving**: npad holds text as NUL-terminated strings, so a
  file containing a NUL byte loads only up to that point. When that happens npad
  **will not save over the original** - Save behaves as Save As, starting at
  `photo (text).txt` (for `photo.png`) in the same folder, and the original is refused as the
  target under any name (another spelling of its path, or a hard link to it),
  so the file it could not read in full is never overwritten with the
  fragment. Auto-save skips such documents entirely. This is detected when the
  file is decoded, so it also covers files that do not trip the binary-looking
  warning below, and it survives crash recovery and a restore after an update.
- **Binary files**: a file whose first bytes look binary (NUL bytes outside
  UTF-16 text, with or without a BOM, or mostly control characters) prompts
  with **Cancel / Open in npad / Open with the default app** before anything
  else happens - the current document is untouched unless "Open in npad" is
  chosen.
- **Windows dialogs**: File > Open uses the same modern Open dialog as Save
  As. If the Open, Save As, Export or Import dialog itself fails, npad says
  so instead of acting as if you had cancelled.
- **Window position** (Windows): a window whose title bar would be off the
  screen - a monitor unplugged, a taskbar moved - is moved back where it can
  be grabbed, and a window closed maximized opens maximized at once.
- **npad run as administrator** (Windows) receives what the other npad
  windows send - settings changes, Close All, an update's request to close -
  like any other window. A normal npad accepts those messages only from
  programs running at its own level.
- **A fatal start-up error** (Windows), such as the window failing to open,
  is shown in a message box rather than npad silently not starting.
- **Dialog placement**: every dialog (Find/Replace, Go To Line, Convert
  Delimiters, Custom Indent) opens at the same notepad-style offset into
  the npad window; Find/Replace additionally remembers where you moved it
  for the rest of the session.

## Versioning & changelogs

npad follows [semver](https://semver.org). While on 0.x: **minor** bumps for
feature rounds or breaking changes, **patch** bumps for fix-only rounds.
Every change updates:

1. `src/main.h` + `src/platform/npad.rc` - the version numbers
2. `CHANGELOG.md` - curated per-version notes (the commit-level history is
   `git log`, so there is no separate changes file)

`scripts/check-release-version.sh <tag>` checks that these agree before a
release builds anything (CI runs its untagged form on every change): the tag
is `v<x>.<y>.<z>`, optionally followed by `-` and a pre-release of letters,
digits and dots; `main.h` and every version in `npad.rc` are its version (a
pre-release's base version, `0.36.0` for `v0.36.0-rc2`); and
`CHANGELOG.md`'s newest entry is this version, with its highlights. A final
release also needs a dated entry (not "Unreleased") and the same version as
the newest release in the Linux AppStream metainfo. A release is then made as
a draft, filled with every download and checked (each file's `.sha256`, a
complete `CHECKSUMS.txt`), and published only at the end, so the in-app
updater never sees a release whose files are still arriving. A run that
fails leaves an unpublished draft, which re-running the same tag reuses; a
tag that is already published is refused. `scripts/release.sh <tag>` runs the
gate and the local checks and prints the commands to tag and to dry-run;
only `--push` tags and pushes.
