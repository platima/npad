# npad on Linux - the GTK 3 port

Status, conventions and the remaining work for the Linux desktop build. The
Windows build is the reference: anything not called out below as a deliberate
difference should behave exactly as it does there (see
[DOCUMENTATION.md](DOCUMENTATION.md)). Read [LESSONS.md](LESSONS.md) first, in
particular its **GTK / Linux** section.

## Status (v0.34.0 - foundation round)

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

Greyed out until ported (see the checklist): Find / Replace / Go To,
Preferences, Format > Font, Page Setup / Print / Print Preview, and the
Markdown list tools. There is no Check for Updates on Linux by design.

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

Broadway tips (all learned the hard way - see LESSONS.md): the page only
paints while the browser pane is visible; keystrokes reach whichever npad
window Broadway last focused, so click into a new window first; and
screenshots are scaled (the pane's 1280x720 viewport comes back as 800x450).
When the pane cannot paint, verify through files instead - Ctrl+S and read the
bytes, or watch `~/.config/npad/recovery/` (the core snapshots on the first
edit and then every 30 s). Use an isolated `HOME` so test settings stay
throwaway.

Broadway is neither X11 nor Wayland, so window placement and the portal file
chooser still need a check on a real desktop.

## Architecture

```
src/platform/ui_gtk.h           private header: Window struct, command enum, cross-file API
src/platform/ui_gtk.c           main window, menus, shortcuts, commands, text, undo, view state,
                                status bar, timers, and the ui_platform_* entry points
src/platform/ui_gtk_dialogs.c   file choosers, message boxes, save / binary prompts, About
src/platform/ui_gtk_find.c      Find / Replace / Go To  (stubs - batch A)
src/platform/ui_gtk_instance.c  cross-instance messaging over D-Bus
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
  state persist everywhere.
- **Colour schemes.** `light` (the default) and `dark` / Solarized paint the
  text area with the same colours as Windows, whatever the GTK theme; the
  window chrome follows via GTK's dark variant. `system` uses the GTK theme's
  own colours. So on Mint-Y-Dark, a default npad shows a white page inside
  dark chrome - Notepad-faithful, but worth a look.
- **"Text Documents" filter** matches by type (`text/plain` and every subtype,
  plus `*.txt`) rather than by extension: most Linux text files have no
  `.txt`. Save As does not append `.txt`.
- **Fonts:** `Monospace` / `Sans` defaults, resolved by fontconfig.
- **Column numbers** count code points, so an emoji is one column; Windows
  counts UTF-16 units (two). Notepad itself does the latter.
- **Typing over a selection** undoes in two steps (the replacement plus its
  first character, then the rest of the word); RichEdit does one.
- **Undo history** keeps 10,000 steps (typing coalesces per word); Windows
  keeps 100,000 RichEdit actions.
- **No Check for Updates** - Linux updates come from the package manager.
- **The context menu** is still GTK's stock one (batch D1).

## Parity checklist

Sized for one Sonnet session each. File ownership is listed so sessions can
run in parallel in separate worktrees; every batch also edits
`command_available()` and possibly `create_menu()` in `ui_gtk.c`, which are
small, mechanical merge points. Each batch: implement, delete its entries from
`command_available()`, add unit tests for anything testable in core, verify in
Broadway, update this file, CHANGELOG.md and DOCUMENTATION.md, bump the
version.

### Batch A - Find, Replace, Go To (`ui_gtk_find.c`)
- [ ] Modeless Find dialog: match case, whole word, up/down, wrap around,
      interpret escapes (`list_unescape`), Highlight All (a GtkTextTag)
- [ ] Find Next / Previous (F3 / Shift+F3) without the dialog
- [ ] Replace, Replace All (one undo step - wrap in a user action)
- [ ] Search history, options persisted under the Win32 keys, re-read in
      `npad_gtk_find_reload_settings`
- [ ] "Match n of m" / not-found messages like Win32
- [ ] Go To Line (Ctrl+G, and clicking Ln/Col) - logical lines
- [ ] Notepad behaviours from DOCUMENTATION.md "Behaviour notes" (selection
      seeds the search term, etc.)

### Batch B - Preferences and Font (new `ui_gtk_prefs.c`)
- [ ] GtkNotebook dialog with the General, Appearance, Defaults, Backup and
      Markdown pages, and the hidden Debug page (Ctrl+Shift+.)
- [ ] Every change saves immediately and calls `ui_notify_settings_changed()`
- [ ] Export / import / reset settings
- [ ] Format > Font: GtkFontChooserDialog writing the font keys
- [ ] Linux replacements for the Windows-only pages: Associations becomes a
      "make npad the default text editor" action (`xdg-mime`), Updates is
      dropped

### Batch C - Printing (new `print_gtk.c`)
- [ ] Page Setup, Print, Print Preview via GtkPrintOperation (its preview is
      built in)
- [ ] Notepad header/footer codes from the Defaults settings
- [ ] Measure pagination with Pango on the print context, never on screen
      (LESSONS.md "Measure the layout on the printer")

### Batch D1 - Editing behaviours (`ui_gtk.c`)
- [ ] Context menu with npad's items (Undo, Redo, Cut, Copy, Paste, Delete,
      Select All) replacing GTK's stock menu (`populate-popup`)
- [ ] Drag a file onto the window to open it; Ctrl+drop inserts its contents
- [ ] External-change detection on focus-in (`editor_check_external_change`;
      Win32 `prompt_external_change` has the wording and choices)
- [ ] Ctrl+mouse wheel zoom
- [ ] Disabled-not-hidden vertical scroll bar, like Notepad

### Batch D2 - Markdown list tools (`ui_gtk.c` + new `ui_gtk_lists.c`)
- [ ] Markdown menu (sort, unique, case, convert delimiters, indent formats,
      custom indent) and its context-menu mirror
- [ ] Tab / Shift+Tab indent, Enter continues a list, Ctrl+X cuts the line
- [ ] Paste as Markdown and Ctrl+Shift+V (read `text/html` from the clipboard;
      convert with `core/html_md`)
- [ ] Every transform is one undo step

### Batch E - Packaging (new `packaging/linux/`, Makefile, CI)
- [x] `io.github.platima.npad.desktop` (MimeType: text/plain and friends)
      and hicolor icons generated from `assets/`
- [x] `make install` installs the desktop file and icons
- [x] `.deb` built with `dpkg-deb` in CI and attached to releases
- [x] AppStream metainfo (needed for software centres and Flathub)

### Batch F - Polish
- [ ] `system` scheme follows the desktop's light/dark preference live
      (`org.freedesktop.appearance color-scheme` through the Settings portal)
- [ ] Off-screen saved position on X11 falls back to the default
- [ ] Settings path honours `XDG_CONFIG_HOME` (core `settings_get_config_dir`)
- [ ] App icon in the About dialog and window once Batch E installs one

## Known issues

- Not yet run on a real X11 or Wayland session (only Broadway): window
  placement, the portal file chooser and HiDPI need a check on Mint.
