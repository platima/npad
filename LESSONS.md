# npad — lessons learned

Hard-won, non-obvious findings. Every entry here cost real time or shipped a
real regression, and every one is something a reasonable person would otherwise
attempt again from first principles.

**Read this before starting work on the area it covers.** The fix is usually in
the code with a comment; this file is the index, so the knowledge is findable
*before* you write the wrong thing rather than after.

Format: **what looks true** → **what is actually true** → **what to do**.

---

## Win32: status bar

**`SB_SETBKCOLOR` looks like it sets the status bar's background.** It does not,
when visual styles are active (which they are — npad has a comctl32 v6
manifest). comctl32 paints the bar from the theme and ignores the colour
entirely. The message succeeds; nothing changes.

Dropping the theme (`SetWindowTheme(h, L"", L"")`) *does* make the colour take
effect — and simultaneously drops the control to **classic Windows 95
rendering**: a sunken bevel around every segment, and the loss of the theme's
own text inset, so text sits hard against each segment's left edge.

→ Recolouring the status bar means owner-drawing it completely: subclass it,
erase the background yourself, and draw every segment borderless. Shipped and
reverted in v0.28.5/v0.28.6 — the colour was right, the chrome was not.

**`SBT_OWNERDRAW` rides on the `SB_SETTEXT` wParam, not on a style.** So
switching a segment between owner-drawn and system-drawn requires *re-sending
its text*. If you dedupe identical text (npad does, to stop repaint drag), an
unchanged segment silently keeps its old painting. An empty segment is the
trap: it compares equal to a cleared cache entry and is skipped.

**`SB_SETPARTS` needs monotonically ascending, non-negative edges.** They are
computed by subtraction from the client width, so a narrow window or high DPI
drives them negative and out of order, and the bar draws inverted. Clamp.

## Win32: icons

**`LoadIconW` always returns the `SM_CXICON` image**, whatever the .ico
contains. A hand-tuned 16×16 inside the file is never used; the caption gets a
downscaled 32px. → Use `LoadImageW` with explicit per-slot metrics.

**The two icon slots feed different surfaces**, which is what lets them differ:

| surface | slot | documented? |
|---|---|---|
| window caption | `ICON_SMALL` | yes |
| taskbar button | `ICON_BIG` | **no** — established by probing |
| Alt+Tab | `ICON_BIG` | yes |

**The shell caches the taskbar icon, and `WM_SETICON` on `ICON_BIG` alone does
not invalidate it.** Only re-sending `ICON_SMALL` forces a re-read — even with
byte-identical artwork. → Always send both, `ICON_BIG` first, `ICON_SMALL`
last. Undocumented; do not "optimise" either send away.

**Two cases where the split silently collapses**, both inherent to Windows: a
**grouped** taskbar button (several windows combined — the Windows 11 default)
is drawn from the *executable's* icon, not the window's; and with **small
taskbar buttons** the taskbar reads the caption's slot instead.

**Icon source art is often already posterised.** npad's is 253 unique colours,
so smooth downscaling *invents* thousands of intermediate colours and destroys
PNG compression — a LANCZOS 256px entry cost 69 KB against the 53 KB of the
icon it replaced. → HAMMING resampling plus quantising back to 256 colours:
7 KB, with error confined to antialiased edges. Verify against an amplified
difference render rather than assuming.

## Win32: dark mode

**`SetPreferredAppMode(AllowDark)` does not mean "let me choose".** It means
"follow the Windows `AppsUseLightTheme` setting". For an app with its *own*
colour scheme that is wrong in both directions: npad-dark on a light Windows
got no dark scroll bars at all (silently — `SetWindowTheme` returns S_OK and is
ignored), and npad-light on a dark Windows picked up dark menus. → Use
`ForceDark` / `ForceLight` derived from the app's own scheme, re-applied when it
changes, with `FlushMenuThemes` (ordinal 136) to drop the cached menu theme.

**There are two separate Windows theme settings.** `AppsUseLightTheme` is app
windows; `SystemUsesLightTheme` is the taskbar and system chrome. Pick by which
surface the thing you are colouring actually sits on.

**The menu bar cannot be recoloured by supported means.** Windows draws it as
non-client area. The only route is owner-drawing via undocumented
`WM_UAHDRAWMENU` / `WM_UAHDRAWMENUITEM`. Deliberately not done.

## Win32: RichEdit

**Idle windows are not idle.** Two broadcasts reach a window nobody is
touching: `npadSettingsChanged` from any other instance (the automatic
update check writes its result and broadcasts on every launch) and Windows'
`WM_SETTINGCHANGE "ImmersiveColorSet"` (theme *or accent* change - a
wallpaper slideshow feeding the accent fires it per slide). Anything hung off
those must be idempotent, or it runs on the document every time. A
document-wide `EM_SETCHARFORMAT SCF_ALL` is not free: it reflows and RichEdit
may move the view. Compare before applying, preserve the first visible line
(`EM_GETFIRSTVISIBLELINE` + `EM_LINESCROLL`, not pixel `EM_SETSCROLLPOS`,
which is unreliable across a reflow), and after `WM_SETREDRAW` use
`RedrawWindow`, not `InvalidateRect` - the control can otherwise sit blank
until something forces a full repaint.


**RichEdit in plain-text mode stores line breaks as bare `\r`.** `GT_USECRLF`
converts only on *retrieval* via `EM_GETTEXTEX`; `EM_GETTEXTRANGE` gives you the
internal form. Anything matching on line breaks must expect `\r`.

**`EM_GETLINECOUNT`, `EM_EXLINEFROMCHAR` and `EM_LINEINDEX` count DISPLAY
lines.** With word wrap on, a 96-line document reports 106, `Ln` counts wrapped
rows, and Go To navigates to the wrong place. → With wrap off the cheap API is
correct; with wrap on, count the `\r` breaks. (`get_paragraph_bounds` already
knew this and said so — the core navigation didn't.)

**RichEdit's OLE drag-and-drop needs `OleInitialize`, not `CoInitializeEx`.**
npad calls only the latter, so `RegisterDragDrop`/`DoDragDrop` fail with
`CO_E_NOTINITIALIZED` and drag-drop is inert — which also makes
`ES_NOOLEDRAGDROP` a no-op. Do not add it as a speculative fix.

## Win32: dialogs and messages

**`IsDialogMessageW` must be gated on the message belonging to that dialog.**
Called for every queued message while a modeless dialog exists, it can divert
Tab / Enter / Escape / arrows typed in the *editor*. MSDN warns against this
explicitly.

**Preferences pages need `DS_FIXEDSYS` alongside `DS_SETFONT`.** Without it
`"MS Shell Dlg"` is taken literally and substitutes to Microsoft Sans Serif —
inside a Segoe UI comctl32 frame. Note the two faces do not share a
DLU-to-pixel ratio, so adding it can reflow tight labels.

**A newline in a TaskDialog button label requires `TDF_USE_COMMAND_LINKS`.**
The two-line "action, then explanation" style only renders as intended with
that flag; without it Windows draws a normal push button around the whole
string, stretching the dialog and pushing text outside the button.

**Widening a `bool` return to an enum silently inverts every `!` call site.**
`print_document` grew a three-way result so Cancel could be told from failure;
`PRINT_RESULT_PRINTED` is 0, so the surviving `if (!print_document(...))` then
warned "could not be printed" on every *successful* print and stayed silent on
real failures. The compiler has nothing to say about it. Grep every call site
when a return type changes, even when the build is clean.

**Mnemonics collide silently.** `&Paste` and `&Preferences` in one popup meant
`Alt+E, P` pasted. Check the whole popup's set, including items appended to the
*context* menu from elsewhere.

## Win32: printing and preview

**Windows 11's modern print dialog ignores the DEVMODE you pass to
`PrintDlgW`.** Orientation, paper, everything - regardless of its own "let
the app change my printing preferences" toggle. A known Windows 11 22H2+
behaviour that bites every classic Win32 app (WPF, wxWidgets and others have
the same open issues). `PD_ENABLEPRINTHOOK` with a hook that returns 0 makes
comdlg32 use the classic dialog, which honours the DEVMODE - and has no
preview pane, so the "This app doesn't support print preview" message
disappears as a side effect. Seeding the DEVMODE alone (v0.32.6) fixed
nothing visible; it took the hook.

**A common-dialog hook must return TRUE for `WM_INITDIALOG`.** Returning 0
there means "the hook has set the focus itself", so the dialog skips focusing
its default control. A no-op hook that returns 0 for everything therefore
ships a dialog that opens unfocused - masked when the owner is active (the
activation focuses it anyway), obvious when the owner is disabled, as Print
Preview's is.

**Measure the layout on the printer, never on screen.** Glyph advances are
integer-rounded per ppem, so the same font realises differently at 600 dpi and
at ~96 dpi. A preview that re-measures on its own DC breaks lines a word early
or late and stops being a preview. Compute pagination once on a printer
information context in printer dots, store `(page, x, y, offset, len)` runs,
and have the preview and the printer both replay them.

**`CreateICW` needs no dialog and starts no job.** `PrintDlgW` with
`PD_RETURNDEFAULT` returns the default printer's `DEVNAMES` + `DEVMODE`
silently, and `CreateICW(driver, device, NULL, devmode)` measures against it.
That keeps printing on the already delay-loaded comdlg32 — winspool.drv
(`GetDefaultPrinterW`, `OpenPrinterW`) is not linked and does not need to be.

**Copy a DEVMODE as `dmSize + dmDriverExtra`, not `sizeof(DEVMODEW)`.** The
driver-private tail carries the real settings; a truncated copy looks valid,
applies cleanly, and quietly prints with default paper and orientation. And a
field is honoured only when its bit is set in `dmFields`.

**The printer DC's origin is the printable corner, not the paper corner.**
`PHYSICALWIDTH`/`PHYSICALHEIGHT` are the sheet; `PHYSICALOFFSETX/Y` is the
hardware margin. Margins measured from the printable corner silently add the
hardware margin to whatever the user set. To scale printer coordinates into a
preview rect, `MM_ANISOTROPIC` with window extent = sheet, viewport extent =
the on-screen rect, and window origin = `(-PHYSICALOFFSETX, -PHYSICALOFFSETY)`
makes logical units printer dots and logical (0,0) the printer's own origin.

**`CreateCompatibleBitmap` must take the WINDOW dc.** Passing the memory DC
returns a 1 bpp monochrome bitmap, because a fresh memory DC's default bitmap
is monochrome — the preview then renders with no antialiasing at all.

**Windows 11's modernised print dialog has its own preview pane**, and shows
*"This app doesn't support print preview"* for classic GDI printing clients
that have not handed it an XPS page source. Satisfying it means generating XPS;
drawing our own preview costs less, works on Windows 10 too, and is the only
version that can be measured against the real printer.

**Paper shape is inches, not dots.** `PHYSICALWIDTH`/`PHYSICALHEIGHT` are dot
counts on two independent rulers, and `LOGPIXELSX != LOGPIXELSY` on plenty of
devices (600x300 is ordinary on dot matrix and in draft modes). Comparing the
raw counts previews a portrait sheet as landscape and squashes every glyph with
it. Convert one axis before taking the ratio.

**`CreateWindowExW` sends `WM_SIZE` before it returns.** Storing the HWND in
the struct only after the call means the first layout pass runs against
`hwnd == NULL` - and `InvalidateRect(NULL, ...)` does not no-op, it invalidates
the entire desktop. Set it in `WM_CREATE` from the handle you are given.

**Re-enable a modal window's owner BEFORE `DestroyWindow`, not in
`WM_DESTROY`.** Destroying the only enabled window in the app lets Windows pick
the next candidate, which is whatever other application is behind it - npad
drops out of the foreground as the dialog closes.

**Disabling the document window does not reach Find/Replace.** It is a separate
top-level window owned by the document, so `EnableWindow(doc, FALSE)` leaves it
live - and Replace All would then edit the document behind a "modal" preview
that claims to be showing it.

**A new top-level window must not reuse `NPAD_WINDOW_CLASS`.** Three
`EnumWindows` callbacks identify document windows by that class name, so a
helper window registered under it gets counted in the New Window cascade and
receives `npadCloseAll` / `npadCloseForHandoff`. The flip side: a distinct
class means the `npadSettingsChanged` broadcast never arrives, so
`reload_and_apply_settings` has to push the theme in by hand.

## Win32: registry and file associations

**A property sheet sends `PSN_APPLY` to every page that has been CREATED, not
just the ones that changed.** Visiting a tab is enough. A page that applies
its whole state on Apply therefore rewrites everything it shows whenever the
user presses OK for an unrelated change on another tab. The Associations page
did exactly that: it repointed registrations at whichever copy of npad was
running, copied the all-users set into the user's hive, and deleted a
half-registered legacy group. Snapshot the state at load and apply the diff.

**A trust gate must be re-checked on the elevated side, and must not read the
environment.** The unelevated page decided whether all-users registration was
allowed; the elevated child simply obeyed. It also read `%ProgramFiles%`,
which any launching process can set for its children. Read
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\ProgramFilesDir` instead,
and repeat the check in the elevated process itself. The stakes: a
machine-wide open command pointing at a user-writable copy of npad is code
run by every account that opens the file.

**"Enable it when X, or when something is already registered" is a bypass.**
The all-users checkbox was enabled on a portable copy whenever HKLM already
had entries - meant to allow clearing them, it also allowed re-registering
them with the portable copy's own path.

**Send changes across an elevation boundary, never a whole state.** A state
means an empty or missing argument reads as "clear everything". `+text,-log`
means an empty list is simply nothing to do, and anything that is not an
exact sign-and-name is ignored.

**A modal wait that pumps messages must disable the top-level window, not the
page.** Disabling a property-sheet page leaves the sheet's OK and Cancel
live, and Cancel destroys the page while its own `PSN_APPLY` is still on the
stack.

**Registry code can be tested against a sandbox with `RegOverridePredefKey`.**
It redirects `HKEY_CURRENT_USER` or `HKEY_LOCAL_MACHINE` to any key, for the
calling process only. Open the sandbox keys on the real hive first, redirect,
run the real module, restore with `NULL`, delete the sandbox. Seed anything
the module reads from the redirected hive (here, `ProgramFilesDir`).

**Inno's `UsePreviousTasks` undoes runtime changes on every upgrade.** Setup
re-applies the task ticks recorded at install time. An app that changes the
same registrations at runtime must hand setup the real state:
`/MERGETASKS="assoc\text,!assoc\markdown,..."`. And the uninstall log only
knows what setup wrote - `ValueType: none; Flags: dontcreatekey
uninsdeletekey` creates nothing but deletes the key on uninstall, whoever
made it. WiX: `RemoveRegistryKey Action="removeOnUninstall"`.

**An MSI major upgrade UNINSTALLS the old product before installing the new
one.** With the default `<MajorUpgrade>` (RemoveExistingProducts after
InstallValidate), every `RemoveRegistryKey Action="removeOnUninstall"` row in
the old package fires on every upgrade. A rule meant to clean up at uninstall
therefore wiped the page's registrations each time the MSI was updated.
Inno does not have this problem: an upgrade installs over the top and runs
no uninstall.

**Never positively select an installer task to preserve runtime state.**
`/MERGETASKS="assoc\data"` does not mean "keep what is registered"; it means
"run that task", and the task does whatever the installer does - here, take
ownership of seven file types the page had only registered. Pass only
negations (`!assoc\data`), and leave registered groups to setup's own
record. And if setup will prompt for its install mode (a per-user and an
all-users install both present), no one state is the right one to pass.

**A deliberately-left reference must not read as a registration.** Uninstall
removes the ProgID but leaves `.ext\OpenWithProgids\npad.ext`, which Windows
ignores. Code that read that entry alone as "registered" showed a removed
group as ticked after a reinstall. Check that the thing referred to exists.

## Core: settings

**An escape without a matching unescape compounds.** `serialize_settings`
escaped `\` and `"` on write; the parser walked *over* escapes to find the
value's end but stored the raw bytes. So every save/load cycle doubled every
backslash in a value. A Windows path reached **1.2 GB** in about thirty cycles.

The damage was not proportional to the bug. Every npad window loads the
settings file at startup, so twenty windows held 21 GB between them, saturated
the disk, and stopped responding — and eventually npad could not start at all.
The runaway value also overflowed the serializer's write buffer, whose loop
silently stopped, so the file was rewritten containing *that key alone* and
every other preference was lost.

→ Three lessons, all general:
- **Round-trip anything you serialise, repeatedly, in a test.** One cycle looks
  perfect; only repetition exposes compounding. This path had no test at all,
  which is the whole reason it survived.
- **Reject implausible input on load, not just on write.** A fix that only
  stops *creating* corruption leaves existing users unable to start. Bounding
  the file size and the value length lets a broken install heal itself.
- **A serialiser that cannot fit everything must fail, not truncate.** Silently
  writing a well-formed file that is missing data is far worse than refusing:
  the caller keeps the previous file, which is the safe direction.

**A guard that discards data is a data-loss bug wearing a fix's clothes.**
v0.28.7 protected against an oversized settings file by ignoring it - and
because npad rewrites settings on exit, the original was then overwritten. The
release that fixed the corruption destroyed the settings of the people it was
meant to rescue. Salvage what parses, move the rest aside, and never leave a
path where the only copy is deleted by the next ordinary operation.

**Beware anything every instance loads at startup.** Cost is multiplied by
window count, and npad is one-process-per-window. A file that is merely
annoying at one instance is fatal at twenty.

**npad rewrites settings.json on every exit**, unconditionally — `main()` calls
`settings_save_window_state()` then `settings_save()` after the message loop,
whether or not anything changed. Normally harmless; with a corrupt 1.2 GB file
it meant every one-second launch wrote 1.2 GB, and it is why a fresh
settings.json reappeared moments after the corrupt one was renamed away.

**Stopping corruption is not the same as repairing it.** The unescape fix
freezes an already-doubled path rather than fixing it, because a corrupted `\\`
is indistinguishable from a deliberate one (UNC paths begin with two). Decide
explicitly which you are doing, and say so — here the entries age out of Recent
Files on their own, so freezing was the right trade.

**A key that starts with a kept prefix is kept - even when it is a preference.**
Reset All Preferences preserves everything under `find_` (search options and
history), so `find_seed_selection`, a real Preferences checkbox, would have
survived a reset and the checkbox would then lie about "defaults". Name a new
preference outside the kept prefixes where you can; where the name is fixed
(it is shared across platforms), remove it explicitly after the prefix reset
(`reset_all_preferences` does) and say so wherever the next platform's reset
gets written (LINUX_PORT.md batch B).

## Core: text model

**Detect a limitation where you can prove it, not where you guessed it.** The
binary-truncation guard keys off the decoder finding a NUL, not off the
"looks binary" heuristic that runs before loading - a file can contain a NUL
without looking binary. And in UTF-16 a zero *byte* is normal (every ASCII
character has one); only a zero *code unit* means truncation. Getting that
wrong would make npad refuse to save ordinary documents.

**npad's core passes NUL-terminated `char *`.** So any file containing a NUL
truncates at that byte: a 120 KB PNG loads as 9 bytes. The counts are then
honest — it is the *load* that is broken. Worse, `current_file` still points at
the original, so typing one character and saving overwrites it. Genuine binary
support needs length-carrying buffers throughout.

## GTK / Linux

**GtkTextBuffer (GTK 3) looks like RichEdit's peer, but it has no undo at
all.** GTK 4 added one; GTK 3 never will. → npad records edits from the
buffer's `insert-text` / `delete-range` signals into `core/undo.c` and replays
them. Connect the handlers normally (not `_after`): they then run before the
default handler, while the iterators still show where the edit starts.

**GTK wraps every keystroke in a "user action"**, not just compound edits. So
the obvious design - one undo step per user action - makes every character its
own step and kills word-level coalescing. → Hold the edits seen inside a user
action and flush at its end: one edit is recorded plainly (it can still merge
with the previous keystroke), several become one group. A paste or typing over
a selection is several; a keystroke is one.

**`gtk_check_menu_item_set_active` emits `activate`.** Syncing a check mark
from code therefore runs the command again - a Word Wrap check refreshed on
menu open toggles wrap back off. Radio items also emit `activate` for the item
being switched OFF. → Route state syncs through one helper that sets a guard
flag, and ignore `activate` on a radio item that is not active.

**`gtk_widget_remove_accelerator` warns when the accelerator is not
installed**, so "remove both possibilities, then add the right one" (the
Ctrl+N / Ctrl+Shift+N swap) prints GTK-CRITICAL noise. Track what is
installed.

**A CSS number built with printf breaks under a decimal-comma locale.**
`font-size: 10,5pt` is rejected silently and the old font stays, so it looks
like the setting did nothing - only for users in de_DE, fr_FR and friends.
→ `g_ascii_dtostr`, never `%f`, for anything parsed as CSS.

**Wayland does not let a client read or set its window position.**
`gtk_window_get_position` returns 0,0 and `gtk_window_move` is ignored.
Persisting what it reports would overwrite the position an X11 session saved
with 0,0. → Remember the last requested position and report that back; only
X11 (`GdkX11Display`) positions for real.

**`g_set_prgname` must run before `gtk_init`, and must be the app id.** It
becomes the Wayland `app_id` and the X11 `WM_CLASS`, which is how the desktop
matches windows to the `.desktop` launcher and its icon. Get it wrong and the
taskbar shows a generic icon however the launcher is set up.

**A .deb built on a new Ubuntu does not install on an old Debian by default.**
`dpkg-shlibdeps` would write `libgtk-3-0t64` (the 64-bit time_t rename in
Debian 13 / Ubuntu 24.04), which Debian 12 and Ubuntu 22.04 do not have, and
a `libc6` floor from the build host. -> `packaging/linux/nfpm.yaml` writes
`libgtk-3-0t64 | libgtk-3-0` by hand and `scripts/build-package.sh` takes the
libc6 floor from the binary's highest `GLIBC_x.y` symbol; releases build in a
Debian 12 container to keep that floor low, and `packages.yml` fails if it
rises above Ubuntu 22.04's 2.35. A .desktop file's name must equal the app id
(`g_set_prgname`) or the shell shows a generic icon.

**nfpm expands `${VAR}` in `contents` only when the entry says `expand: true`.**
Environment variables work in `version`, `release` and the `depends` lists
without any switch, which makes the `src:` failure ("Glob failed:
`${STAGE}/usr/bin/npad`: no matching files") look like a bug in your path.
Likewise `type: tree` makes the rpm *own* the directories it walks (it listed
`/usr/share/icons/hicolor`, which belongs to hicolor-icon-theme), so
`nfpm.yaml` lists files individually. An empty `release` gives a deb version
without a `-1` revision; the rpm needs one.

**Artifact names must be unique within a workflow run, and `release.yml` runs
`ci.yml` first.** Both build the Linux packages through `packages.yml`, so
that workflow takes an `artifact_name` input and the release passes its own;
otherwise the second upload fails. Attaching to the release happens in
`release.yml`, not in `packages.yml`, because a reusable workflow requesting
`contents: write` makes every caller that has only read access fail at
startup, including plain CI runs.

**A `GtkComboBoxText` with an entry is not the entry's parent.** The entry sits
inside a `GtkBox` inside the combo, so `gtk_widget_get_parent(entry)` and a
cast to `GTK_COMBO_BOX_TEXT` fail with a CRITICAL and the history list stays
empty - silently, apart from the log. → Keep the combo widget and fetch the
entry with `gtk_bin_get_child`.

**Applying a `GtkTextTag` invalidates `GtkTextIter`s**, just as an edit does.
Highlighting every match by walking forward with one iterator and tagging as
you go gives stale iterators. → Collect the matches as character offsets
first, then rebuild iterators and apply.

**`gtk_text_iter_forward_search` takes the string before the flags** (`iter,
str, flags, ...`) - the reverse of what the name and every wrapper suggest. A
wrong order is a compile error, not a runtime surprise.

**Under `-std=c99`, `strdup` is not declared**, and the implicit declaration
turns the result into an `int`. In the GTK files use `g_strdup`; GLib's
allocator is plain `malloc` (2.46+), so core's `free()` is still right.

**GtkPrintOperation's built-in preview is a PDF viewer, not a preview.**
`GTK_PRINT_OPERATION_ACTION_PREVIEW` exports a temporary PDF and opens it in
the desktop's default PDF handler. On a machine with none (WSL, a minimal
install) nothing appears and nothing reports an error; it also has no Print or
Page Setup. → Connect the operation's `preview` signal, return TRUE and draw
the pages yourself with `gtk_print_operation_preview_render_page` onto your own
cairo context. The page count only exists after the preview's `ready` signal,
so build the window there.

**A `preview` handler must give the print context a cairo context before it
returns, or GTK aborts.** `print_pages` checks right after the signal and calls
`g_error("You must set a cairo context on the print context")` - a fatal abort
with the backtrace inside libgtk, not in npad. Pagination (`begin-print`) runs
afterwards and needs it too. → `gtk_print_context_set_cairo_context` with a
throwaway surface straight away; set the real one in each draw.

**GTK's 72 dpi print context hints glyph advances to whole points, so text
measures differently at different resolutions.** A 14 pt monospace face is
8.43 pt per glyph but laid out as 8 pt on the export/PDF context: 96 columns
fit where 91 should, and a preview built on a 300 dpi context wraps five
columns earlier than the page it claims to preview. Pango 1.44+ also rounds
glyph positions on its own. → Measure and draw on a Pango context of your own
(`pango_font_map_create_context(pango_cairo_font_map_get_default())`, 72 dpi,
`CAIRO_HINT_METRICS_OFF`, `pango_context_set_round_glyph_positions(FALSE)`),
work in points, and `cairo_scale(72 / dpi)` when drawing onto the print
context. The same text then measures the same on every device.

**`gtk_print_operation_set_use_full_page(TRUE)` is how margins can be measured
from the sheet edge.** By default the context's origin and size are the page
setup's own margins, which GTK's paper dialog cannot edit (it has no margin
fields) and which start at the printer's hardware margins. Full page gives the
corner of the sheet, so Notepad-style margins are applied by npad alone.
Landscape needs no rotation here: the context is already orientation-aware.

**A text field that compares itself with `settings_get_string(key, NULL)`
writes a setting nobody changed.** A GTK entry shows the default ("&f",
"Page &p") for an unset key; on focus-out it found the key missing, called
that a change and wrote the default into `settings.json` - for every field the
user merely tabbed through, and it stuck as the user's own value, so a later
change of default would never reach them. -> Compare with the value the
getter returns for the key's own default, and write only when they differ.
The same rule is why Preferences has no Apply button: each control writes its
one key as it changes, never the whole page.

**`g_spawn_check_exit_status` is deprecated in GLib 2.70, and its replacement
needs 2.70.** Debian 12 and Ubuntu 22.04 ship 2.74 / 2.72, but the build
warns on both names depending on the minimum you target. -> `g_spawn_sync`'s
status is a plain wait status on Unix: `WIFEXITED(status) &&
WEXITSTATUS(status) == 0`.

**A modal `gtk_dialog_run` does not stop settings broadcasts.** The main loop
keeps running under it, so another instance saving settings re-runs
`npad_gtk_reload_and_apply_settings` while Preferences is open, and the
dialog's controls then show values older than `settings.json`. That is safe
only because every control writes just its own key; a dialog that saved all
its controls on close would overwrite the other instance's change.
**`GTK_POLICY_ALWAYS` alone does not give Notepad's scroll bar.** GTK 3.24
scroll bars are overlay indicators that fade out whatever the policy says, and
once overlay scrolling is off (`gtk_scrolled_window_set_overlay_scrolling(FALSE)`)
a bar with nothing to scroll still draws a full-length slider that looks live.
Notepad's is a greyed, empty gutter. -> Policy ALWAYS, overlay off, and make
each scroll bar insensitive while its adjustment's `upper - lower` does not
exceed `page_size` (connect the adjustments' `changed`).

**`GtkTextView` is already a drag destination - for text, not files.** Its
target list has no `text/uri-list`, so a file dropped on it is refused, and a
destination set on the parent window is never asked because the text view
covers it. -> Add `gtk_target_list_add_uri_targets` to the text view's own
list (`gtk_drag_dest_get_target_list`), handle `drag-data-received` for that
target id only and `g_signal_stop_emission_by_name` (the class handler would
otherwise try to read text and fail the drop); text drops fall through to GTK
and keep working. Finish the drag (`gtk_drag_finish`) *before* opening the
file: the open can raise a save prompt, and a modal dialog inside a drag
callback leaves the source's drag session hanging.

**Window activation is `notify::is-active`, not `focus-in-event`.** The latter
fires for the toplevel's widgets as focus moves inside it. And every dialog the
prompt opens deactivates and reactivates the window on its way out, so a
handler that prompts needs the same re-entrancy guard Win32's
`prompt_external_change` has, plus an idle hop so the dialog is created after
the activation finished.

**The wheel's smooth-scroll delta has the opposite sign to the zoom.**
`GDK_SCROLL_SMOOTH` reports scrolling up as a negative `delta_y`. An
accumulator fed with `-dy` is therefore reversed by `dy * accum > 0`, not
`< 0`; the wrong test reset it on every event, so a touchpad swipe (many
deltas of 0.3) never reached a whole step while a mouse notch (delta 1) still
worked - which is why it only showed in the harness, not by hand.

## Installers

**Inno Setup's check lists do not scale their check box offset.**
`TNewCheckListBox.Offset` defaults to 2 (Components) and 4 (Tasks) raw
pixels, and stays there at any DPI while the glyph scales; at 150% with the
default `WizardSizePercent` (`ScaleX(100)` = 167) the top-level boxes are drawn
half outside the list. Not the wizard style: identical in `modern`, `classic`
and `excludelightcontrols`, on 6.7.0 and 6.7.3. Set
`WizardForm.ComponentsList.Offset := ScaleX(8)` (and the Tasks list) in
`InitializeWizard`. Seen only when the maintainer's display went to 150% -
the script had not changed in twelve releases.

**Azure Trusted Signing is now "Artifact Signing", and so is everything
named after it.** The role is *Artifact Signing Certificate Profile Signer*,
the client package is `Microsoft.ArtifactSigning.Client` - but the DLL inside
it is still `Azure.CodeSigning.Dlib.dll`, the endpoints are still
`*.codesigning.azure.net`, and `metadata.json` still says
`CodeSigningAccountName`. Search for the old names and you find stale advice.

**GitHub's OIDC subject is name-based unless the repository opts in, and
Entra's GitHub template now demands the ID-based one.** Repositories created
before 2026-07-15 still emit `repo:owner/repo:...`; Entra's federated
credential form requires the owner and repository IDs and generates
`repo:owner@<owner_id>/repo@<repo_id>:...`. The two never match until the
repo is switched: `PUT /repos/{owner}/{repo}/actions/oidc/customization/sub`
with `use_immutable_subject: true` (the response's `sub_claim_prefix` shows
the new form). Two runs failed on exactly this before the toggle was found.

**Entra matches the subject case-sensitively, and GitHub lowercases the
owner.** The token said `platima@...`; the credential, typed from the org's
display name, said `Platima@...`. Entra's error names the problem ("matches
case-insensitively but not case-sensitively"); the fix is to lowercase the
organization in the credential. Read the presented subject straight out of
the AADSTS error and paste it - it is the one string that is known correct.

**Bind the federated credential to a GitHub *environment*, not a tag.** A Tag
credential must name one exact tag (no wildcards), so it would need adding
before every release. An Environment credential's subject is
`repo:<owner>/<repo>:environment:<name>` on any ref; the signing job then
declares `environment: release`.

**Inno's `SignTool` hands the command a QUOTED filename.** Wrapping the tool in
`cmd.exe /c "wrapper" $f` therefore yields four quote characters, and cmd's
quote-stripping rule mangles it (exit code 1, no explanation). Invoke a `.ps1`
through `pwsh.exe -File` instead - and `pwsh`, not `powershell.exe`: Windows
PowerShell 5.1 launched from a pwsh parent inherits a `PSModulePath` it cannot
load its own modules from (`Set-AuthenticodeSignature` "could not be loaded").
Also, with `SignTool` set Inno signs the finished setup exe itself as well as
the uninstaller - signing it again afterwards just spends a signing call.

**A signing pipeline can be dry-run without the signing service.** A throwaway
`New-SelfSignedCertificate -Type CodeSigningCert` plus a `.ps1` that calls
`Set-AuthenticodeSignature` exercises every hook (npad.exe, uninstaller, setup,
MSI, portable, checksum ordering); `Get-AuthenticodeSignature` then reports
`UnknownError` (untrusted root) with the throwaway subject, which is the pass
condition. Remove the certificate from `Cert:\CurrentUser\My` afterwards.

**The wizard can be inspected without touching the keyboard or mouse.** The
bootstrapper spawns `<name>.tmp` for the wizard, so find it by window class
(`TWizardForm`) rather than PID; `BM_CLICK` on the `&Next` / `I &accept`
buttons walks the pages, `PrintWindow` with `PW_RENDERFULLCONTENT` captures
them, and `WM_CLOSE` + `BM_CLICK` on the "Exit Setup?" Yes button leaves
without installing (exit code 2). `/LOG=` plus `Log()` in `[Code]` reads any
property back. `scripts/probe-installer.ps1` does all of it. Local variants
build with ISCC from a copy of the script with the font `Source:` lines
removed - the fonts are not in the tree.


**Inno's `UsePreviousTasks` defaults to `yes`, and it beats `Flags: unchecked`.**
Changing a task's default only affects users with no prior selection; anyone who
ticked it once gets it restored on every upgrade. → Renaming the task removes
the history it inherits, which is what actually makes the new default stick.

**Do not tag several releases in quick succession.** Four tags pushed within
an hour produced four concurrent release runs; one wedged on a runner and the
rest queued behind it, publishing releases with *no assets at all* while the
tags and commits were perfectly fine. The last one completed normally, so the
practical damage was only untidy history - but the failure mode is silent and
looks like a build break when it is really contention. Tag, wait for the run to
finish, then tag the next.

**Check release state through the web endpoints, not the API, when polling.**
Unauthenticated api.github.com allows 60 requests an hour, and a polling loop
burns that in minutes - after which you cannot see the state you are waiting
for. `releases.atom` and `releases/expanded_assets/<tag>` are plain pages and
answer both questions (what published, and with which assets).

**The release workflow creates the GitHub release *before* validating assets.**
So an installer build failure leaves a published, half-complete release — and
`validate-release`, which would have caught it, is skipped because it runs
after. v0.28.0 shipped with no installers this way. CI now resolves every
`SourceFile` reference in `npad.wxs` at lint time.

## Tooling and process

**CI's cppcheck (2.13) is stricter than a typical distro's (2.10).** Worse, an
inline suppression that 2.13 needs becomes an *unmatched suppression* error on
2.10 — the two can be mutually exclusive. → Prefer restructuring the code over
suppressing. Build 2.13 locally and point `CPPCHECK` at it (see the Makefile).

**Do not put C string literals through shell heredocs.** `\0`, `\r` and `\x20`
escapes get collapsed by an extra unescaping pass — this session wrote literal
NUL bytes into a source file that way. → Use the file-editing tools.

**Multi-line commit messages go through `git commit -F <file>`.** A `-m` message
containing double quotes breaks shell quoting; git then parses the remainder as
pathspecs, the commit silently fails, and a tag created straight afterwards
points at the *previous* commit.

**Verify a background job has actually finished before reading its output.**
Reading an in-progress result file returned "0 findings", which was reported as
a clean review; it later completed with six, including the critical one that
took the release down.

**npad itself can be inspected the same way.** `scripts/probe-prefs.ps1` posts WM_COMMAND for Preferences / Find / Replace to a fresh npad, walks the property sheet with `PSM_SETCURSEL`, captures each page with `PrintWindow`, and closes everything with `PSM_PRESSBUTTON` / `WM_CLOSE`. Two PowerShell traps met on the way: `$pid` is a read-only automatic variable (a parameter of that name fails at call time), and a static method is `[W]::Name`, never `W::Name`.

**`wsl.exe` expands `$` before your `sh -c` ever sees it.** From Git Bash,
`wsl.exe -- sh -c 'echo $HOME; $(pgrep x)'` substitutes on the way in (empty
variables, mangled loops), and Git Bash also rewrites `/mnt/d/...` arguments
into Windows paths unless `MSYS_NO_PATHCONV=1` is set. → Put anything with a
`$` in a script file and run that: `MSYS_NO_PATHCONV=1 wsl.exe -d Debian --
sh /mnt/c/.../script.sh`.

**A background process started by a `wsl.exe` call dies if that call returns
at once.** `nohup app &` as the last line of the script, with `wsl.exe`
exiting straight after, tears the session down and kills the job before it
gets going - silently, with nothing in its log. It looked like npad crashing
on launch, twice. → Keep the call alive a second or two after launching
(`sleep 1`), or start the process with `setsid`.

**GTK's Broadway backend is a non-intrusive way to look at the Linux build
from Windows** - it renders into a browser tab, not onto the desktop - but it
only paints while the page is visible. With Claude's browser pane hidden,
screenshots time out, and a page reloaded while hidden comes back blank. Keys
still arrive, but only at the window Broadway last focused, so a newly
launched window needs a click first. Screenshots are scaled (a 1280x720
viewport comes back as 800x450), so measure in the screenshot's frame. When
the pane cannot paint, verify through files: Ctrl+S and read the bytes, or
watch `~/.config/npad/recovery/`.

**Several sessions share one `broadwayd`, and `pkill broadwayd` is global.**
Another session's cleanup killed the display mid-test (the page went dark and
the app was gone). → Use your own display number (`:7` serves 8087) and run
renamed copies of `broadwayd` and the binary so a sibling's `pkill` cannot
match them. Also: the pane's screenshot trails the last action by a frame, and
the `type` action does not reach GTK widgets (use `key`).

**A user-local Xvfb (`apt-get download` + `dpkg -x`) cannot start.** It runs
the keymap compiler from a path compiled into the binary (`/usr/bin/xkbcomp`)
and has no option to move it, so without root it dies with "Failed to activate
virtual core keyboard". Use Broadway, or install `xvfb` properly.

**`path_traversal_protection` passes only because of where the checkout
sits.** It expects `file_read_text("../../../etc/passwd")` to fail, and
nothing in npad blocks traversal (by design - see `is_valid_path`). On CI the
checkout is deep enough that the path does not exist; from a directory two
levels below `/` it resolves to the real `/etc/passwd`, which reads fine and
fails the test. Build mirrors at least three levels deep, and treat the test
as asserting nothing about security.

**Other sessions share the WSL and its Broadway displays.** `broadwayd :5` and
`:7` were already in use by parallel sessions; starting a second one on the
same number fails silently and the app connects to the *other* session's
display - the screenshot then shows someone else's window, and keystrokes land
in it. `pkill broadwayd` / `pkill npad-linux` kills theirs too. → Use an
unused display number (`:23` is port 8103), record your own PIDs, and kill
only those.

**`lround` is in libm, which the Linux build does not link.** `make linux-gtk`
fails at link time with "DSO missing from command line" for a single call to
`lround`, even though the header compiles. → A one-line rounding helper, or
add `-lm` deliberately.

**A Python patch run from a heredoc can silently never match.** The Bash tool
collapses a C `"\n"` (backslash-n) in a heredoc, so a `replace()` whose search
text contained one failed its assertion and wrote nothing, and a heredoc with
apostrophes broke the shell outright. → Write patch scripts with the file tool
and run them, as with the existing heredoc advice.

**A private npad for probing, and the traps on the way.** npad reads its
settings through `SHGetFolderPath(CSIDL_APPDATA)`, which `APPDATA=` does not
move - but overriding `USERPROFILE` for the child does, provided
`%USERPROFILE%\AppData\Roaming\Platima\npad` already exists (otherwise it
exits with "Failed to initialize settings"). That keeps a probe off the real
settings.json, whose hash you can compare before and after. Start it with
`WindowStyle Minimized` and `ShowWindow(SW_SHOWNOACTIVATE)` so it never takes
focus. `PSM_PRESSBUTTON` takes `PSBTN_OK`=3, `APPLYNOW`=4, `CANCEL`=5 (0 is
BACK, 1 is NEXT - they do nothing on a sheet). Boolean settings are stored as
the strings `"true"`/`"false"`. And never `SendMessage` a command that can raise
a message box (Find Next with no further match): the sender blocks forever;
`PostMessage` it.

**Broadway cannot show GTK's popup menus, but everything else can be driven
and looked at.** Menu bar drop-downs, the context menu and combo popups never
paint (the log fills with "Negative content width" for the menu items), though
the windows and dialogs do. Dialogs also tend to be placed at negative
coordinates (in Broadway, centred on a parent that sits at 0,0), so only part shows. -> To
look at a dialog, move its `<canvas>` with the browser's JS tool (`style.left`
/ `top`; the next repaint puts it back); to look at a menu, populate it, move
its items into a vertical `GtkMenuBar` inside a `GtkOffscreenWindow` and save
`gtk_offscreen_window_get_pixbuf`; read greying from `gtk_widget_get_sensitive`.
A fresh page load fixes the pane when it gets confused about scale.

**Test the real code by compiling a harness into the real process.** Compile
`main.c` with `-Dmain=npad_main`, link a harness whose `main` queues a
`g_timeout_add` and calls `npad_main`, and it can reach every `static` in the
public API it includes (`g_gtk_main_window`, the `Window` struct) and the
actual widget tree. Synthetic input goes through the real routing:
`gdk_event_new(GDK_SCROLL)` with a window, a device
(`gdk_event_set_device`) and a state, then `gtk_main_do_event`; a drop is
`g_signal_emit_by_name(view, "drag-data-received", NULL, x, y, &sd, info, 0)`
with a struct laid out like the private `GtkSelectionData` (the NULL context
makes `gtk_drag_finish` log one CRITICAL, expected). Use `editor_set_modified`,
not `ui_set_text_modified`, to get past the save prompt - the core keeps its
own flag. `g_object_notify(window, "is-active")` re-runs an activation handler
when the window is already active, and `gtk_window_present` makes Broadway
activate it. Never put C string literals through a shell heredoc to write the
harness: a backslash-n in a string literal came out as a real newline twice.

## Method

**Measure before optimising, and let the measurement kill the hypothesis.** Two
confident root causes in this project were disproved by probing: the
`apply_word_wrap` scroll-bar theory, and "the slow launch is inside npad" (89%
of it happens before the process object exists — code signing is the fix, not
optimisation).

**A negative control is worth more than the positive result.** An assertion of
"0 amber pixels" returned 42; a control run with the feature never used returned
41. It was ClearType fringing, not the bug.

**Distinguish "no mechanism found" from "not npad's bug".** Enumerating every
scroll primitive proved npad issues no scroll of its own — but "therefore
external" was the wrong inference, and user evidence (two machines, no other app
affected, never in classic Notepad) falsified it.
