# npad on Linux - the GTK 3 port

Status, conventions and the remaining work for the Linux desktop build. The
Windows build is the reference: anything not called out below as a deliberate
difference should behave exactly as it does there (see
[DOCUMENTATION.md](DOCUMENTATION.md)). Read [LESSONS.md](LESSONS.md) first, in
particular its **GTK / Linux** section.

## Status (v0.35.0 - every Windows feature ported)

Every command and preference of the Windows build now works here: batches
A to F below are done, and `command_available()` in `ui_gtk.c` no longer
withholds anything. What remains is checking each batch on a real X11 and
Wayland desktop (the "Needs testing" lists in [TODO.md](TODO.md)), and the
deliberate differences listed further down.

**Verified** (Broadway under WSL): the window, menu bar and status bar render;
opening a file from the command line (UTF-8 and emoji intact, path stored
absolute); typing and the modified marker; undo/redo with per-word
coalescing; Save, preserving LF and UTF-8 byte for byte; crash recovery
(snapshot on the first edit, then every 30 s); logout handling (SIGTERM parks
unsaved work, the next launch restores it silently); and D-Bus: instance names
registered, a Word Wrap change broadcast, saved and picked up live by a second
window.

**Implemented, not yet exercised:** Save As and its encoding picker, the save
and binary-file prompts, recent files menu, monospace toggle, zoom, the colour
schemes, the status-bar pickers, word/char/line counts, Time/Date, New
Window, Close All, clipboard commands, About. Worth a pass on a real desktop
before the feature batches build on them.

**Batch A (v0.34.0):** Find, Find Next / Previous, Replace, Replace All and Go
To are ported (details under the checklist). Verified in Broadway: both
dialogs, Up / Down, wrap, highlight all, whole word, Replace All undone in
one step, Ctrl+F / H / G from inside the dialog, Go To by shortcut and by the
Ln/Col click, and the beyond-the-end message; the search engine itself
(non-ASCII text, case folding, whole word, escapes, history) was also run
headless against a real GtkTextBuffer.

**Printing (v0.34.0)** is verified under Broadway with GTK's Print to File
backend: a multi-page document, a long wrapped line, a 40,000-word single
line (every word present and in order across the 64 KB layout chunks),
header/footer codes in all three positions, landscape A4 with custom margins,
the default Letter/A4 setup, an empty document, Page Setup round-tripping
through `settings.json`, and the preview (opened from the menu and from the
print dialog's Preview button, paging, Page Setup from inside it). Not tried:
a real CUPS printer.

**Batch B (v0.35.0):** Preferences and Format > Font are ported (`ui_gtk_prefs.c`).
Verified in Broadway: every page rendered and driven (checkboxes, spin
buttons, text fields, the paste radios, the sensitivity of dependent
controls), only the changed keys written to `settings.json`, the
OpenDyslexic-not-installed refusal, the font chooser (from the menu and from
Appearance), Export / Import / Reset, live propagation to a second window
(status bar, font) and the `SettingsChanged` broadcast on `dbus-monitor`, the
hidden Debug page from Ctrl+Shift+., and the Make-default action against an
isolated `HOME`. Not tried: Shift+click on the menu item (the Shift check
reads `gtk_get_current_event_state`) and `xdg-mime` on a real desktop that
tracks its own default-app database.

**Batch D1 (v0.35.0):** the context menu, file drops, external-change detection,
Ctrl+wheel zoom and the permanent scroll bar are ported (details under the
checklist). Verified in Broadway: the menu's items and greying, a Ctrl+drop and
a plain drop, an external edit and delete, the wheel zoom (including
smooth-scroll deltas and the 10-500% limits) and the scroll bars with short
and long text. Not verifiable there, so worth a pass on Mint: the context menu
popup itself, a real drag from the file manager (and whether Ctrl is seen
while dragging, X11 and Wayland), and real wheel and touchpad input.

**Batch D2 (v0.35.0):** the Markdown list tools are ported (`ui_gtk_lists.c`):
the Markdown menu and its context-menu mirror, Tab / Shift+Tab (or Ctrl+] /
Ctrl+[), Enter continuing a list, Ctrl+X cutting a line and the next Ctrl+V
pasting it above, and rich-text paste. Verified with a harness compiled into the
real process under Broadway (151 checks: every transform and its single undo
step, the menu appearing and disappearing live, the greying, mnemonic
uniqueness in both menus, the dialogs driven through their widgets, real key
events for Tab / Enter / Ctrl+X / Ctrl+V, the paste modes against an HTML
clipboard, the HTML payload decoding) and by hand in the real binary (Tab over a
selection, one Ctrl+Z). Not verifiable there, so worth a pass on Mint: the Markdown
menu and the popup actually opening, a real browser's `text/html` clipboard
(Broadway does not share the clipboard between processes), Ctrl+] / Ctrl+[ on
a layout where the brackets need AltGr.

**Check for Updates (v0.36.0)** notifies only: Help > Check for Updates and an
Updates page in Preferences, with the Windows keys and defaults but no install
step (see the deliberate differences). Verified: the real lookup against
GitHub over GIO TLS from a harness, and the clear failure when no TLS backend
is loaded (`GIO_MODULE_DIR` at an empty folder); `dpkg -S` ownership and the
apt-source scan (a throwaway `npad.sources`, a commented-out `.list`); in
Broadway with a build numbered v0.34.0 and an isolated `HOME`: the up-to-date
and update-available dialogs (portable advice, the release link, Skip writing
`update_skipped_version`), the Help-menu dot and "Update Available" opening
Preferences on Updates, Check Now refreshing the page live, Skip greying and
clearing the dot, Apply writing only `update_check_on_launch`, and the launch
check under a private `dbus-run-session` (with other npad windows on the
session bus it rightly stands down). Not verified: a real apt / dnf / pacman /
AUR install reporting its own advice, and a desktop without glib-networking.

**Batch F (v0.35.0):** the `system` colour scheme follows the desktop live, the
settings directory honours `$XDG_CONFIG_HOME`, an off-screen saved position is
repaired on X11, and the installed icon is used everywhere. Verified: the
scheme switching in Broadway against a portal stub (light, dark, light again;
a repeat of the same value and an unrelated key ignored; no portal falls back
to GTK's `gtk-application-prefer-dark-theme`), the XDG directory and its
fall-back to the old file against a throwaway `HOME` (old file byte-identical
afterwards) plus unit tests, and the position test in a harness with a faked
1920x1080 monitor. Not verifiable here, so worth a pass on Mint: the real
Cinnamon / GNOME portal values, the position repair on a real X11 session
with a monitor removed, and the icon from an installed package.

## Building and running

```bash
sudo apt-get install build-essential pkg-config libgtk-3-dev
make linux-gtk          # produces ./npad-linux
./npad-linux [file]
```

One binary covers X11 and Wayland - GDK picks the display server at runtime.
`make linux` also builds the (stub) terminal variant; `make install` puts the
desktop build at `/usr/local/bin/npad`.

Debian 12 / 13, Linux Mint 22 and LMDE all ship GTK 3.24, which is what this
targets. Nothing beyond GTK (and the GLib/Pango it brings) is linked: the
D-Bus messaging is GLib's own GDBus.

## Testing from Windows (WSL)

The maintainer's machine has WSL Debian with `libgtk-3-dev`, GCC and the
MinGW cross-compiler. To build without polluting the Windows checkout, mirror
it into the WSL home and build there:

```bash
rsync -a --delete --exclude '*.exe' --exclude dist/ --exclude build/ \
  /mnt/d/Repos/npad/ ~/.cache/npad-wsl/tree/
cd ~/.cache/npad-wsl/tree && make test linux-gtk
```

Keep the mirror at least three directories below `/`: the existing
`path_traversal_protection` test reads `../../../etc/passwd` and fails from a
shallow path (LESSONS.md).

To look at the running app without opening windows on the Windows desktop,
use GTK's **Broadway** backend, which renders the app as a web page:

```bash
broadwayd :5 &
GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 ./npad-linux somefile.txt &
# then open http://localhost:8085 in Claude's browser pane
```

Several sessions may share one WSL: Broadway display `:5` (page on port 8085)
and `:7` (8087) have been taken by others - pick an unused number (`:23` is
port 8103), keep your own PID files, and never `pkill broadwayd` / `pkill npad*`.

To print without a printer, set `NPAD_PRINT_EXPORT=/path/out.pdf` before
launching: Ctrl+P then writes a PDF with no dialog. Inspect it with
`gs -sDEVICE=txtwrite -sOutputFile=- out.pdf` (text and pagination) or
`gs -sDEVICE=png16m -r70 -sOutputFile=p%d.png out.pdf` (look at the pages).

Broadway tips (all learned the hard way - see LESSONS.md): the page only
paints while the browser pane is visible; keystrokes reach whichever npad
window Broadway last focused, so click into a new window first; and
screenshots are scaled (the pane's 1280x720 viewport comes back as 800x450).
When the pane cannot paint, verify through files instead - Ctrl+S and read the
bytes, or watch `~/.config/npad/recovery/` (the core snapshots on the first
edit and then every 30 s). Use an isolated `HOME` so test settings stay
throwaway.

More Broadway traps: the harness's `type` action does not reach GTK entries,
use `key` (`key c a f e`); non-ASCII text cannot be typed at all, so put the
term in the settings history (`find_hist_0`) or the document and drive the
engine headless (a throwaway program that `#include`s `ui_gtk_find.c`
against a real GtkTextBuffer works); combo-box popups do not paint; the
screenshot trails the last action by a frame (wait, then shoot twice); and
parallel sessions share broadwayd - use your own display number
(`broadwayd :7` is port 8087) and a renamed copy of the binaries, because a
sibling's `pkill broadwayd` kills yours too.

More from the Preferences round: the harness `key` action cannot deliver
Ctrl+comma or Ctrl+Shift+period - dispatch `KeyboardEvent`s on `document`
from the javascript tool instead (`key: ','`, `code: 'Comma'`, `keyCode: 188`,
`ctrlKey: true`) - and keys go to whichever npad window Broadway focused last.
With several npad windows on one display, a dialog opens partly off-screen
(top left); drive it by keyboard: a notebook tab with focus takes Right to
change page, Tab walks the controls, Space toggles. Menu popups paint several
seconds late (or not at all): open the menu, then `Up Return` reaches its
last item. `broadwayd :N` serves HTTP on port 8080+N; a second broadwayd on a
taken port logs "Address already in use" and still hands clients a socket, so
read its log, and never copy over a binary that is still running ("Text file
busy") - use a new file name per run.

To test the `system` colour scheme, stand in for the portal: a few lines of
GDBus that own `org.freedesktop.portal.Desktop` on a throwaway
`dbus-run-session` bus, answer `Settings.Read` (reply `(v)` holding a `v`
holding the `u`: 0 none, 1 dark, 2 light) and emit `SettingChanged`
`(ssv)` for `org.freedesktop.appearance` / `color-scheme` themselves. `gdbus
emit` cannot do it: npad subscribes to the portal's own bus name, and a signal
from `gdbus`'s unique name is not from that sender (LESSONS.md). Set
`"theme": "system"` in the isolated `HOME`'s settings, run `broadwayd` under
`timeout` so nothing needs killing, and watch the window chrome in the browser
pane; `G_MESSAGES_DEBUG=all` logs each change of value.

For the settings directory, run with a throwaway `HOME` holding
`.config/npad/settings.json` and `XDG_CONFIG_HOME` pointing at an empty
directory: the new `npad/settings.json` appears with the old content, and the
old file's checksum is unchanged.

Broadway is neither X11 nor Wayland, so window placement and the portal file
chooser still need a check on a real desktop (and its monitor geometry reads
0x0, so the off-screen check declines to judge there).

## Architecture

```
src/platform/ui_gtk.h           private header: Window struct, command enum, cross-file API
src/platform/ui_gtk.c           main window, menus, shortcuts, commands, text, undo, view state,
                                status bar, timers, and the ui_platform_* entry points
src/platform/ui_gtk_dialogs.c   file choosers, message boxes, save / binary prompts, About
src/platform/ui_gtk_edit.c      context menu, file drops, external-change prompt, Ctrl+wheel zoom,
                                inert scroll bars
src/platform/ui_gtk_find.c      Find / Replace / Go To
src/platform/ui_gtk_lists.c     Markdown list tools: menu, transforms, Tab / Enter, cut line, rich paste
src/platform/ui_gtk_prefs.c     Preferences dialog and Format > Font
src/platform/print_gtk.c        Page Setup, Print and Print Preview (GtkPrintOperation)
src/platform/ui_gtk_instance.c  cross-instance messaging over D-Bus, and the desktop
                                light/dark preference from the xdg-desktop-portal
src/core/undo.c                 platform-neutral undo history (tests/test_undo.c)
```

### Conventions

- **Text model.** The GtkTextBuffer holds `\n` only. `ui_set_text` normalises
  CRLF / CR on the way in; `ui_get_text` returns `\n` and the core converts to
  the document's line ending when it saves. Invalid UTF-8 is repaired with
  `g_utf8_make_valid` rather than dropped.
- **Positions are GTK character offsets** (Unicode code points) everywhere in
  the GTK layer and in the undo history. Never mix in byte indexes
  (`gtk_text_iter_get_line_index` and friends) without converting.
- **Undo.** GTK 3's buffer has no undo, so `ui_gtk.c` records every edit from
  the buffer's `insert-text` / `delete-range` signals into `core/undo.c`.
  Edits inside a GTK *user action* are held and flushed at its end: one edit is
  recorded plainly (so typing still coalesces per word), several become one
  undo group. **Any compound programmatic edit must be wrapped in
  `gtk_text_buffer_begin_user_action` / `end_user_action`** - that is what
  makes Replace All or a list transform a single undo step.
  `npad_gtk_replace_selection` does this for the simple case.
- **Commands.** Every menu item and shortcut is an `NpadCommand` dispatched by
  `npad_gtk_handle_command`. Unported commands are listed in
  `command_available()`; delete the case when a feature lands, and the menu
  item and shortcut come alive.
- **Settings keys are shared with Win32.** Use the same keys and defaults; the
  font keys fall back to `Monospace` / `Sans` because Consolas and Segoe UI do
  not exist on Linux.
- **Check and radio menu items:** `gtk_check_menu_item_set_active` emits
  `activate`, so code that syncs a check state must go through `set_check()`,
  which sets `syncing_menu` and keeps the command from running again.
- **Idle windows get broadcasts.** A settings change in any window re-runs
  `npad_gtk_reload_and_apply_settings` in all of them, so everything it calls
  must skip work when nothing changed (the theme and CSS already compare
  before applying - reloading either restyles the whole document).
- **Locale.** `gtk_init` sets the process locale from the environment, so
  `%f` / `atof` in core become locale-dependent (a decimal comma in de_DE).
  Only `settings_*_double` does that today and nothing calls it; use
  `g_ascii_dtostr` / `g_ascii_strtod` in the GTK layer.
- **Names:** private helpers are `npad_gtk_*` (never bare `gtk_*`, which is
  GTK's own namespace). The application id is `io.github.platima.npad`.

### Cross-instance messaging (D-Bus)

Each instance claims the session-bus name `io.github.platima.npad.Instance.p<pid>`
and listens on interface `io.github.platima.npad.Instance` at
`/io/github/platima/npad`. Signals carry the sender's pid first; receivers
ignore their own.

| Signal | Args | Win32 equivalent | Receiver does |
|--------|------|------------------|---------------|
| `SettingsChanged` | `(u pid)` | `npadSettingsChanged` | `npad_gtk_reload_and_apply_settings` |
| `ViewStateChanged` | `(u pid, i zoom, b mono)` | `npadViewStateChanged` | mirror zoom / font type if `sync_view_state` |
| `CloseAll` | `(u pid)` | `npadCloseAll` | its own save-checked close |

Instance count = names with the prefix (minus our own); "is pid a live npad"
= `NameHasOwner`, falling back to `/proc/<pid>/comm` for an npad in another
login session. Without a session bus all of this is a silent no-op.

To watch it: `dbus-monitor --session "interface=io.github.platima.npad.Instance"`.

## Deliberate differences from Windows

Decisions taken in the foundation round. Each is easy to revisit.

- **Window position** is not restored on Wayland, which reserves placement
  for the compositor. The last position is remembered and written back
  unchanged, so an X11 session's saved position survives. Size and maximised
  state persist everywhere. On X11 a saved position whose top edge (about a
  title bar, at least 100 x 20 px) is on no monitor - an unplugged screen, a
  changed layout - is replaced by the window's size centred on the primary
  monitor, and that is what gets saved. Windows has no such check.
- **Colour schemes.** `light` (the default) and `dark` / Solarized paint the
  text area with the same colours as Windows, whatever the GTK theme; the
  window chrome follows via GTK's dark variant. `system` uses the GTK theme's
  own colours, and follows the desktop's light/dark preference live: the
  portal's `org.freedesktop.appearance color-scheme` (1 dark, 2 light), then
  its `SettingChanged` signal; with no portal, or "no preference" (0), GTK's
  `gtk-application-prefer-dark-theme` as it was at startup, which is not
  tracked live. So on Mint-Y-Dark, a default npad shows a white page inside
  dark chrome - Notepad-faithful, but worth a look.
- **Dark chrome on themes without a dark variant.** `gtk-application-prefer-dark-theme`
  is ignored by themes with no `gtk-3.0/gtk-dark.css` (Mint-Y, Arc, ...), where
  dark is a sibling theme instead. For a dark scheme, `apply_chrome_variant`
  sets this process's `gtk-theme-name` to the installed sibling (`Name-Dark`,
  `Name-dark`, `Mint-Y-Aqua` -> `Mint-Y-Dark-Aqua`) and restores the original
  when the scheme turns light, only if the theme is still the one it set.
  Nothing is written to the desktop's settings. With no sibling installed, a
  *light* theme (its window background measured on a window that is never shown)
  falls back to GTK's built-in Adwaita, whose dark variant the prefer-dark hint
  selects - that is what Cinnamon's TraditionalOk gets - while a theme that is
  already dark (BlackMATE) is left alone. Found on Linux Mint, where the portal
  reported dark and npad stayed light; the Debug page (Ctrl+Shift+.) lists what
  the colour code sees.
- **`system` on a dark desktop with a theme whose text area stays white**
  (BlackMATE) uses the Dark scheme's page colours (#1e1e1e on #dcdcdc);
  `theme_text_area_is_light()` measures the theme's own `textview text`
  background on a throwaway text view's style path. A theme with a dark text
  area, or a desktop that is light or has no preference, keeps the theme's
  colours.
- **Settings directory** is `$XDG_CONFIG_HOME/npad` when that is set and
  absolute, else `~/.config/npad` (Windows keeps `%APPDATA%\Platima\npad`).
  An existing `~/.config/npad/settings.json` is read when the new location has
  none, never moved or deleted. Recovery snapshots under the old directory are
  not carried over.
- **"Text Documents" filter** matches by type (`text/plain` and every subtype,
  plus `*.txt`) rather than by extension: most Linux text files have no
  `.txt`. Save As does not append `.txt`.
- **Fonts:** `Monospace` / `Sans` defaults, resolved by fontconfig.
- **Column numbers** count code points, so an emoji is one column; Windows
  counts UTF-16 units (two). Notepad itself does the latter.
- **Tab inserts spaces** (`tab_inserts_spaces`, `tab_width`; shared rule in
  `core/tab_stops`) counts code points on both platforms. GTK reads the
  line's text straight from the buffer; Win32 converts the line before the
  caret to UTF-8 first, so an emoji is one column there too even though the
  status bar's `Col` counts it as two. The key handler lives in
  `ui_gtk_lists.c` beside the Markdown Tab handling because it shares that
  key-press hook, but it is not gated on `list_tools_enabled`. Over a
  selection it deletes and inserts inside one undo group, so a Tab is one
  step (unlike a typed replacement, below).
- **Typing over a selection** undoes in two steps (the replacement plus its
  first character, then the rest of the word); RichEdit does one.
- **Undo history** keeps 10,000 steps (typing coalesces per word); Windows
  keeps 100,000 RichEdit actions.
- **Print dialog and Page Setup are GTK's.** Margins have no field in GTK's
  paper dialog, so npad's Page Setup is its own small dialog (margins) that
  opens GTK's for paper and orientation. Orientation saves as portrait (1) or
  landscape (2); the reverse variants fold into those. A paper size with no
  Windows `DMPAPER_*` equivalent is saved as custom (256 + width/length in
  tenths of a millimetre).
- **Print Preview is npad's own window** (not GTK's PDF-viewer hand-off).
- **Check for Updates notifies only.** Linux updates come from the package
  manager, so npad never downloads or installs: the Updates page has no
  "Download and install automatically" mode (a settings file carrying `auto`
  behaves as `prompt`) and no Install Now, and the update dialog says how to
  update instead - decided by `update_install_source` (core) from who owns the
  binary (`dpkg -S`, `rpm -qf`, `pacman -Qqo`, asked only for `/usr/bin/npad`)
  and whether the pkg.npad.dev apt / dnf source or the `[npad]` pacman repo is
  configured. The launch check runs from the first npad window only: each
  window is its own process, so "no other npad on the session bus" stands in
  for Windows' primary window.
- **New documents default to LF**, not CRLF: `default_line_ending` falls back
  to `NPAD_DEFAULT_LINE_ENDING` (`core/file_ops.h`), CRLF on Windows and LF
  elsewhere. The same constant covers files with no line breaks and the
  session / write fallbacks, so an empty file you type into saves as LF.
- **Preferences has OK / Cancel / Apply like the property sheet.** Controls
  stage their change (`stage_*` in `ui_gtk_prefs.c`); Apply writes only the
  staged keys, saves, broadcasts and re-applies, OK does that and closes,
  Cancel drops them. As on Windows the font buttons, Clear Recent Files,
  Export / Import, the xdg-mime button and Reset act at once. The dialog is
  modal, as the property sheet is.
- **Associations is one action**, "Make npad the default text editor"
  (`xdg-mime default io.github.platima.npad.desktop text/plain`), never run
  unless clicked. The page shows `xdg-mime query default text/plain`.
- **No application-icon choice** (`icon_style` has no Linux UI): the icon
  comes from the icon theme. The only automatic variation is the window icon
  following the dark scheme (`io.github.platima.npad-dark`, chosen in
  `apply_app_icon` from `g_dark_mode`); the launcher always uses the standard
  one and nothing is stored.
- **Cut-line paste-above** keeps the cut text and compares it with the
  clipboard's (Win32 compares clipboard sequence numbers, which GTK lacks), so
  the same text copied elsewhere in between would still paste above.
- **Markdown menu items grey only while the menu is open** (like the Edit menu:
  an insensitive item's accelerator is dead, and Ctrl+] / Ctrl+[ are real
  accelerators on those items).
- **A transform that changes nothing is not an edit** (no undo step, the document
  stays unmodified); Win32 replaces the text regardless.
- **Ctrl+drop inserts at the drop point**, not at the caret as Win32 does
  (there the caret is all there is; here the drop has coordinates). Whether Ctrl
  is visible during a drag depends on the display server and on what you drag
  from, so `ctrl_is_down()` asks both the keyboard and the pointer state and
  remembers the value from the last motion over the window.
- **External-change dialog** is a plain GtkDialog built like Win32's command
  links (an action, then a dim line saying what it does) with the same wording
  and the same "Keep editing" default. It checks on `notify::is-active` of the
  window.
- **Scroll bars** are `GTK_POLICY_ALWAYS` with overlay scrolling off, and a
  bar with nothing to scroll is made insensitive (the slider is then not drawn)
  - GTK otherwise shows a full-length slider that looks live. The horizontal
  bar follows the same rule when Word Wrap is off, as on Win32.
- **Find / Replace / Go To placement.** The dialogs are transient windows
  with no position of their own, so the window manager places them (usually
  centred over npad); Win32 uses a fixed offset and remembers where you moved
  it. Wayland would ignore either.
- **Whole word** requires a word boundary only beside a term edge that is
  itself a letter, digit or underscore, so `foo(` still matches `foo(x)`.
- **Find's "Cannot find" box** is a GTK message dialog parented to the Find
  dialog when it is open.

## Parity checklist

Sized for one Sonnet session each. File ownership is listed so sessions can
run in parallel in separate worktrees; every batch also edits
`command_available()` and possibly `create_menu()` in `ui_gtk.c`, which are
small, mechanical merge points. Each batch: implement, delete its entries from
`command_available()`, add unit tests for anything testable in core, verify in
Broadway, update this file, CHANGELOG.md and DOCUMENTATION.md, bump the
version.

### Batch A - Find, Replace, Go To (`ui_gtk_find.c`)
- [x] Modeless Find dialog: match case, whole word, up/down, wrap around,
      interpret escapes (`list_unescape`), Highlight All (a GtkTextTag)
- [x] Find Next / Previous (F3 / Shift+F3) without the dialog
- [x] Replace, Replace All (one undo step - wrap in a user action)
- [x] Search history, options persisted under the Win32 keys, re-read in
      `npad_gtk_find_reload_settings`
- [x] "Match n of m" / not-found messages like Win32
- [x] Go To Line (Ctrl+G, and clicking Ln/Col) - logical lines
- [x] Notepad behaviours from DOCUMENTATION.md "Behaviour notes"
- [x] Ctrl+F / Ctrl+H fill "Find what" from a single-line selection
      (`find_seed_selection`, default on, shared rule in `core/find_seed`),
      including when the dialog is already open

### Batch B - Preferences and Font (new `ui_gtk_prefs.c`) - done in v0.35.0
- [x] GtkNotebook dialog with the General, Appearance, Defaults, Markdown,
      Backup and Associations pages, and the hidden Debug page (Ctrl+Shift+.
      or Shift+click Preferences)
- [x] OK / Cancel / Apply; Apply writes only the changed keys, saves and calls
      `ui_notify_settings_changed()`
- [x] General page: a **Fill Find with the selected text** checkbox bound to
      `FIND_SEED_KEY` (`core/find_seed.h`, default on). Reset removes that key
      explicitly - it starts with `find_`, which the keep-list preserves
      (see `reset_all_preferences` in `ui_win32.c`)
- [x] Export / import / reset settings
- [x] Format > Font: GtkFontChooserDialog writing the font keys
- [x] Linux replacements for the Windows-only pages: Associations is a "make
      npad the default text editor" action (`xdg-mime`), Updates is dropped
- [ ] On a real desktop: `xdg-mime` against GNOME / Cinnamon's own default-app
      database, the portal file chooser behind Export / Import, and
      Shift+click on the Preferences menu item

### Batch C - Printing (new `print_gtk.c`) - done in v0.34.0
- [x] Page Setup, Print, Print Preview via GtkPrintOperation. The preview is
      npad's own window fed by the operation's `preview` signal, because
      GTK's built-in one launches an external PDF viewer
- [x] Notepad header/footer codes from the Defaults settings
- [x] Measure pagination once, in points, on a Pango context with hinting and
      glyph rounding off (LESSONS.md "Measure the layout on the printer" and
      the GTK printing entries), and replay the stored runs to both preview
      and printer
- [ ] On a real desktop: a CUPS printer, the print dialog's Page Setup tab,
      HiDPI preview (see TODO.md)

### Batch D1 - Editing behaviours (`ui_gtk_edit.c`) - done in v0.35.0
- [x] Context menu with npad's items (Undo, Redo, Cut, Copy, Paste, Delete,
      Select All) replacing GTK's stock menu (`populate-popup`). The greying
      is `npad_gtk_edit_command_enabled()`, shared with the Edit menu.
      `extend_context_menu()` is the hook where batch D2 appends the Markdown
      list items
- [x] Drag a file onto the window to open it; Ctrl+drop inserts its contents
      at the drop point as one undo step. `text/uri-list` is added to the text
      view's own drop targets, so dropped text still lands as text
- [x] External-change detection on window activation
      (`editor_check_external_change`; Win32 `prompt_external_change` has the
      wording and choices), gated by `watch_file_changes`
- [x] Ctrl+mouse wheel zoom (10% steps, smooth-scroll deltas accumulated)
- [x] Disabled-not-hidden scroll bars, like Notepad
- [ ] On a real desktop: the popup menu, a drag from the file manager with and
      without Ctrl (X11 and Wayland), wheel and touchpad zoom (see TODO.md)

### Batch D2 - Markdown list tools (`ui_gtk.c` + new `ui_gtk_lists.c`) - done in v0.35.0
- [x] Markdown menu (sort, unique, case, convert delimiters, indent formats,
      custom indent) and its context-menu mirror. The menu is inserted after
      Format and removed live when `list_tools_enabled` changes
      (`npad_gtk_lists_reload_settings`, called from
      `npad_gtk_reload_and_apply_settings`); it is rebuilt only when what it
      shows changes, so a broadcast cannot destroy a menu that is open
- [x] Tab / Shift+Tab indent (a `key-press-event` handler on the text view,
      Ctrl+] / Ctrl+[ as real accelerators), Enter continues a list (the marker
      rule is `list_marker_length` in `core/list_ops.c`, unit-tested),
      Ctrl+X cuts the line (`ui_platform_cut` asks `npad_gtk_lists_cut_line`)
- [x] Paste as Markdown and Ctrl+Shift+V (read `text/html` from the clipboard,
      UTF-8 or UTF-16; convert with `core/html_md`); `ui_platform_paste` asks
      `npad_gtk_lists_paste` first and falls back to a plain paste
- [x] Every transform is one undo step (a user action around the edits)
- [ ] On a real desktop: the menu and popup opening, a browser's `text/html`
      clipboard, the bracket shortcuts on a layout that needs AltGr for them

### Batch E - Packaging (new `packaging/linux/`, Makefile, CI)
- [x] `io.github.platima.npad.desktop` (MimeType: text/plain and friends)
      and hicolor icons generated from `assets/`
- [x] `make install` installs the desktop file and icons
- [x] Icons regenerated with uniform scaling and a margin
      (`scripts/make-linux-icons.py`), plus a `-dark` icon for the window
      under dark schemes; `LINUX_ICON_SIZES` x `LINUX_ICON_NAMES` in the
      Makefile and `nfpm.yaml` list the same files
- [x] `.deb` built in CI and attached to releases
- [x] AppStream metainfo (needed for software centres and Flathub)
- [x] `.rpm` for Fedora (`make rpm`); `.deb` and `.rpm` both built by nfpm
      from `packaging/linux/nfpm.yaml` in a Debian 12 container, install-tested
      on Debian 12/13, Ubuntu 22.04/24.04 and Fedora in CI
- [x] Signed apt and dnf repositories at <https://pkg.npad.dev>, built by the
      separate `platima/npad-packages` repo from the release assets (the GPG
      key lives only there). Install instructions: DOCUMENTATION.md >
      Installing on Linux. Release notifies it via `NPAD_PACKAGES_TOKEN`.
- [x] Three architectures (v0.35.0): amd64, arm64 and riscv64 (rpm: x86_64,
      aarch64, riscv64). amd64 and arm64 compile in Debian 12 (arm64 natively
      on GitHub's `ubuntu-24.04-arm` runners), riscv64 under QEMU in Debian 13
      because Debian 12 has no official riscv64; so riscv64's floor is Debian
      13 / Ubuntu 24.04 (glibc <= 2.39, checked) where the others' is Debian
      12 / Ubuntu 22.04 (<= 2.35). The binary is compiled in the container
      (`scripts/ci-linux-build.sh`) and packaged on the host, since nfpm has
      no riscv64 build; `scripts/build-package.sh` reads the architecture
      from the ELF header with readelf, which reads foreign ELF files where
      an amd64 objdump may not. Install-tested per architecture: arm64 on
      the same five distros as amd64, riscv64 under QEMU on Debian 13, Ubuntu
      24.04 and openSUSE Tumbleweed (Fedora has no riscv64 image). The
      pkg.npad.dev apt repository publishes `binary-amd64`, `binary-arm64`
      and `binary-riscv64`, and the dnf repository all three rpm
      architectures.
- [x] Portable tarball (v0.36.0): `npad-v<version>-portable-linux-<arch>.tar.gz` for
      amd64, arm64 and riscv64, the counterpart of the Windows portable exe.
      The same binary as the .deb (CI compares them byte for byte) with
      `share/` (launcher, icons, metainfo) and a README.txt
      (`packaging/linux/README-portable.txt`: GTK 3 and glibc floor, run in
      place or copy under `~/.local`, pkg.npad.dev for updates); built by
      `make tarball` / `scripts/build-package.sh tar` on the host beside the
      packages. Release validation checks each tarball's ELF architecture;
      install-test unpacks it with only GTK 3 installed and runs
      `./npad --version` (parsed before GTK starts, so no display) on Ubuntu
      22.04 and Arch (amd64), Debian 12 (arm64) and Debian 13 (riscv64).
      Deliberately not static (GTK cannot usefully be) and not an AppImage
      (bundling GTK is against npad's lightness)
- [x] Arch Linux (v0.36.0): `npad-<version>-1-<arch>.pkg.tar.zst` for x86_64,
      aarch64 (Arch Linux ARM) and riscv64 (the Arch RISC-V port), built by
      nfpm's archlinux packager from the same `nfpm.yaml` (`make pacman`,
      `scripts/build-package.sh arch`); depends `gtk3` plus the libraries the
      binary links directly, and carries `/usr/share/licenses/npad/LICENSE`.
      CI checks it holds the .deb's binary byte for byte and says the right
      `arch` in `.PKGINFO`; release validation reads each package's `.PKGINFO`
      and ELF architecture. Install-tested with `pacman -U` after a full
      `pacman -Syu`: x86_64 in the official `archlinux` image, aarch64 in an
      image imported from Arch Linux ARM's generic rootfs (signature checked
      against the pinned Build System key) and riscv64 in one imported from
      the Arch RISC-V mirror's rootfs (SHA-256 pinned), both by
      `scripts/ci-arch-rootfs.sh`. Arch has no official aarch64 or riscv64
      container images, and the community ones on Docker Hub are rebuilds of
      those same tarballs. The pacman repository at pkg.npad.dev
      (`/arch/<arch>/npad.db`, signed) is built by `platima/npad-packages`.
      AUR: `packaging/arch/aur/npad-bin` (the portable tarball, per arch),
      updated by `scripts/aur-update.sh` and, when the `AUR_SSH_KEY` secret
      exists, pushed by release.yml's `publish-aur` job; setting the AUR
      account up is in `packaging/arch/README.md`
- [ ] Not verified on real Fedora / Mint / Arch hardware: installs are only tested in
      CI containers (no display), so launching the app from the .rpm is unseen.
      The user ran the GUI on real arm64 (v0.35.0) and installed v0.35.0 from
      the apt repository on a real riscv64 board (SpacemiT K3), where
      `npad --version` runs; the riscv64 GUI is not yet seen. The Arch aarch64 and riscv64 installs have
      only run in CI containers; the x86_64 package, the PKGBUILD (makepkg)
      and `pacman -U` were also run in a local Arch Linux (WSL)

### Batch F - Polish - done in v0.35.0
- [x] `system` scheme follows the desktop's light/dark preference live
      (`org.freedesktop.appearance color-scheme` through the Settings portal,
      `ui_gtk_instance.c`; GTK's own setting when there is no portal)
- [x] Off-screen saved position on X11 falls back to the default
      (`position_is_reachable` in `ui_gtk.c`)
- [x] Settings path honours `XDG_CONFIG_HOME` (core `settings_init`, with a
      read-only fall-back to `~/.config/npad`; unit-tested)
- [x] App icon in the About dialog and window: `npad_gtk_app_icon_name()`,
      also the default icon of every dialog
- [ ] On a real desktop: the portal on Cinnamon and GNOME (does Mint report a
      colour scheme at all?), the position repair with a monitor unplugged,
      and the icon from an installed package

## Testing the Markdown tools

LESSONS.md ("Test the real code by compiling a harness into the real process")
describes the technique, which `ui_gtk_lists.c` was verified with: the harness
`#include`s `ui_gtk_lists.c` so its statics are reachable, runs `main.c` as
`npad_main`, presents the window and drives it with `GdkEventKey`s sent through
`gtk_propagate_event` (Broadway drops them in `gtk_main_do_event`, and cannot
resolve Ctrl+Shift+letter from its keymap - fire that accelerator with
`gtk_accel_groups_activate`). Dialogs are driven by a `g_timeout` that finds the
toplevel by title, fills its entries and calls `gtk_dialog_response`. Broadway
does not share the clipboard between clients, so a second process cannot be the
HTML source there; the clipboard owner is the harness itself, and the payload
decoding is exercised directly.

## Known issues

- Only the foundation has been run on a real desktop (Mint and Debian 13);
  batches A to F were verified in Broadway, and installs of every
  architecture in CI containers. Window placement, the portal file chooser,
  HiDPI and the rest of TODO.md's "Needs testing" lists still need a pass
  on X11 and Wayland, and arm64 / riscv64 a launch on real hardware.
