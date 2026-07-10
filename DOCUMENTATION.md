# npad Documentation

Reference for npad's settings, keyboard shortcuts, status bar, command line
and behaviour. For per-release notes see [CHANGELOG.md](CHANGELOG.md); for the
raw commit history see [CHANGES.md](CHANGES.md).

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
| `auto_save_enabled` | bool | `true` | Silently save named documents on a timer. Untitled documents are never auto-saved. |
| `auto_save_interval` | int | `300` | Auto-save period in seconds (minimum 10). |
| `large_file_warning_mb` | int | `100` | Confirm before opening files larger than this (MB). `0` disables the prompt. |
| `recent_files_max` | int | `10` | Recent Files menu length (0-10). |
| `session_resume_enabled` | bool | `false` | Crash recovery: snapshot unsaved work on a timer; offer to restore after an unclean exit. All windows are restored, extras in their own cascaded windows. |
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
| `sync_view_state` | bool | `false` | Mirror per-window view changes (font type, zoom) live to every open npad window. |

### Defaults (Preferences > Defaults) - initial state for new windows/files

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `default_encoding` | int | `0` (UTF-8) | Encoding for new files: 0 UTF-8, 1 UTF-8 BOM, 2 UTF-16 LE, 3 UTF-16 BE, 4 ANSI. |
| `default_line_ending` | int | `0` (CRLF) | Line endings for new files: 0 CRLF, 1 LF, 2 CR. |
| `default_font_mono` | bool | `true` | New windows start in monospace (`true`) or proportional (`false`) mode. |
| `default_zoom` | int | `100` | New windows' zoom percent (10-500). |
| `auto_update_defaults` | bool | `false` | View changes (font type, zoom) immediately become the new defaults. |

The **Use Current** button copies the active window's font type and zoom into
the fields.

### Other persisted state (no dedicated UI)

| Key | Description |
|-----|-------------|
| `word_wrap` | Word wrap on/off (Format > Word Wrap, Alt+Z). |
| `window_x/y/width/height/maximized` | Window geometry, saved on exit. First run uses ~48% x ~72% of the monitor work area, centred. |
| `recent_file_0..9` | Recent files list. |
| `find_match_case`, `find_whole_word`, `find_search_down`, `find_wrap_around` | Find/Replace options. |
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
| Ctrl+Z / Ctrl+Y | Undo / Redo (100,000-step history) |
| Ctrl+X / Ctrl+C / Ctrl+V / Del | Cut / Copy / Paste / Delete |
| Ctrl+A | Select All |
| Ctrl+F / F3 / Shift+F3 / Ctrl+H | Find / Find Next / Find Previous / Replace |
| Ctrl+G | Go To Line |
| F5 | Insert time/date |
| Alt+Z | Toggle word wrap |
| Ctrl+M | Toggle monospace (this window) |
| Ctrl+E | Cycle line endings (CRLF > LF > CR) |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0 | Zoom in / out / reset (this window; Ctrl+Scroll also zooms) |
| Ctrl+, | Preferences |

## Status bar

Segments left to right; several are clickable:

| Segment | Shows | Click action |
|---------|-------|--------------|
| Message | Transient messages (e.g. "Match 3 of 7", "Replaced N occurrences") | - |
| Ln, Col | Caret position | Open Go To Line |
| Zoom % | This window's zoom | Reset to 100% |
| Mono/Prop | This window's font type | Toggle monospace |
| Line ending | Windows (CRLF) / Unix (LF) / Mac (CR) | Line-ending picker (converts on save) |
| Encoding | UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / ANSI | Encoding picker (applies on save) |

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
- **Encodings & line endings**: detected on open (BOM + heuristics) and
  preserved on save; both changeable per-document via the status bar or
  Format menu.
- **Crash recovery**: with session resume enabled, each window snapshots
  unsaved work to its own recovery slot. After an unclean exit, npad offers
  to restore; the first document opens in the current window and the rest in
  new, cascaded windows. Slots are cleared on clean save/exit. Snapshots
  belonging to a still-running npad instance are ignored, so opening a new
  window while others are open never offers to "restore" their live work.
- **Drag & drop**: drop a file to open it; hold Ctrl to insert its contents
  at the caret instead.
- **Large files**: opening a file over the configured threshold asks for
  confirmation first.

## Versioning & changelogs

npad follows [semver](https://semver.org). While on 0.x: **minor** bumps for
feature rounds or breaking changes, **patch** bumps for fix-only rounds.
Every change updates:

1. `src/main.h` + `src/platform/npad.rc` - the version numbers
2. `CHANGELOG.md` - curated per-version notes
3. `CHANGES.md` - one entry per commit
