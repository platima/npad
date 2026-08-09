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
| `npad-v<v>-setup-win-x64.exe` | Interactive installer (Inno Setup) |
| `npad-v<v>-msi-win-x64.msi` | Silent/managed deployment (`msiexec`) |
| `npad-v<v>-portable-win-x64.exe` | Portable - run from anywhere, no install |

**SmartScreen / Defender**: releases are not code-signed yet, so Windows
may block or warn about the downloaded installer or portable exe. Fix:
right-click the file → **Properties** → tick **Unblock** → **Apply** (this
clears the mark-of-the-web), or choose "More info" → "Run anyway" on the
SmartScreen prompt. When unsure, verify the file's SHA256 against the
release's `CHECKSUMS.txt` before unblocking.

**Install scope**: both installers default to a **per-user** install
(`%LOCALAPPDATA%\Programs\npad`, HKCU registry). System-wide
(`Program Files`, HKLM) is used when the installer runs elevated or when
chosen: the setup EXE asks (or accepts `/ALLUSERS`); the MSI takes
`ALLUSERS=1`.

**Setup EXE options** (interactive or `/VERYSILENT /SUPPRESSMSGBOXES`):
- *Components*: bundled fonts - Intel One Mono (monospace), Roboto
  (proportional), OpenDyslexic (reading assistance); all SIL OFL licensed,
  installed on by default. Per-user installs use the Windows 10 1809+
  per-user font store.
- *Tasks*: **Add npad to PATH** (on by default - see below), file
  associations grouped into **Text** (`.txt`, on by default), **Markdown &
  documents** (`.md`, `.markdown`), **Data** (`.csv`, `.tsv`, `.json`, `.xml`,
  `.yaml`, `.yml`, `.toml`), **Config** (`.ini`, `.cfg`, `.conf`) and **Logs**
  (`.log`) - all opt-in except Text; 'notepad' alias (on by default - see
  below), "set the bundled fonts as npad's default editor fonts" (**off by
  default** - out of the box npad should look like notepad.exe, and this
  rewrites the editor fonts; offered when the fonts are being installed or
  already present; updates settings.json in place, preserving all other
  settings - tick it or use `/MERGETASKS="fontdefaults"` to opt in), desktop
  icon (off).

**MSI features** (silent-deployment oriented, no UI - use `/qn` or `/qb`):
`Main`, `PathEnv`, `OpenWith`, `AssocText`, `NotepadAlias`, `Fonts` install by
default; `AssocMarkdown`/`AssocData`/`AssocConfig`/`AssocLog` via `ADDLOCAL`, e.g.
`msiexec /i npad-v<v>-msi-win-x64.msi /qn ADDLOCAL=Main,AssocText,NotepadAlias,AssocData`.
Fonts install in machine-wide mode only (`ALLUSERS=1`): Windows MSI resolves
the fonts folder to `C:\Windows\Fonts` regardless of scope, so the feature
is skipped on per-user installs (use the setup EXE for per-user fonts).

**npad on PATH**: App Paths (below) only serves Win+R and ShellExecute;
cmd.exe and PowerShell resolve a bare `npad` from PATH alone. The `addtopath`
task / `PathEnv` feature adds the install folder to PATH so `npad` and
`npad file.txt` work from a terminal - the user PATH for a per-user install,
the machine PATH for an all-users install. A new terminal session picks it
up; uninstall removes the entry.

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

**Default editor**: Windows does not let installers set the default app for
a file type. The installers register npad for the chosen extensions (it
appears in "Open with" and Default Apps); the setup EXE offers the Default
Apps Settings page after install to make it the default for .txt.

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

**Installing over a running npad**: setup closes it first so the executable can
be replaced — npad's normal save prompt still appears if you have unsaved work.
Only in that case does the last page offer **Relaunch npad**; if npad was not
running, no launch option is shown. Windows' Restart Manager is prevented from
restarting it automatically, so the checkbox is the single place that decides.
Setup spots a running npad by its window, and additionally accepts
`/RELAUNCH=1` — which npad passes when it launches the installer for an in-app
update, since by then it has closed itself and there is no window left to find.

**Uninstall** removes the program, shortcuts and every registry entry the
installer wrote, but keeps your settings (`%APPDATA%\Platima\npad`) and any
installed fonts.

**Building the installers** (repo checkout, Windows): `pwsh
installer/build-installers.ps1` - needs Inno Setup 6, the WiX dotnet tool
and a built `npad.exe`; fonts are downloaded from SHA256-pinned upstream
releases by `installer/fetch-fonts.ps1`. CI builds both via
`.github/workflows/installers.yml`.

## File locations

| What | Where (Windows) |
|------|-----------------|
| Settings | `%APPDATA%\Platima\npad\settings.json` |
| Crash-recovery snapshots | `%APPDATA%\Platima\npad\recovery\<slot>.txt` / `.meta` |

Linux (when implemented): `~/.config/npad/`.

## Settings reference

All settings live in `settings.json` and are editable in Preferences
(Edit menu, Ctrl+,) unless noted. Types are stored as JSON strings.

### General (Preferences > General)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `auto_save_enabled` | bool | `false` | Silently save named documents on a timer. Off by default: overwriting the file from a timer is destructive, so it is opt-in. Untitled documents are never auto-saved. |
| `auto_save_interval` | int | `300` | Auto-save period in seconds (minimum 10). |
| `large_file_warning_mb` | int | adaptive | Confirm before opening files larger than this (MB). `0` disables the prompt. When never set, the default scales with installed RAM (1/64th, clamped 50-1024 MB; 100 MB when memory size is unknown). |
| `recent_files_max` | int | `10` | Recent Files menu length (0-10). |
| `session_resume_enabled` | bool | `true` | Crash recovery: snapshot unsaved work on a timer; offer to restore after an unclean exit. On by default: snapshots never touch the user's file. All windows are restored, extras in their own cascaded windows. Does **not** gate handoff snapshots taken when an update or a Windows restart closes npad - those would otherwise be silently discarded. |
| `session_interval` | int | `30` | Snapshot period in seconds (minimum 5). |
| `ctrl_n_new_window` | bool | `false` | Swap Ctrl+N / Ctrl+Shift+N between "New" (clear this window) and "New Window" (open another instance). |

### Appearance (Preferences > Appearance)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `theme` | string | `light` | Colour scheme: `light`, `dark`, `system` (follow OS), `solarized-light`, `solarized-dark`. |
| `monospace_font` | string | `Consolas` | Face used in monospace mode (picker button). |
| `proportional_font` | string | `Segoe UI` | Face used in proportional mode (picker button). |
| `font_size` / `font_weight` / `font_italic` | int/int/bool | `11`/`400`/`false` | Shared font metrics, set via the font pickers or Format > Font. |
| `opendyslexic_enabled` | bool | `false` | Use the OpenDyslexic font (reading assistance). Requires the font to be installed; reverts with a hint if it is not. |
| `opendyslexic_font` | string | `OpenDyslexic` | Face used when OpenDyslexic mode is on. |
| `status_bar_visible` | bool | `true` | Show the status bar (also View > Status Bar). |
| `icon_style` | string | `system` | Which app icon to show: `system` follows the Windows **taskbar** light/dark setting (the surface the icon appears on), `npad` follows npad's own colour scheme, `light` / `dark` pin one variant, `classic` restores the original pre-0.28 icon. Preferences > Appearance. |
| `sync_view_state` | bool | `false` | Mirror per-window view changes (font type, zoom) live to every open npad window. |
| `status_show_counts` | bool | `false` | Show word / character / line counts in the leftmost status bar segment. Refreshed live while typing (coalesced ~8x/second); documents over ~1 MB fall back to a settle-then-count debounce. Shared with transient messages, which win until the next change. Numbers are grouped using your Windows regional settings (e.g. `1,234,567`), including any separator customised in Control Panel. |

### Defaults (Preferences > Defaults) - initial state for new windows/files

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `default_encoding` | int | `0` (UTF-8) | Encoding for new files: 0 UTF-8, 1 UTF-8 BOM, 2 UTF-16 LE, 3 UTF-16 BE, 4 ANSI. |
| `default_line_ending` | int | `0` (CRLF) | Line endings for new files: 0 CRLF, 1 LF, 2 CR. |
| `default_font_mono` | bool | `true` | New windows start in monospace (`true`) or proportional (`false`) mode. **Monospace by default**, which is what mirrors notepad.exe: it has a single font setting and ships it as Consolas 11 - the same face and size as npad's monospace default. |
| `default_zoom` | int | `100` | New windows' zoom percent (10-500). |
| `auto_update_defaults` | bool | `false` | View changes (font type, zoom) immediately become the new defaults. |

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
| `update_mode` | string | `"off"` | How a found update surfaces: `off` (manual only), `notify` (a **●** on the Help menu title + an "Update Available" item, no dialog), `prompt` (show the update dialog), `auto` (download + SHA-256-verify silently, then one confirmation before installing). |
| `update_check_on_launch` | bool | `false` | Run a single check once after the window first paints (primary window only). Automatic/launch checks fail silently. |
| `update_skipped_version` | string | (unset) | A version the user chose to **Skip**; suppresses the dot/prompt for exactly that version until a newer release appears. |
| `update_latest_version` | string | (unset) | Read-only state: the latest release tag seen by the last successful check (shown on the Updates tab). |
| `update_last_checked` | string | (unset) | Read-only state: local `YYYY-MM-DD HH:MM` of the last check (shown on the Updates tab). |

Installing an update runs the downloaded, checksum-verified installer and
closes npad through the normal save-changes prompt; a fully seamless
no-close update is not possible because Inno/MSI must replace the running
`npad.exe`. Portable-exe users get the same verified installer download.

### Other persisted state (no dedicated UI)

| Key | Description |
|-----|-------------|
| `word_wrap` | Word wrap on/off (Format > Word Wrap, Alt+Z). |
| `window_x/y/width/height/maximized` | Window geometry, saved on exit. First run uses ~48% x ~72% of the monitor work area, centred. |
| `recent_file_0..9` | Recent files list. |
| `find_match_case`, `find_whole_word`, `find_search_down`, `find_wrap_around`, `find_interpret_escapes`, `find_highlight_all` | Find/Replace options (checkboxes in the dialogs). `find_wrap_around` defaults **off**, matching notepad.exe, and also has a Preferences > General checkbox. All propagate live to other instances. |
| `find_hist_0..9`, `replace_hist_0..9` | Recent search/replace terms. |

## View state vs settings

Font type (mono/proportional) and zoom are **per-window view state**, like
the caret position: changing them affects only that window and is not saved.
New windows start from the Defaults-tab values. Two preferences extend this:

- **Sync view across all instances** (Appearance): view changes mirror live
  to every open npad window.
- **Auto-update defaults in real time** (Defaults): view changes are saved
  as the new defaults for future windows.

Everything else (theme, word wrap, status bar, fonts, timers, defaults) is a
shared setting: changing it anywhere saves `settings.json` immediately and
propagates live to all open npad windows.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+N | New (or New Window, per preference) |
| Ctrl+Shift+N | New Window (or New, per preference) |
| Ctrl+O / Ctrl+S / Ctrl+Shift+S | Open / Save / Save As |
| Ctrl+W / Ctrl+Shift+W | Close this window / Close all windows (each save-checked) |
| Ctrl+Z / Ctrl+Y | Undo / Redo (100,000-step history) |
| Ctrl+X / Ctrl+C / Ctrl+V / Del | Cut / Copy / Paste / Delete |
| Ctrl+Shift+V | Alternate paste (Markdown tools): the mode configured in Preferences > Markdown > Paste (default plain). Plain otherwise. |
| Ctrl+A | Select All |
| Ctrl+F / F3 / Shift+F3 / Ctrl+H | Find / Find Next / Find Previous / Replace |
| Ctrl+G | Go To Line |
| F5 | Insert time/date |
| Alt+Z | Toggle word wrap |
| Ctrl+M | Toggle monospace (this window) |
| Ctrl+E | Cycle line endings (CRLF > LF > CR) |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0 | Zoom in / out / reset (this window; Ctrl+Scroll also zooms) |
| Ctrl+, | Preferences |
| Ctrl+Shift+. | Preferences opened on the hidden Debug page (also: Shift+click the Preferences menu item) |
| Tab / Shift+Tab | Indent / Unindent (Markdown tools). Tab indents any selection; without one it types a tab. Shift+Tab always unindents - the selected lines, or the current line. A preference switches this binding to Ctrl+] / Ctrl+[. |
| Ctrl+X (no selection) | Cut the whole current line (Markdown tools); Ctrl+V then pastes that line above the current line, keeping the caret in place, until the clipboard changes |
| Enter (on a list line) | Continue the list: the new line starts with the same indent + marker (Markdown tools). On an empty bullet, removes the marker and inserts a plain newline (ends the list). |

## Status bar

Segments left to right; several are clickable:

| Segment | Shows | Click action |
|---------|-------|--------------|
| Message | Transient messages (e.g. "Match 3 of 7"), and the optional word / character / line counts (`status_show_counts`; messages win until the next text change) | - |
| Ln, Col | Caret position | Open Go To Line |
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
  -h, --help      Show help
  -v, --version   Show version
```

Internal options used by npad itself: `--recover <slot>` (reopen a specific
crash-recovery slot) and `--cascade <n>` (offset a recovery window's
position). Not intended for direct use.

## Behaviour notes

- **Atomic saves**: files are written to a temp file, verified, then renamed
  over the original. A failed save never destroys the existing file.
- **Unsaved-changes prompt**: closing or replacing a modified document asks
  "Do you want to save changes to ...?" with **Save / Don't Save / Cancel**,
  matching notepad.exe. Esc, the close box, and anything other than an explicit
  Save or Don't Save all cancel - i.e. they abort the close and keep your
  document, never discard it.
- **Scroll bars are always shown** (greyed out when the content fits), as in
  classic Notepad, rather than appearing and disappearing. This also keeps the
  text area a constant width, so the wrapped layout does not jump when the
  document grows past one screen. With word wrap on there is no horizontal bar
  at all; with it off, the horizontal bar follows the same always-shown rule.
- **Clicking an unfocused window acts immediately**: the click that activates
  npad also places the caret, and a click-drag starts selecting - no need to
  click once to focus and again to act.
- **Encodings & line endings**: detected on open (BOM + heuristics) and
  preserved on save. Line endings are changeable via the status bar or
  Format > Line Endings; encoding via the status bar or the Save As dialog's
  Encoding dropdown. Pure-ASCII files always report UTF-8 (ASCII bytes are
  identical across UTF-8 and ANSI).
- **Lossy ANSI saves warn first**: manually saving as ANSI when the document
  contains characters the system code page cannot represent (emoji, most
  non-Latin scripts) asks before replacing them with `?`; declining cancels
  the save. Auto-save skips such documents silently rather than prompting
  from a timer (session snapshots still protect the content).
- **Emoji & mixed scripts**: characters the configured font lacks (emoji,
  CJK, etc.) render via Windows font fallback - when typed, when a file is
  opened, and across font or theme changes.
- **Markdown list tools** (Preferences > Markdown, off by default): add a
  **Markdown** menu and right-click items.
  - *Sort / Unique* treat each line as an entry. If the selection spans more
    than one line, only those lines are affected (extended to whole lines);
    otherwise the whole document is. Unique keeps the first occurrence.
  - *Convert Delimiters…* is a find/replace for delimiters; both fields
    interpret `\n \r \t \\ \0 \uXXXX`, so e.g. From `,` To `\r\n` turns a
    comma-separated line into separate lines. It applies to the selection
    (when "Selection only" is ticked) or the whole document.
    Line breaks are matched as `\n` whatever the file's line ending is - the
    document is normalised internally and the file's own ending restored on
    save - so a **From** of `\r\n` is accepted and treated as `\n` rather than
    silently matching nothing.
  - *Indent / Unindent* (Tab / Shift+Tab on a selection by default - a
    preference switches to Ctrl+] / Ctrl+[ - or the Indent submenu) prefix
    each target line. The formats are spaces, tab, `* `, `- `, ` * `, ` - `
    (markers include a trailing space) and *Custom…*, which prompts for any
    prefix (escapes `\t \\ \uXXXX` allowed) and remembers it. Indenting a
    line that already has its marker adds two spaces instead of a second
    marker (markdown-style nesting); unindent removes one unit - two nesting
    spaces, then the marker itself. Whitespace formats add/remove a tab or
    up to four spaces. Each operation is a single undo step. Line scoping
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
    word processor, etc.), paste can convert it. Two depths - **Lists** turns
    `<ul>`/`<ol>` (with nesting) into list lines in your current bullet style
    and flattens the rest to plain text; **Markdown** does a full conversion
    (headings, bold, italic, inline code and fenced code blocks, links,
    images, block quotes, rules, numbered ordered lists). Preferences >
    Markdown > **Paste** sets what **Ctrl+V** (default *Lists*) and
    **Ctrl+Shift+V** (default *Plain*) do; **Paste as Markdown** (Markdown
    menu / right-click) always does the full conversion. A plain-text
    clipboard (no HTML) always pastes plain, and with Basic Markdown support
    off every paste stays plain. The conversion runs in a bounded, self-
    contained core module (`html_md.c`), so hostile or huge clipboard input
    cannot run away.
- **Find/Replace escapes**: an "Interpret escapes" checkbox in the Find and
  Replace dialogs makes both fields interpret `\n \r \t \\ \uXXXX`, so line
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
  exists you can download and install it - the installer and its published
  `.sha256` are fetched to the temp directory and the SHA-256 checksum verified
  before anything runs (a mismatch deletes the download and aborts) - open the
  release notes, skip the version, or do nothing. Even the automatic mode asks
  once before installing; installing closes npad through the normal
  save-changes prompt. Portable-exe users get the same verified installer
  download. Launch/automatic checks fail silently; a manual check reports errors.
- **Highlight all matches**: a checkbox in the Find and Replace dialogs
  washes every match of the search text with a translucent amber overlay
  (capped at 10,000 matches). It is drawn over the text rather than stored in
  it - it never marks the document modified, never enters the undo history,
  and clears when the dialog closes or the box is unticked. The overlay tracks
  live (coalesced ~8x/second) as you type in either the search box or the
  document, and re-appears correctly after the match count drops to zero and
  back.
- **Debug diagnostics**: a hidden Preferences page (Ctrl+Shift+. or
  Shift+click the Preferences menu item) shows the startup phase profile,
  settings/recovery paths and counts, and live paint/selection counters,
  with a Copy Diagnostics button - useful when reporting performance issues.
- **Reset All Preferences** (Preferences > Backup): restores every
  preference to its default; recent files, window position and Find/Replace
  options and history are kept. Works by removing every setting except those
  preserved categories, so preferences added in future rounds reset too.
  Applies live to all open windows.
- **Startup**: the window appears before any deferred work (crash-recovery
  scanning) runs, so launch is not delayed by recovery-slot checks.
- **Crash recovery**: with session resume enabled, each window snapshots
  unsaved work to its own recovery slot. After an unclean exit, npad offers
  to restore; the first document opens in the current window and the rest in
  new, cascaded windows. Slots are cleared on clean save/exit. Snapshots
  belonging to a still-running npad instance are ignored, so opening a new
  window while others are open never offers to "restore" their live work.
- **Handoff (update / Windows restart)**: when something other than the user
  closes npad - installing an in-app update, or Windows restarting the machine
  - every unsaved document is parked in a recovery slot flagged as a *handoff*
  and reopened automatically on the next launch, still unsaved, with **no
  prompt in either direction**. A normal quit (X, Ctrl+W, Exit) is unaffected
  and still asks Save / Don't Save / Cancel. Installing an update closes every
  npad window, not only the one that started it, and waits for them before
  launching setup. npad answers `WM_QUERYENDSESSION` immediately so it never
  blocks a restart, and registers for relaunch afterwards via
  `RegisterApplicationRestart` (subject to Windows' own "restart my apps"
  setting - the parked documents restore on the next launch either way).
  Handoff snapshots deliberately ignore `session_resume_enabled`: that setting
  suppresses *crash* nagging, and honouring it here would silently destroy the
  buffer. Only modified documents are parked, since restored content always
  returns flagged as unsaved.
- **Dark schemes**: the scroll bars and status bar are themed along with the
  text area. Scroll-bar theming needs Windows 10 1809 or later (npad asks
  uxtheme to force it); on older builds they stay light. The mode follows
  **npad's own colour scheme**, not the Windows one, so a dark npad on a
  light-themed Windows still gets dark chrome and a light npad on a
  dark-themed Windows stays light throughout. The light schemes keep the
  system default status-bar painting, so their appearance is unchanged from
  earlier versions.
- **Drag & drop**: drop a file to open it; hold Ctrl to insert its contents
  at the caret instead.
- **Large files**: opening a file over the configured threshold asks for
  confirmation first.
- **Binary files**: a file whose first bytes look binary (NUL bytes outside
  UTF-16, or mostly control characters) prompts with **Cancel / Open in
  npad / Open with the default app** before anything else happens - the
  current document is untouched unless "Open in npad" is chosen.
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
