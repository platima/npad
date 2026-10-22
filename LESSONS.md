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

**Clearing `window->status_msg` does not clear the bar.** Part 0 is redrawn
only by `compose_status_part0`, and with the counts off (the default) nothing
else calls it, so "Match 1 of 3" stayed up while the user typed. Blanking the
bar *without* clearing `status_msg` is the opposite trap: the next counts
refresh or settings broadcast brings the old message back. → Every place that
retires a message clears `status_msg` AND recomposes part 0.

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

**CR CR LF looks like a blank line between two breaks; msftedit deletes it.**
It is the `EM_FMTLINES` soft-break marker, so `EM_SETTEXTEX` removes it and
joins the two lines - and the next save writes them joined. Files get CR CR LF
from Python text mode, git autocrlf and FTP ASCII transfers. → Normalise every
break to a single CR (the control's own form, with the same break rules as
`file_convert_line_endings`) before `EM_SETTEXTEX` and before any
`EM_REPLACESEL` of outside text (`richedit_text_from_utf8` /
`richedit_normalize_breaks`).

**`GT_USECRLF` does more than turn CR into CRLF.** On `EM_GETTEXTEX` it also
turns a stored vertical tab (U+000B) into CR LF, although the control holds the
VT intact (`EM_GETTEXTRANGE` and `GT_DEFAULT` show it), so every VT was saved
as a line break. → Read with `GT_DEFAULT` and widen CR to CRLF yourself
(`richedit_get_text`). In plain-text mode Shift+Enter stores CR, not VT;
`tests/test_richedit_boundary.c` pins this.

**`EM_SETTEXTEX` can change the text it is given, and says nothing.**
msftedit turns U+2028/U+2029 into paragraph breaks and BEL, U+FDD0-FDEF and
U+FFF9-FFFF (U+FFFC and U+FFFD among them) into spaces - 42 BMP code points on
Windows 11 26200 - and its return value is the same. The next save then writes
the substitutes over the original. → After loading, compare the control with
what was sent (`richedit_holds_text`) and treat a mismatch like a truncated
load: Save goes to Save As. Do not hard-code the list; the boundary test sweeps
the whole BMP and checks that the verdict is right either way.

**`EM_GETTEXTEX` can copy less than you asked for.** It returns the number of
characters copied, and `GETTEXTEX.cb` is a DWORD byte count. A `cb` that wraps
or a short copy would be saved over the file as a fragment. → Check both, and
fail the save rather than write what came back.

**A loop of `EM_REPLACESEL` calls is one undo step per call.** Replace All
took one Ctrl+Z per replacement and could run past `EM_SETUNDOLIMIT`. → To
make a multi-site edit one undo step, build the text of the span between the
first and last edit and apply it with a single `EM_REPLACESEL(TRUE)`, as the
list tools do. A single insert that must not merge with the typing around it
needs `EM_STOPGROUPTYPING` on both sides (`replace_sel_one_step`).

**`EM_SETTEXTEX` with `ST_KEEPUNDO` does not leave the undo history alone - it
records the replacement as an undoable step.** That step holds a full copy of
the document and clears Redo, and undoing it marks the document modified and
drops the font-binding faces (emoji go to boxes). The emoji font rebind hit all
of that on every font or scheme change. → To change a document's internals
invisibly, wrap the call in TOM `ITextDocument::Undo(tomSuspend)` /
`Undo(tomResume)` (`EM_GETOLEINTERFACE`, then `QueryInterface` for
`IID_ITextDocument`, which npad defines itself), and skip the operation when
TOM is unavailable rather than pollute undo.

**"Refresh only the default format" is never harmless on a loaded document.**
A `CFM_COLOR`-only `EM_SETCHARFORMAT` keeps every run's face (emoji fallback
faces survive, and no undo step is recorded), but any `SCF_DEFAULT` stamp that
carries `CFM_FACE` restyles ALL existing text in plain-text mode, not just the
insertion default. A DPI change and the dark-mode toggle both did it and boxed
every emoji. → Stamp only colour for theme changes, stamp the full default
only right before inserting new text, and follow any face change with a
rebind.

**`EM_EXGETSEL` (and `EM_GETSEL`) normalise a selection to cpMin < cpMax**, so
restoring it with `EM_EXSETSEL` turns a backward selection forward and moves
the caret to the other end. `EM_SETSEL(anchor, active)` is not a fix either:
it is only specified as a normalised range. → Read the active end with TOM
`ITextSelection::GetFlags(tomSelStartActive)` and restore with
`ITextSelection::SetRange(cpAnchor, cpActive)`. MinGW's `tom.h` names
`SetRange`'s parameters `(cpActive, cpOther)`, the reverse of Microsoft's
documentation; go by the documentation.

**RichEdit's default tab stop is a fixed half inch, not Notepad's 8
characters** (about 5.95 Consolas columns at 11 pt). `PARAFORMAT2` has no
default-tab field; the document default is TOM
`ITextDocument::SetDefaultTabStop` (in points), and `EM_SETTEXTEX` resets
document state such as the zoom, so the stop must be re-applied after every
programmatic text replacement, zoom change and `WM_DPICHANGED`. And a width
measured at one size and scaled is not the width drawn: GDI hints advances at
the device pixel size, so Consolas 11 pt measured at 1440 px/inch gives 121
twips per character where 120 are drawn at 96 DPI. → Measure in device pixels
at the control's DPI and zoom, convert back through the same scale, and round
the stop *down*: a stop even a fraction late is still ahead of text that is
exactly 8 columns long, so its tab stops there instead of at column 16.

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

**At `CBN_SELCHANGE` a combo box's edit field still holds the OLD text.**
`CB_GETLBTEXT` has the new one. Highlight all read the box with
`GetWindowText` there and highlighted the previous term. → Read the list item,
or do the work later (a coalescing timer).

**A modal dialog re-enables its owner only if it disabled it.** It looks as if
a dialog disables its owner and gives it back on close. But a prompt opened
over an owner that is already disabled (the document under Print Preview)
leaves the owner to whoever disabled it, so closing that other window
re-enables the document while the prompt is still up - the document was usable
under its own save prompt. → Close the preview, or wait for the print job,
before the document shows a prompt (`prepare_for_document_prompt` /
`print_is_busy` in ui_win32.c).

**A property sheet has no "all pages have applied" notification.** Each page's
`PSN_APPLY` looks like the place to save, but the sheet sends `PSN_APPLY` to
every created page (see the Associations entry under registry), so a per-page
save-and-broadcast ran once per page per OK, plus once more after
`PropertySheet` returned (even on Cancel), and made every other window reload
each time. → Stage writes in `PSN_APPLY` and commit once every created page has
applied: count the pages (`PSM_INDEXTOHWND` is non-NULL only for created
ones) rather than relying on send order, and return
`PSNRET_INVALID_NOCHANGEPAGE` from the last page if the commit fails.

**`TranslateAccelerator` sends `WM_INITMENUPOPUP` for the popup of every menu
item a shortcut matches** (Ctrl+S, Ctrl+V, F3...), so anything rebuilt in
`WM_INITMENUPOPUP` runs on ordinary keystrokes too. The Recent Files rebuild
checked each entry with `_waccess`, which blocks for the SMB timeout on a
sleeping share: Ctrl+S froze npad for 40 seconds. → Gate the work on `wParam`
being the specific submenu, and keep file-system I/O out of menu building
entirely.

**A virtual-key accelerator matches only its exact Ctrl/Shift/Alt state.** One
`FVIRTKEY` entry for `VK_OEM_PLUS` looks as if it covers + and Ctrl++, but a
bare entry ignores Ctrl+key and Shift+= (a typed +). → Add one entry per
modifier combination you want (see the Print Preview table).

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

**The blocking print calls are not synchronous in the way that matters.**
`PageSetupDlgW`, `PrintDlgW` and spooling with `SetAbortProc` all pump
messages, so a posted `WM_CLOSE` (Close All), a settings broadcast or
`NPAD_WM_UPDATE_DOWNLOADED` is dispatched in the middle of them, with the
caller's frame still on the stack. → Anything that would prompt, close or free
state checks `print_is_busy()` and holds back; `print_set_idle_hook` replays it
when the last print call returns.

**A measure cap does not make line breaking linear.** The old 4096-character
cap measured 4096 characters per wrapped output line (about 40 times the text)
and rescanned for the next tab from every output line, so one long line (a
one-line JSON file) was quadratic, and drawing scanned every run for every
page. → Size the measure from the width left and grow it only while everything
fits, find a run's end once, and bisect the page-ordered runs
(`core/print_core.c`, `print_layout_draw_page`).

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
re-applies the task ticks recorded at install time, so a group the user
unticked on the Associations page came back with the next update. → Hand
setup negations only, one for each group that is no longer registered:
`/MERGETASKS="!assoc\markdown,!assoc\data"` - never a positive `assoc\text`
(see "Never positively select an installer task" below;
`assoc_installer_merge_tasks` builds exactly this). And the uninstall log only
knows what setup wrote - `ValueType: none; Flags: dontcreatekey
uninsdeletekey` creates nothing but deletes the key on uninstall, whoever
made it. The WiX counterpart, `RemoveRegistryKey Action="removeOnUninstall"`,
is still right for a key only npad owns (`Applications\npad.exe` in
`npad.wxs`), but an MSI major upgrade runs it on every update, so it must never
cover anything a runtime page manages (next entry).

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

## Win32: window placement

**`GetWindowPlacement`'s `rcNormalPosition` looks like screen coordinates, but
it is in workspace coordinates**: relative to the work area of the monitor the
rectangle is on, so it is offset by a top or left taskbar (`WS_EX_TOOLWINDOW`
windows are the exception). Saving it and restoring with `SetWindowPos`
(screen coordinates) moved the window by one taskbar per session, and the "not
on any monitor" fallback never fired because the window still overlapped the
monitor. → Convert with `rcWork - rcMonitor` of `MonitorFromRect` when
reading, or use `SetWindowPlacement` in both directions. Never mix the two
systems. When checking that the title bar is reachable, use the monitor the
caption strip is on, not the one with the largest overlap.

**Maximizing after `ShowWindow(SW_SHOW)` paints the window at its restored
size first.** → Use `SW_SHOWMAXIMIZED` on the first show. The launcher's
`STARTUPINFO` can override that first `ShowWindow`, so check `IsZoomed`
afterwards and fall back to `SW_MAXIMIZE` - unless `IsIconic`, which means the
shortcut asked for Minimized.

## Win32: text and paths

**The narrow `argv` a MinGW `main()` receives is in the ANSI code page, not
UTF-8.** npad's core reads every string as UTF-8, so any non-ASCII file name
from Explorer, Open with, Send To or the command line broke: cp1252 letters
became U+FFFD and names outside the code page became `?`. Drag and drop and
File > Open use wide APIs and still worked, which hid the bug. → Rebuild
`argv` from `GetCommandLineW` + `CommandLineToArgvW`
(`src/platform/args_win32.c`). Do not use the manifest's
`activeCodePage=UTF-8` instead: it changes `CP_ACP` for the whole process,
and `CP_ACP` is how npad reads and writes ANSI files.

**Every ANSI ("A") API that returns a path gives code-page bytes that look
like UTF-8 on an ASCII-only dev machine.** `SHGetFolderPathA`,
`GetModuleFileNameA`, `getenv`, and `CreateDirectoryA` through the
`CreateDirectory` macro (UNICODE is not defined) all do. On a profile like
`C:\Users\José` the bytes decode to U+FFFD, and settings, crash recovery and
the update handoff failed without a word. `SHGetFolderPathA` also returns
`S_FALSE`, not `S_OK`, for a folder that does not exist yet. → Use the W API
(`SHGetKnownFolderPath`) and convert with `WideCharToMultiByte(CP_UTF8)`. In
the other direction convert with `MB_ERR_INVALID_CHARS`, so a mis-encoded path
fails visibly.

**In a wide format string, `%s` means `wchar_t *` only by MSVCRT's
convention.** `_snwprintf` swaps the meaning of `%s`; under C99 rules
(`swprintf`, or MinGW's `__USE_MINGW_ANSI_STDIO`) `%s` in a wide format means
`char *`. → Always write `%ls` for `wchar_t *` and `%hs` for `char *` in wide
formats; both are unambiguous everywhere.

**`GetOpenFileNameW` with a `MAX_PATH` buffer reports a longer pick as a plain
FALSE, exactly like Cancel.** Only `CommDlgExtendedError()`
(`FNERR_BUFFERTOOSMALL`) tells them apart, and it lives in `cderr.h`, which
`WIN32_LEAN_AND_MEAN` leaves out. → Use `IFileOpenDialog`, which returns an
allocated path of any length, keep the classic dialog as a fallback with a
32768-character buffer, and report its errors. `CommDlgExtendedError` is
delay-loaded from comdlg32, so it must also be listed in
`src/platform/delay/comdlg32.def`.

**The manifest's `longPathAware` does nothing unless `LongPathsEnabled=1` is
also set** (HKLM, or the "Enable Win32 long paths" policy), so long-path
behaviour cannot be seen on a default machine. Meanwhile fixed `MAX_PATH`
buffers that truncate (`wcsncpy`, `_snwprintf` into an array) quietly name a
different file. → Audit for truncation, not just overflow, and give file_ops'
paths of 240 characters or more the `\\?\` prefix.

**windres reads narrow resource strings as Windows-1252.** A literal UTF-8 ©
in `npad.rc` compiled as two characters, "Â©", in the file's properties. →
Keep `.rc` files ASCII and write the sign as the octal escape `\251`.

## Win32: processes, sessions and DLLs

**A bare `LoadLibraryW` searches the exe's own folder first.** For a DLL that
is not a KnownDLL (msftedit, riched20, dwmapi, winhttp, bcrypt, msimg32), a
portable exe in Downloads therefore loads a DLL planted beside it, and so does
libmingwex's `__delayLoadHelper2`, which calls `LoadLibraryA`. →
`SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` at the top of `main()`
covers runtime and delay loads. Static imports are resolved before `main()`;
only lld's `--dependent-load-flag` reaches those, and GNU ld has no
equivalent, so check them with `objdump -p`: they must all be KnownDLLs or
side-by-side (comctl32).

**"Is this pid npad?" cannot be answered by the exe's file name.** The
portable download keeps its release-asset name
(`npad-v<v>-portable-win-<arch>.exe`), the arm64 build may run as
`npad-arm64.exe`, and users rename copies, so a check for `npad.exe` made
every other live window's crash-recovery snapshot look like a crash leftover.
→ Treat a process as npad if it owns an `NpadMainWindow` top-level window, or
its image name is npad's or this process's own, and confirm it is the same
process with its start time (see "A pid alone does not identify..." under
crash recovery).

**A registered message posted to an elevated window is dropped silently.**
`PostMessageW` of a `RegisterWindowMessage` id to an npad started with Run as
administrator returns TRUE, and the window still enumerates and counts as
present, but UIPI never delivers it - so settings changes, Close All and the
update handoff skipped that window. → The receiving window must call
`ChangeWindowMessageFilterEx(MSGFLT_ALLOW)` for each broadcast message - and
only when it runs above medium integrity, or a normal npad accepts its close
messages from low-integrity processes too.

**Returning FALSE from `WM_QUERYENDSESSION` does not get the app killed
straight away.** Windows lists it on the "apps preventing shutdown" screen with
its `ShutdownBlockReasonCreate` text, and the user can still force the
shutdown. An old comment assumed otherwise, so npad agreed to end the session
even after parking unsaved work had failed. → When parking fails, block with a
reason ("npad has unsaved changes that could not be kept safe...") and remove
it on the next successful query and in `WM_ENDSESSION`.

**A handle with write access blocks `CreateProcess` on that file** (a sharing
violation). To keep a verified installer unchangeable until it runs: write it,
close the writer, reopen it `GENERIC_READ` with `FILE_SHARE_READ` only (which
blocks writers, delete and rename), verify through that handle
(`WinVerifyTrust` takes `hFile`), and close the handle only after the process
is created. This is reasoned from the documented share-mode rules and still to
be confirmed on a VM.

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
  which is the whole reason it survived. Make it a property test over whole
  files, not hand-picked values: the fuzz suite's first run found a key with a
  backslash growing on every save (the same bug, in keys) within 100
  iterations, where the hand-written round trips had checked values only.
- **Reject implausible input on load, not just on write.** A fix that only
  stops *creating* corruption leaves existing users unable to start. Bounding
  the file size and the value length lets a broken install heal itself.
- **A serialiser that cannot fit everything must fail, not truncate.** Silently
  writing a well-formed file that is missing data is far worse than refusing:
  the caller keeps the previous file, which is the safe direction.

**A guard that discards data is a data-loss bug wearing a fix's clothes.**
v0.28.7 protected against an oversized settings file by ignoring it - and
because npad then rewrote the whole settings file on exit, the original was
overwritten. The
release that fixed the corruption destroyed the settings of the people it was
meant to rescue. Salvage what parses, move the rest aside, and never leave a
path where the only copy is deleted by the next ordinary operation.

**Beware anything every instance loads at startup.** Cost is multiplied by
window count, and npad is one-process-per-window. A file that is merely
annoying at one instance is fatal at twenty.

**npad writes settings.json on every exit** — `main()` calls
`settings_save_window_state()` then `settings_save()` after the message loop.
Until 0.36 that rewrote the whole in-memory store, unconditionally: with a
corrupt 1.2 GB file every one-second launch wrote 1.2 GB, it is why a fresh
settings.json reappeared moments after the corrupt one was renamed away, and
it made the exit save "last writer wins" - the window closed last overwrote
every other window's changes. Since 0.36 a save is read-modify-write: it
re-reads the file and writes its current contents plus this window's own
changes. The exit save still always writes, because window geometry is forced
to count as a change (see "a set that does not change the value" below).

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

**Moving where settings live is a migration, and the old file is the only
copy.** `settings_get_config_dir` now honours `$XDG_CONFIG_HOME`, so a user who
exports it after months of `~/.config/npad` would open npad to defaults - and
because npad rewrites settings on exit, the defaults would then be saved
(the same shape as the v0.28.7 loss above). → `settings_init` reads the old
file when the new location has none, in memory only: it never renames or
deletes it (even an oversized one is salvaged without the `.corrupt` rename,
`load_settings_from(path, may_move_aside)`), and the next save writes the new
file. An empty or relative `$XDG_CONFIG_HOME` is ignored, as the spec says.
Not carried over: recovery snapshots under the old directory.

**`settings_load` no longer means "replace memory with the file".** Since the
merge-saves it merges, keeping this instance's unsaved sets and removes on top
of what it reads. Code that relied on a reload to throw away values that never
reached disk - GTK Preferences after a failed Apply, and Import on both
platforms - kept them. → Where the file must win, call
`settings_discard_unsaved_changes()` first (or discard just the keys you
wrote).

**With merge-saves, a set that does not change the stored value is
deliberately not a change.** So a window that re-asserts a value it already
holds loses quietly to another window's newer one. Code that must win even
when the value looks unchanged (window geometry at exit) uses the forced path
(`set_string_internal` with `force_dirty`); anything else that has to win
needs the same treatment. Recent Files and the find history are saved as
whole lists (`settings_set_list`), because an entry-by-entry merge listed a
file twice and left gaps that hid the rest of the history.

**`settings_save` returns true without writing when nothing changed** and the
file parsed in full. A test that does clear, load and save without changing
anything therefore never rewrites the file after the first cycle and passes
vacuously. → Make a real change before each save (remove a key and set it
back), and compare the saved bytes two cycles apart (the reader before 0.36
prepended entries, so consecutive saves reversed the order).

**`editor_enable_session_resume()` saves settings.json itself.** It looks
like a pure setter, so a caller that batches its saves (Preferences' one save
per Apply) still gets an extra write whenever it calls it.

**glibc's `isspace` depends on the locale, and GTK switches to the user's.** A
parser that uses `isspace` (or `strtod` / `printf` for decimals) can behave
differently under GTK than in the unit tests, which run in the C locale. → The
settings reader uses an explicit JSON whitespace set, and doubles are
converted to and from `.` at the boundary.

**`(UINT) seconds * 1000` looks safe for any positive int, but it wraps at
4294968 seconds into a sub-second timer**, and `settings_get_int` was `atoi`,
which (in practice; it is undefined in C) turns a value above `INT_MAX`
negative - that switched the large-file warning off. → Read bounded numbers
with `strtol` / `strtoll` and range checks, fall back to the default on
malformed text, clamp in core (`ui_rules.c`), and compute timer periods in 64
bits.

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

**Windows ANSI decoding never fails on bad input; it substitutes.**
`MultiByteToWideChar` without `MB_ERR_INVALID_CHARS` replaces invalid DBCS
bytes, and in cp932/936/949/950 a stray lead byte also swallows the next byte,
CR or LF included, so two lines merge. `WideCharToMultiByte`'s best fit then
makes the save look fine. → Decide lossiness by re-encoding the decoded text
exactly as the save will (same code page, `WC_NO_BEST_FIT_CHARS`) and
comparing with the raw bytes, and split DBCS input at CR/LF bytes, which can
never be trail bytes (every trail range starts at 0x40).

**`GetACP()` is not always the ANSI code page.** With Windows' "Beta: Use
Unicode UTF-8" it returns 65001, and a file only reaches npad's ANSI branch
because it is *not* valid UTF-8, so decoding it as `CP_ACP` turned every such
byte into U+FFFD. `WC_NO_BEST_FIT_CHARS` is also invalid for 65001. → Go
through `ansi_code_page()` in file_ops.c (the locale's legacy code page in
that case), never `CP_ACP` directly.

**A check that predicts a conversion must make the identical call.** The
lossy-ANSI warning used `WC_NO_BEST_FIT_CHARS` and the save used 0 (best fit),
so the save wrote U+221E as `8` and ł as `l` while the warning promised `?`.

**A zero-initialised `TextFileInfo` is a safe default only if every flag is
phrased so that false is the safe state.** That is why the field is
`utf16_no_bom` (false: write a BOM, as for new files) rather than `has_bom`.
`load_default_file_info` also used to set only the encoding and line ending, so
flags from the previously opened file survived File > New. → Give new fields a
safe false meaning, and reset them everywhere the struct is reset.

**A string compare is not a substitute for asking the search engine whether a
selection is a match.** Both Replace implementations compared the selection
with the search term, and both refused matches their own Find Next had just
selected: `_wcsicmp` folds only ASCII in the C locale, and GTK's
case-insensitive search matches through Unicode normalisation, which
`g_utf8_casefold` + `strcmp` does not. → Re-run the same search from the
selection start and accept the selection only if the match found is exactly
the selection.

## Core: file I/O

**Logging an error can change the error.** The default error callback writes
to stderr; in a `-mwindows` process started from Explorer there is no stderr,
so the write set `errno` to `EBADF`, and code that read `errno` after logging
reported "Bad file descriptor" for every failed open. → Capture `errno` (and
`GetLastError`) before logging and pass it on explicitly. The logger now
restores both, and skips the write when there is no stderr.

**`ReplaceFileW` without a backup name has a documented failure
(`ERROR_UNABLE_TO_MOVE_REPLACEMENT`) that leaves the original under no name at
all.** → Always pass a backup name, and afterwards check whether the original
ended up there (move it back if so). With a backup name, `ReplaceFileW` keeps
the ACL, owner, attributes, creation time and alternate streams
(Mark-of-the-Web); renaming the temp file over the original with `MoveFileEx`
keeps none of the original's.

**Restoring a file's mode after the rename is not enough on POSIX, and
copying it before the data is written is wrong too.** A temp file created
0644 and chmodded later can be opened by another user in between, and that
open descriptor can still read the secret written afterwards. And the first
`write` (or `ftruncate`) by a process without `CAP_FSETID` clears `S_ISUID`,
`S_ISGID` and `security.capability`, so identity applied before the data does
not survive it - a check run before the write passes and proves nothing. →
Create the temp file 0600, write the data, then copy the identity: `fchown`
first (it clears the set-id bits too), then `fchmod`, then compare with
`fstat`; then fsync, verify and rename. The in-place fallback restores the
set-id bits its own write cleared.

**The in-place fallback is only as crash-safe as its backup's location.**
`/tmp` is a tmpfs, or cleaned at boot, on Fedora, Arch and Debian 13+, so a
backup there is gone after exactly the power cut it exists for. → The backup
goes to the persistent recovery folder (`file_ops_set_backup_dir`, set by
`editor_init`), with `TMPDIR` / `%TEMP%` only as a fallback.

**`file_copy` is not a safe way to replace a file.** It opened the destination
for writing (truncating it) before it had written anything, so copying onto
settings.json with a full disk left a partial file and lost the original. →
Use `file_write_text_atomic` or `file_write_binary_atomic` for anything that
replaces a file the user depends on (`file_copy` now goes through the atomic
writer too).

**`near` and `far` are macros in windows.h.** A parameter named `near`
compiles on Linux and breaks only the MinGW build, with "expected expression
before ';' token". → Avoid those names in any file that includes windows.h.

## Core: editor and crash recovery

**`NPAD_ERROR_FATAL` looked like "log, then the caller cleans up and returns
1"** (main.c is written that way), but the default callback called `abort()`,
so every cleanup path after a fatal report was dead code. The callback also ran
while holding the error mutex: a Win32 critical section is recursive and a
default pthread mutex is not, so a callback that reports again (or shows a box
whose message loop leads to a report) worked on Windows and deadlocked on
Linux. → Callbacks run on a copy outside the lock, FATAL only logs, and an
`npad_mutex_*` must never be locked by a thread that already holds it
(`thread_safety.h` says so).

**Timers keep firing inside modal dialogs.** Win32 `MessageBox`, `TaskDialog`
and `GetSaveFileName`, and GTK's `gtk_dialog_run`, all run a nested loop that
dispatches timers, so `UI_EVENT_AUTO_SAVE` arrived in the middle of "Save
changes?", the Save As dialog or another prompt. It could save right under
"Don't Save", and a failing auto-save nested a new error box per interval. →
Nothing fired by a timer may show UI, and it must check editor.c's
`g_modal_depth`; wrap every new modal the editor raises with it
(`editor_ask`, `editor_run_save_dialog`).

**"Keep editing" in the changed-on-disk prompt looked harmless because it only
re-stamps the baseline.** With auto-save on, re-stamping meant the next tick
overwrote the other program's version, though the button says nothing is
written. → Accepting a real change sets `auto_save_held`, which only a manual
save, Save As, reload or new document clears. Auto-save checks the file's
stamp itself, not through `editor_check_external_change`, which returns
UNCHANGED whenever watching is off or suppressed.

**Commit Save As state only after the write succeeds.** Setting
`g_editor.file_info.encoding` from the dialog before saving looked fine
because the save normally succeeds; on any failure or decline the document
was left with an encoding the status bar did not show, and the next Ctrl+S
re-encoded the original. → Build a local `TextFileInfo` for the write and
commit it to `g_editor` only after `file_write_text_ex` succeeds.

**A Yes/No bool cannot tell No from "not answered".**
`ui_show_message_box(..., true) == false` looked like the user said No. But on
GTK, Esc and the close box return `GTK_RESPONSE_DELETE_EVENT` and a dialog
destroyed under `gtk_dialog_run` returns `GTK_RESPONSE_NONE`, and on Win32 a
`WM_QUIT` reaching `MessageBox`'s loop ends it with no answer. All of them came
back false, and the recovery prompt deleted every crash snapshot. → Anything
irreversible on No uses the tri-state `ui_ask_question` and acts only on
`UI_ANSWER_NO`.

**A pid alone does not identify a recovery slot's owner, even when the name
check passes.** After a reboot the same pid can belong to a newer npad, and on
GTK even to the scanning process itself, since `pid == getpid()` counts as
alive; on Linux `kill(pid, 0)` failing with `EPERM` is another user's process,
which never owns a slot in this user's folder. → Compare the process start
time (`GetProcessTimes`; `/proc/<pid>/stat` field 22 plus `btime`) with the
time in the slot id.

**To extend a file that older versions also read, put new fields where the
old parser stops looking.** npad before 0.36 reads the slot meta's line 2 with
`atoi`, so attributes after the number (` truncated=1 stamp=...`) are
invisible to it; the path has to stay last because it may contain a newline.
The same goes for the slot id: npad up to 0.36.0-rc1 reads the pid after the
last `-`, so new ids keep the pid last (`<time>-<nonce>-<pid>`), or an older
window running beside a new one would take its live snapshots for crash
leftovers.

## Core: clipboard HTML (rich paste)

**Lists paste must not strip tags from HTML that has no list.** Code editors
and terminals copy one styled `<div>` or `<span>` per line, and Excel copies
table cells, so tag stripping lost indentation and columns that the source's
own plain text kept. → Convert in Lists mode only when an `<li>` (or a Word
`mso-list` paragraph) was really parsed; otherwise return "" so the caller
pastes the plain text.

**Google Docs wraps the whole HTML payload in `<b style="font-weight:normal"
id="docs-internal-guid-...">`,** and real bold is on `<span
style="font-weight:700">`. Treating `<b>` as bold put `**` around the whole
paste. → Read `font-weight` / `font-style`, and have each close tag undo
exactly what its own open tag added (an element stack, not an on/off flag).

**`white-space: pre-wrap` is not a code signal.** Google Docs puts it on every
span, and chat apps use it on prose. VS Code and Windows Terminal use
`white-space: pre` on a block `<div>`. → Only `pre` on a `div` / `p` becomes a
code fence.

**Word does not write `<ul>` / `<li>`.** Its lists are `<p
style='mso-list:l0 level2 lfo1'>` paragraphs, with the bullet or number glyph
inside `<![if !supportLists]>...<![endif]>` (or `<!--[if !supportLists]-->` in
Outlook). The level comes from the style; numbered or bulleted comes from the
glyph text.

**Excel's CF_HTML fragment holds bare `<tr>` / `<td>` rows**: the `<table>`
tag sits outside the StartFragment/EndFragment range. → Start a table at the
first row.

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

**In a Debian `Version` field the last hyphen starts the Debian revision.**
`Version: 0.36.0-rc2` is upstream 0.36.0, revision rc2, which dpkg sorts
*after* 0.36.0, so apt never replaced a release candidate with the final
release. → Spell a pre-release `0.36.0~rc2` inside the package (nfpm does;
`build-deb.sh` now does too) while the file name keeps the tag's spelling, and
check the order with `dpkg --compare-versions`.

**nfpm expands `${VAR}` in `contents` only when the entry says `expand: true`.**
Environment variables work in `version`, `release` and the `depends` lists
without any switch, which makes the `src:` failure ("Glob failed:
`${STAGE}/usr/bin/npad`: no matching files") look like a bug in your path.
Likewise `type: tree` makes the rpm *own* the directories it walks (it listed
`/usr/share/icons/hicolor`, which belongs to hicolor-icon-theme), so
`nfpm.yaml` lists files individually. An empty `release` gives a deb version
without a `-1` revision; the rpm needs one.

**nfpm has no riscv64 binary, but it packages for any architecture.** Its
releases carry x86_64 and arm64 (and others), not riscv64, so a riscv64
container cannot run it. -> Compile inside the emulated container, package on
the host: `nfpm.yaml` says `arch: ${NPAD_ARCH}` (nfpm expands variables in
`arch` without `expand: true`, as in `version`) and writes the rpm's own
spelling (`aarch64`, `x86_64`) from the Debian name. The architecture comes
from the binary's ELF header, read with `readelf`: an amd64 host's `objdump`
may not recognise a riscv64 file at all, where `readelf` reads any ELF.
`build-package.sh` refuses a binary whose header disagrees with `PKG_ARCH`,
because a mislabelled package installs and then cannot execute.

**nfpm's archlinux packager: `pkgver` is `version-release`, the description is
flattened into `pkgdesc`, and `license: MIT` is not enough.** `release: 1` becomes `pkgver = 0.35.1-1` in `.PKGINFO`, which is
what pacman wants (`npad-0.35.1-1-x86_64.pkg.tar.zst`). The multi-line
description (the deb's synopsis plus long text) comes out as one 200-character
`pkgdesc`, so `build-package.sh arch` hands nfpm a copy of the config with the
synopsis alone. `licence: MIT` is not enough for namcap: Arch wants the file
under `/usr/share/licenses/<pkg>/`, added to the Arch package only with
`packager: archlinux` on the content entry. Arch has no official aarch64 or
riscv64 container image: `scripts/ci-arch-rootfs.sh` imports Arch Linux ARM's
rootfs (its `latest` moves, so the detached signature is verified against the
pinned Build System fingerprint; the `.md5` beside it is plain http and proves
nothing) and the Arch RISC-V mirror's (no checksums published, so one dated
file's SHA-256 is pinned). Do not swap in a Docker Hub rebuild. A rootfs
imported this way needs `pacman-key --init && --populate` and a full
`pacman -Syu` before `pacman -U` (Arch supports no partial upgrades, and these
images can be months old).

**Fedora publishes no riscv64 container image; openSUSE Tumbleweed does.**
Debian (trixie) and Ubuntu (24.04) have riscv64 images, Debian 12 does not
(no official port, which is also why riscv64's floor is Debian 13 / Ubuntu
24.04). openSUSE has no package named `gtk3`, but `libgtk-3-0` provides it,
so the rpm's `Requires: gtk3 >= 3.24` resolves there; zypper refuses an
unsigned local rpm without `--allow-unsigned-rpm`.

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
turns the result into an `int`. In the GTK files use `g_strdup` (GLib's
allocator is plain `malloc` since 2.46, so core's `free()` is still right), or
define `_DEFAULT_SOURCE` before the first `#include`, as several GTK files
already do for `setsid`, `kill` and `dlopen`.

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
The same rule governs Preferences' Apply: a control stages a change only when
it differs from the setting, and Apply writes just the staged keys, never the
whole page.

**`g_spawn_check_exit_status` is deprecated in GLib 2.70, and its replacement
needs 2.70.** Debian 12 and Ubuntu 22.04 ship 2.74 / 2.72, but the build
warns on both names depending on the minimum you target. -> `g_spawn_sync`'s
status is a plain wait status on Unix: `WIFEXITED(status) &&
WEXITSTATUS(status) == 0`.

**A modal `gtk_dialog_run` does not stop settings broadcasts.** The main loop
keeps running under it, so another instance saving settings re-runs
`npad_gtk_reload_and_apply_settings` while Preferences is open, and the
dialog's controls then show values older than `settings.json`. That is safe
only because Apply writes just the keys the user changed; a dialog that saved
all its controls would overwrite the other instance's change.

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

**A square hicolor icon that fills its canvas edge to edge looks oversized and
"stretched" in a panel.** The report was a stretched taskbar icon on Mint MATE.
Measured against the source, no size was distorted (each file is the 512 px art
at one uniform scale, to within antialiasing). The artwork itself is 483 x 512
of ink, nearly square, and had been resized to the full canvas with no margin,
while the line-art icons around it keep 1-2 px at 16-24 px; a near-square
block with no breathing room reads as big and wide. -> Compare against the
source before hunting an aspect-ratio bug, then give every size the
Adwaita-style margin (`scripts/make-linux-icons.py`: crop to the ink, one scale
factor, centre, render at 8x so the art sits on half pixels). The old files
were also palette-quantised, which made the small sizes look rough.

**Icons for light and dark are two icon *names*, not a theme.** hicolor has no
dark variant mechanism, and the launcher's `Icon=` is read by the shell
independent of the app. -> Install `<id>-dark` beside `<id>` and have the
window call `gtk_window_set_icon_name` (and `gtk_window_set_default_icon_name`
for dialogs) when `g_dark_mode` changes; GTK refreshes `_NET_WM_ICON`. The
About dialog reads the icon name back from its owner window, so it follows.

**A GDBus signal subscription filtered by a well-known sender never matches
`gdbus emit`.** The portal's `SettingChanged` is subscribed with sender
`org.freedesktop.portal.Desktop`, which GDBus resolves to the unique name that
owns it; a test that fires the signal with `gdbus emit` comes from the
`gdbus` process's own unique name and is silently dropped. → To exercise the
`system` colour scheme, stand in for the portal: a small GDBus program that
owns `org.freedesktop.portal.Desktop`, answers `Settings.Read`
and emits `SettingChanged` itself, inside `dbus-run-session` (so the real
session bus is untouched). `Read` returns the value wrapped in a *second*
variant (`(v)` holding a `v` holding the `u`), `SettingChanged` carries it in
one - unwrap in a loop. The signal also fires for every other portal setting,
so filter on namespace and key and report only a *different* value, or every
unrelated change re-themes an idle window (see "Idle windows are not idle").

**Ask the portal lazily.** The first `Read` can start the portal service, so
only a window whose scheme is `system` asks (synchronously, 1 s timeout, so it
opens in the right colours rather than flashing the wrong ones); `light`, the
default, never touches it. Value 0 means "no preference" - fall back to GTK's
own `gtk-application-prefer-dark-theme` as it stood at startup.

**`gtk-application-prefer-dark-theme` does nothing for most themes.** It
selects the `gtk-dark.css` variant inside a theme, which Adwaita and Yaru have.
Mint-Y, Arc, Materia and most others ship dark as a *separate* theme
(`Mint-Y-Dark`), so on Linux Mint the property was silently ignored: the
`dark` scheme gave a dark page in a light frame, and `system` could not follow
a dark portal (the maintainer's MATE session reported `color-scheme` 1 and
npad stayed light). Broadway, with Adwaita, hid it. → For a dark scheme also
set the process's `gtk-theme-name` to the installed sibling, remembering what it
replaced so a light scheme can put it back (`apply_chrome_variant`). Test it
with fake `FakeTheme` / `FakeTheme-Dark` folders under a throwaway
`XDG_DATA_HOME` and `gtk-theme-name` in a throwaway `gtk-3.0/settings.ini`.

**Another app going dark does not mean the theme can.** On Mint (Cinnamon,
TraditionalOk) GNOME Terminal's menu bar and preferences followed the desktop's
dark preference while npad's stayed light. npad only had GTK's prefer-dark
hint, which TraditionalOk (no `gtk-dark.css`, no dark sibling) ignores; how
Terminal darkens itself on that theme was not established (it may use its own
fallback). Ask for the Debug page output (`Theme has a dark variant`, `desktop
colour-scheme via portal`) rather than inferring from a screenshot. For a light
theme with no way to go dark, npad swaps to the built-in Adwaita (always
present, has a dark variant); a theme whose window background is already dark
(BlackMATE) must be left alone, so measure it (`theme_chrome_is_light`).

**To read what a GTK theme paints, build the path from a real widget.** The
page colour lives on the text view's `text` child node. A `GtkWidgetPath` made
by hand (`append_type` window, text view, then a `text` node) answers
"transparent" for every theme; the same path copied from a throwaway
`gtk_text_view_new()` with a `text` node appended answers correctly.
Measure on a throwaway so npad's own provider (attached to the real text view)
does not colour the answer. Some dark themes (BlackMATE) keep text areas white,
so a dark `system` scheme must check the page, not just the chrome.

**Broadway's monitor reports a 0x0 geometry**, so a placement check that asks
whether a window overlaps a monitor says "off screen" for everything there.
`position_is_reachable` skips zero-area monitors and, if none is measurable,
declines to judge. To test the overlap logic anyway, compile the function into
a harness with `#define gdk_monitor_get_geometry` pointing at a fake.

**A dialog's entry takes the PRIMARY selection, and that collapses the text
buffer's selection.** `gtk_widget_grab_focus` on a GtkEntry selects its text,
which makes the entry the PRIMARY owner; a GtkTextBuffer whose selection loses
PRIMARY moves its selection bound onto the caret. So a tool that opens a dialog
and then acts on "the selection" (Custom Indent, Convert Delimiters'
"Selection only") finds none: the selected lines were indented as one line, or
the conversion ran over the whole document, with the dialog showing "Selection
only" ticked a moment earlier. -> Note the selection (offsets, and which end is
the caret) before the dialog runs and put it back once it is destroyed.

**`gtk_text_iter_forward_to_line_end` skips a whole line when it is already
there.** Called on an iterator at the end of its line (an empty line, or a
caret after the last character) it moves to the end of the *next* line. -> Test
`gtk_text_iter_ends_line` first; "the end of this line" is "this iterator, or
forward_to_line_end if it does not end the line".

**Browsers offer `text/html` on the X11 clipboard as UTF-16 with a BOM, other
sources as UTF-8, and nothing says which.** Reading it as UTF-8 gives a string
that is empty at the first NUL, so a rich paste silently falls back to plain
text. -> Sniff: FF FE / FE FF is UTF-16 with a BOM, an ASCII byte next to a NUL is
UTF-16 without one, otherwise UTF-8 (minus an EF BB BF BOM). `html_cf_extract_fragment`
then takes the fragment from the markers or the whole text.

**Broadway cannot test the clipboard between processes, and drops synthetic key
events.** A second GTK client on the same Broadway display does not see the
first one's clipboard (no targets, no text), so "set `text/html` from another
process" is not possible there; make the harness the owner and test the payload
decoder directly. Keys sent with `gtk_main_do_event` never reach the window,
while `gtk_propagate_event(text_view, ev)` does. Broadway's keymap has
keycode == keyval, so Ctrl+Shift+letter does not resolve to the accelerator
GTK would match on a real keymap; call `gtk_accel_groups_activate(window,
lowercase_key, state)` for those.

**`dpkg -S` answers for the path the package lists, not the file you ran.** On
a merged-/usr system `dpkg -S /usr/bin/ls` finds nothing, because coreutils
registers `/bin/ls`; the probe looked like a broken ownership query. npad's
package ships `/usr/bin/npad` and `/proc/self/exe` resolves the `/bin`
symlink to it, so asking about the resolved path is right for npad - but test
the query against a file a package registers under `/usr/bin` (`/usr/bin/dpkg`).

**After a package upgrade, `/proc/self/exe` names a file that no longer
exists.** The kernel reports the replaced inode as `/usr/bin/npad (deleted)`,
so anything that reads the link and uses the path - the update check's
package-ownership query, and spawning npad again for New Window and recovered
windows - fails, and the spawn failed silently. → Read it through
`npad_gtk_self_exe_path()`, which strips the suffix. To start another window,
run that path when it is executable (after an upgrade it is the new version,
which is what the user installed) and fall back to `/proc/self/exe`, which
still executes the running image; report a failed spawn instead of a
`g_warning` nobody sees.

**"The first npad window" on the session bus includes other people's
windows.** The Linux launch check runs only when no other npad instance is on
the bus (each window is its own process, standing in for Windows' primary
window). In WSL every session shares one bus, so a launch check in Broadway
silently stood down because sibling sessions had npads open on other
displays. -> Test it under `dbus-run-session -- npad-linux`.

**A `GtkMessageDialog` heading is bold only when there is secondary text, and
the secondary text is centred as a block.** A label added to the message area
under the secondary text sits a few pixels left of it, and dropping the
secondary text for labels of your own makes the heading plain. -> Own labels
with `xalign 0` for the body, and bold/larger markup on the heading
(`gtk_message_dialog_set_markup`).

**GtkNotebook does not take Ctrl+Tab.** A notebook looks like a Windows
property sheet, where Ctrl+Tab / Ctrl+Shift+Tab change page from anywhere in
it. GTK binds only Ctrl+PageUp / PageDown, and only while focus is on the
tabs; Ctrl+Tab inside a page just moves the focus. → Handle Tab /
ISO_Left_Tab with Control in a `key-press-event` on the dialog
(`on_prefs_key` in `ui_gtk_prefs.c`). In Broadway, Ctrl+, does not reach the
accelerator either; open Preferences with Alt+E, S there.

**`gtk_text_buffer_paste_clipboard` is asynchronous, and its edit looks like
a keystroke.** The text arrives after the call returns, in a GTK user action
holding one edit, which npad's undo recorder cannot tell from typing, so a
paste of "x" after "abc" merged into the typed word. Wrapping the call in
`undo_begin_group` / `undo_end_group` does not help, because the group closes
before the text arrives. → Do the paste yourself (`gtk_clipboard_request_text`,
then insert inside an undo group in the callback). For edits GTK starts by
itself (middle-click PRIMARY paste, Shift+Delete, text drops) put `undo_break`
on both sides rather than a group: a group opened in a "before" handler whose
"after" handler never runs (one that stops the emission, as the file-drop
handler does) would swallow every later edit into one step.

**A `GtkMenu`'s `deactivate` fires before the chosen item's `activate`.** A
popup destroyed in its `deactivate` handler destroys the choice with it, so
the item never runs; not destroying it leaks one hidden menu per click. →
Destroy it from an idle callback queued in `deactivate` (`popup_status_menu`).

**A GtkWindow's `destroy` handlers run before its children are destroyed.**
`destroy` is a RUN_CLEANUP signal and GtkContainer destroys its children in
the class handler, so a dialog's entries can still be read in its own
`destroy` handler; that is how Find adopts the typed term when it closes. On
Win32 the same holds for `WM_DESTROY`, which reaches the dialog before its
children.

**`gtk_dialog_run` returning `GTK_RESPONSE_NONE` means the dialog was
destroyed while it ran**, and by then it may already be freed, so calling
`gtk_widget_destroy` on it afterwards is a use-after-free. → Guard it with
`g_object_add_weak_pointer` (`run_and_destroy` in `ui_gtk_dialogs.c`).

**A quit that arrives while a GTK modal dialog is up does not close the
dialog.** `gtk_main_quit` ends the outer `gtk_main` once the dialog returns,
so the user's answer is then acted on in a window that is already going. →
Record that a quit was requested (editor.c's `g_leaving`) and ignore the
answer.

**`gtk_text_buffer_set_text(buf, text, -1)` measures with `strlen` into a
`gint`.** Over 2 GB the length goes negative (nothing is inserted) or wraps to
its low 32 bits (a fragment is inserted), with no error, and Save then writes
that over the original. `insert_at_cursor(..., -1)` is the same. → Check the
length against `G_MAXINT` first (`npad_gtk_text_fits`) and pass it
explicitly.

**GTK 3 outside `GtkApplication` never talks to a session manager**, and GTK 3
dropped XSMP, so a logout gives npad no notice at all and unsaved work was
lost. → Register a D-Bus client with `org.gnome.SessionManager`
(`RegisterClient(app_id, DESKTOP_AUTOSTART_ID)` returns a client path; the
signals `QueryEndSession`, `EndSession`, `CancelEndSession` and `Stop` come on
`org.gnome.SessionManager.ClientPrivate`; answer with `EndSessionResponse`),
plus `org.xfce.SessionManager` and a logind delay inhibitor for shutdown. Use
`call_sync` for the `EndSession` answer: an async call still queued when npad
exits is never sent. Leave a SIGHUP or SIGINT that npad inherited as ignored
(`nohup`, a script's background job) ignored.

**GTK's X11 backend exits the process the moment the display connection
drops.** It installs its own Xlib IO-error handler, which calls `_exit`, so no
`atexit` or main-loop code runs. → To act first, replace it after `gtk_init`
with `XSetIOErrorHandler`, found with `dlsym(dlopen(NULL), ...)` since
libgdk-3 has already loaded libX11 (that keeps `-lX11` off the link line, and
glibc 2.34+ needs no `-ldl`), and chain to the previous handler so GDK still
reports and exits. Wayland has no equivalent hook.

## Installers

**Release asset names drifted once already.** The Windows downloads were
`npad-v<v>-<type>-win-<arch>`, and the portable Linux tarball arrived as
`npad-v<v>-linux-<arch>` with no type and Debian's architecture names, so the
two looked like different schemes. -> The convention is written in CLAUDE.md
("Release asset names"); `scripts/validate-release-assets.sh` enforces it.
Packages for a package manager keep their own ecosystem's names on purpose.

**A pre-release tag cannot be the installers' version.** MSI's ProductVersion
and Inno Setup's AppVersion must be numeric, so `build-installers.ps1` builds
from `src/main.h` (0.36.0) - and it also named the files from it, while the
Linux packages and `validate-release-assets.sh` use the tag (`v0.36.0-rc1`).
The first release candidate failed validation for it, after every file was
already attached. → The version inside an installer and the version in its
file name are separate: `-FileVersion` (the tag, from installers.yml) names
the files, `$Version` stays numeric.

**Inno Setup's check lists do not scale their check box offset.**
`TNewCheckListBox.Offset` defaults to 2 (Components) and 4 (Tasks) raw
pixels, and stays there at any DPI while the glyph scales; at 150% with the
default `WizardSizePercent` (`ScaleX(100)` = 167) the top-level boxes are drawn
half outside the list. Not the wizard style: identical in `modern`, `classic`
and `excludelightcontrols`, on 6.7.0 and 6.7.3. Set
`WizardForm.ComponentsList.Offset := ScaleX(8)`, and the same for
`TasksList` and `RunList` (the "Launch npad" box on the Finished page), in
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
before every release. An Environment credential matches on any ref; the
signing job then declares `environment: release`. Since this repository was
switched to immutable subjects (entry above), the subject is the ID-based
`repo:<owner>@<owner_id>/<repo>@<repo_id>:environment:<name>`; the name-based
`repo:<owner>/<repo>:environment:<name>` no longer matches. Copy the subject
from the AADSTS error rather than building it by hand.

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
condition. **Ask the user first:** it adds a certificate to their real
`Cert:\CurrentUser\My` store, and it must be removed again afterwards.

**The wizard can be inspected without touching the keyboard or mouse - through
the PID you started, never by class.** The bootstrapper you start spawns
`<name>.tmp` for the wizard as its CHILD. → Find it through the
`Win32_Process` parent chain (`ParentProcessId`, filtered by `CreationDate` >=
the root's start time) and look for `TWizardForm` only among the windows
those PIDs own. A class-only lookup drives whatever Inno wizard is on the
desktop, including another product's installer, which the old
`probe-installer.ps1` did. Then `BM_CLICK` on the `&Next` / `I &accept`
buttons walks the pages (sent with `SendMessageTimeout`, so a modal box cannot
hang the probe), `PrintWindow` with `PW_RENDERFULLCONTENT` captures them, and
`WM_CLOSE` + `BM_CLICK` on that wizard's own "Exit Setup?" Yes button leaves
without installing (exit code 2). Never kill by name; anything that does not
exit is left running and reported. `/LOG=` plus `Log()` in `[Code]` reads any
property back. `scripts/probe-installer.ps1` now does exactly this (it refuses
`/SILENT` and `/VERYSILENT` and never presses Next on the Ready page); it
still puts a wizard on the user's desktop, so run it only when the user has
asked for it. Local variants build with ISCC from a copy of the script with
the font `Source:` lines removed - the fonts are not in the tree.

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

**A release created before its assets are validated is published
half-complete.** An installer build failure left a published release with
missing files, and `validate-release`, which would have caught it, was skipped
because it ran after. v0.28.0 shipped with no installers this way, and the
in-app updater could find a new release whose installer was still uploading.
→ release.yml now creates a draft, uploads and validates every asset, and
publishes last; a failed run leaves an unpublished draft that a re-run of the
same tag reuses. CI also checks every file the installers reference
(`scripts/ci-installer-refs.sh`: every `SourceFile=` and `Source=` in
`npad.wxs`, every `[Files]` `Source:` and image or licence file in
`npad.iss`), not just `SourceFile=`.

**Inno keeps uninstall-log records across upgrades.** Removing `Flags:
uninsdeletevalue` from an entry looks as if it stops uninstall deleting the
value. It does not on any machine that was ever upgraded: the old install's
record is still in `unins000.dat` and still runs. → To change what uninstall
does to a shared value, snapshot it in `CurUninstallStepChanged(usUninstall)`,
before the log runs, and repair it at `usPostUninstall`.

**An uninstall flag on a shared value is not a clean-up.** On
`Software\Classes\.txt`'s default value it looks like tidying up after
yourself; machine-wide it deleted Windows' own `txtfile` / `inifile` /
`xmlfile` (or another app's ProgID), taking the type's name, icon and New >
Text Document with it, with no way back. → Claim a shared value only when it is
empty or already yours, and on uninstall put back what was there.

**`MSIINSTALLPERUSER = 1` does not mean "this is a per-user install".**
`Scope="perUserOrMachine"` makes the package set `ALLUSERS=2` and
`MSIINSTALLPERUSER=1`, and passing `ALLUSERS=1` on the command line does not
clear the latter, so a machine-wide install got per-user PATH and no fonts. →
Test the resolved scope: `ALLUSERS = 1 OR (ALLUSERS = 2 AND NOT
MSIINSTALLPERUSER = 1)` (one preprocessor expression, `MachineScope`, in
`npad.wxs`).

**A hard-coded Inno Setup version on a CI image is whatever the image
carries.** `windows-latest` (the windows-2025 image, October 2026) had Inno
Setup 6.7.1 preinstalled, so `choco install innosetup` was a no-op. → `choco
upgrade <pkg> --version X --allow-downgrade` forces an exact version. Inno's
uninstall key `Inno Setup *_is1` (under WOW6432Node) gives `InstallLocation`
and `DisplayVersion`, so ISCC can be found without assuming the `Inno Setup 6`
folder name.

## Windows on Arm (arm64)

**MinGW GCC has no arm64 Windows target; llvm-mingw (clang + lld) does - and
its dlltool has no `-y`.** The x64 build delay-loads winhttp, bcrypt, comdlg32
and msimg32 through GNU `dlltool -y` delay-import libraries plus
`-ldelayimp`. `llvm-dlltool` rejects `-y` outright. lld delay-loads natively
instead: `-Wl,--delayload=<dll>` against an ordinary import library (built
from the same `.def` with `llvm-dlltool -l`, so the `.def` files still declare
every entry point used), with `__delayLoadHelper2` taken from libmingwex -
`-ldelayimp` is not needed there. The Makefile switches on `WIN_LINKER=lld`.

**The two linkers leave different evidence.** lld fills the PE delay-import
directory (`llvm-readobj --coff-imports` lists `DelayImport { Name: winhttp.dll }`);
GNU ld's `-y` stubs do not (`objdump -p` shows the Delay Import Directory as
0) and simply leave the DLL out of the import table. So "is it delay-loaded"
has a different test per linker, and `scripts/ci-check-imports.sh` runs both:
for x64, none of the four DLLs may appear in `objdump -p`'s `DLL Name:` list
while each name must still be in the file (its `dlltool -y` stub); for arm64,
each must be listed under `DelayImport`. It runs on the cross build, the
native MSYS2 build and both `installers.yml` builds, so the binary that ships
is the one checked. (Until 0.36 the register said x64 was "checked as not
bound", but no workflow checked it.)

**clang warns where GCC is silent.** The same sources under clang (llvm-mingw,
arm64) warned about a C11 typedef redefinition (`Window` in ui_win32.c and
ui_interface.h), a set-but-unused global and an unused variable that GCC
accepted. They are fixed, and CI builds arm64 with `WERROR=1`, so a clang-only
warning now fails the build (see "Compiler warnings did not fail CI" under
Tooling and process). → Build `make WERROR=1 windows-arm64` with CI's pinned
llvm-mingw (`scripts/install-llvm-mingw.sh`) before pushing Windows changes.

**llvm-mingw ships `x86_64-w64-mingw32-gcc`, `-dlltool` and `-windres` too,
not just aarch64 tools.** With its `bin/` put first on PATH it silently
replaces MinGW GCC for the x64 build as well, which then failed on
llvm-dlltool rejecting `-y`. The Makefile now picks the delay-load route from
what `$(MINGW_CC) --version` reports (clang means lld; `WIN_LINKER` still
overrides it). → Even so, use `make windows-arm64 LLVM_MINGW=<dir>` or append
llvm-mingw to PATH, so the local x64 build stays the GCC one that ships.

**An arm64 exe cannot be tested by running it** on the x64 runners or an x64
PC, and an arm64 Inno setup refuses to start there. Check the file instead:
the PE machine field (0xAA64; `validate-release-assets.sh` reads it with
`od`), the import tables, and the MSI's summary Template (`Arm64;0`).

## Tooling and process

**A source-reading guard can match its own documentation.** Release
validation reads the updater's asset suffixes as every quoted string starting
`-setup-win-` or (since the updater installs MSIs) `-msi-win-` in
`update_check.c`; a comment there quoting the pattern became a "suffix" that
no asset ends with, and failed the dry run. Keep strings of either shape out
of comments in the file a guard parses, and say so beside them.

**`workflow_dispatch` only appears for a workflow whose default-branch copy
declares it.** A dry-run trigger added on a feature branch cannot be run until
the file reaches `main`; until then use workflows that already have one there
(`installers.yml`, `packages.yml`) with `--ref <branch>` - the run then uses
the branch's copy of the file.

**CI's cppcheck (2.13) is stricter than a typical distro's (2.10).** Worse, an
inline suppression that 2.13 needs becomes an *unmatched suppression* error on
2.10 — the two can be mutually exclusive. → Prefer restructuring the code over
suppressing. Build 2.13 locally and point `CPPCHECK` at it (see the Makefile).

**cppcheck checks every `#ifdef` configuration.** A variable that only one
platform's branch can change (`posix_fallocate` under `#ifndef __APPLE__`)
becomes `knownConditionTrueFalse` in the other configuration, and a label
reached only by a `goto` inside an `#if` becomes `unusedLabelConfiguration`.
→ Move the platform split into a small helper function instead.

**cppcheck runs with `--platform=win32A`, where `long` is 32 bits.** A
digit-accumulating parse guarded only by `r <= 999999999` still overflows on
`r * 10` there. → Saturate before multiplying.

**clang-format 18 can misread a `for` header as a template.** It turned `width
< count; width = width > count / 2 ? ...` into `width<count; width = width>
count`. → Move the step expression into the loop body.

**Do not put C string literals through shell heredocs.** `\0`, `\r` and `\x20`
escapes get collapsed by an extra unescaping pass — this session wrote literal
NUL bytes into a source file that way, and in the 0.36 review round a Python
patch inside a heredoc turned a C `"\n"` into a real newline in the middle of
an `snprintf` message and a comment. → Write C text with the file-editing
tools only.

**Multi-line commit messages go through `git commit -F <file>`.** A `-m` message
containing double quotes breaks shell quoting; git then parses the remainder as
pathspecs, the commit silently fails, and a tag created straight afterwards
points at the *previous* commit.

**Verify a background job has actually finished before reading its output.**
Reading an in-progress result file returned "0 findings", which was reported as
a clean review; it later completed with six, including the critical one that
took the release down.

**npad itself can be inspected the same way, through the npad you started.**
`scripts/probe-prefs.ps1` starts a fresh npad under a private profile (see "A
private npad for probing" below; `scripts/probe-common.ps1` does the
isolation), posts `WM_COMMAND` for Preferences / Find / Replace to it, walks
the property sheet with `PSM_SETCURSEL`, captures each page with
`PrintWindow`, and closes everything again: the sheet with
`PSM_PRESSBUTTON(PSBTN_CANCEL)` - 5; the old script sent 0, which is
`PSBTN_BACK` and does nothing on a sheet - and its npad with `WM_CLOSE` to the
windows its own PID owns. It never force-kills; an npad that does not exit is
left running and reported. Menu command IDs come from ui_win32.c's `ID_*`
defines: Find and Replace are 2107 and 2108, while 2003 / 2004 are Save /
Save As, which the old example passed. It still opens windows on the user's
desktop, so run it only when the user has asked for it. Traps met on the way:

- `SendMessage(BM_CLICK)` blocks for as long as the click's handler runs, so a
  click that opens a modal box hangs the probe, exactly as Find Next with no
  further match does. → `SendMessageTimeout` with `SMTO_ABORTIFHUNG` and a
  timeout (`Send-ProbeMessage`): synchronous when the handler is quick,
  bounded when it is modal; or `PostMessage` it.
- A property sheet owned by a minimized window is hidden with its owner, so
  `IsWindowVisible` on the sheet stays false. → Restore npad's main window with
  `ShowWindow(SW_SHOWNOACTIVATE)` before opening Preferences.
- PowerShell: `$pid` is a read-only automatic variable (a parameter of that
  name fails at call time), and a static method is `[W]::Name`, never
  `W::Name`. `"$r: text"` in a double-quoted string is a parse error (the
  colon makes it a scope- or drive-qualified name; write `"${r}: text"`),
  while `"${($cr.R)}"` is a variable literally named `($cr.R)` and prints
  nothing (write `"$($cr.R)"`).
- PowerShell: piping a native command into `Select-Object -First N` stops the
  pipeline early, and `$LASTEXITCODE` then reports a failure although the
  program succeeded (`wix --version` did this). → Collect the output whole
  with `@(& tool args)` and index it.

**`wsl.exe` expands `$` before your `sh -c` ever sees it.** From Git Bash,
`wsl.exe -- sh -c 'echo $HOME; $(pgrep x)'` substitutes on the way in (empty
variables, mangled loops), and Git Bash also rewrites `/mnt/d/...` arguments
into Windows paths unless `MSYS_NO_PATHCONV=1` is set. → Put anything with a
`$` in a script file and run that: `MSYS_NO_PATHCONV=1 wsl.exe -d Debian --
sh /mnt/c/.../script.sh`. MSYS path conversion also rewrites any argument
that merely looks like a path for every native Windows program, including one
beginning with `//` (a C comment passed to a native python as a search
marker, which then matched nothing). → `MSYS_NO_PATHCONV=1` and Windows-style
paths for native tools too.

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

**Other sessions share the WSL and its Broadway displays, and `pkill` is
global.** `broadwayd :5` and `:7` were already in use by parallel sessions;
starting a second one on the same number fails silently and the app connects
to the *other* session's display - the screenshot then shows someone else's
window, and keystrokes land in it. Another session's cleanup (`pkill
broadwayd`) killed the display mid-test (the page went dark and the app was
gone), and the same command from yours kills theirs. → Do not reuse a display
number from this file. Display `:N` serves port 8080+N, so pick a number whose
port is free (`ss -ltn`) right before starting. Run renamed copies of
`broadwayd` and the binary so a sibling's `pkill` cannot match them, record
your own PIDs, and stop only those, by PID - never by name. Also: the pane's
screenshot trails the last action by a frame, and the `type` action does not
reach GTK widgets (use `key`).

**Moving a Broadway dialog's `<canvas>` moves only the picture.** Clicks still
go to where GTK thinks the window is (often at negative coordinates), so a
click on a button you moved into view lands elsewhere. Press it by its
mnemonic (Alt+S for "_Skip This Version") instead. Ctrl+Page Down did not
switch GtkNotebook pages there either; open a page from code (Help > Update
Available opens Preferences on Updates).

**The Makefile's binaries depend on their sources, not on headers.** Only
`npad-linux` lists any (the two GTK headers, `ui_gtk.h` and `print_gtk.h`);
no binary depends on a core header or `src/main.h`. Editing only `src/main.h`
(say, to number a test build lower than the latest release) and running
`make linux-gtk` keeps the old binary, which still reports the old version.
-> `rm` the binary first.

**`make format-check` covers `src/` only.** Running clang-format over a test
file reflows lines that were never formatted, which then show up in the diff
as changes you did not make; format only `src/`, or only your new lines.

**A user-local Xvfb (`apt-get download` + `dpkg -x`) cannot start.** It runs
the keymap compiler from a path compiled into the binary (`/usr/bin/xkbcomp`)
and has no option to move it, so without root it dies with "Failed to activate
virtual core keyboard". Use Broadway, or install `xvfb` properly.

**A test that asserts a file is unreachable through a relative path asserts
nothing about security.** `path_traversal_protection` expected
`file_read_text("../../../etc/passwd")` to fail, while nothing in npad blocks
traversal (by design - see `is_valid_path`; npad deliberately opens `..`
paths). It passed or failed depending on how deep the checkout sat, and has
been deleted. → Test what the code is meant to do, not where the tree
happens to be.

**The unit tests no longer run from the checkout root.** Each suite runs in
its own temporary folder (`TEST_INIT`), with HOME / XDG_CONFIG_HOME (Linux) or
`NPAD_TEST_CONFIG_DIR` (Windows) pointed inside it. A test that opens a
relative path such as `tests/../x` works only if it creates that folder itself
(`open_relative_parent_path` did not, and passed only because the repo has a
`tests/` folder). `NPAD_TEST_KEEP=1` keeps a suite's folder for a look.

**A suite can pass on Linux and crash on Windows.** An uninitialised
`CRITICAL_SECTION` crashes where a zeroed pthread mutex happens to work, so any
suite that touches the settings store must call `thread_safety_init()` first
(`test_find_seed` and `test_tab_stops` did not; later `test_ui_rules` crashed
natively the same way, and `test_editor_reload` reaches the store through
`editor.c`). That includes suites that only call a core module which reads
settings (`editor.c`, `ui_rules.c`, `find_seed.c`, `tab_stops.c`,
`settings_import.c`), not just ones that call `settings_*` themselves; a
Linux-only run never shows the miss. `fopen(..., "w")` on Windows
turns every `\n` into `\r\n`, so a fixture written in text mode fails byte
counts only there: write fixtures with `"wb"`. And `tests/test_framework.h`
declares a non-static `test_file_exists()`, so a static helper of that name in
a test file fails to compile ("static declaration follows non-static
declaration").

**Windows treats an unmanifested exe named like an installer as one.** An
executable with no manifest whose name contains update, setup, install or
patch is detected as an installer and asks for elevation, so
`test_update_check.exe` was blocked by UAC. → Every test exe links an
asInvoker manifest (`tests/test.manifest` through `tests/test_manifest.rc`);
add it to any new helper exe with such a name.

**`lround` is in libm, which the Linux build does not link.** `make linux-gtk`
fails at link time with "DSO missing from command line" for a single call to
`lround`, even though the header compiles. → A one-line rounding helper, or
add `-lm` deliberately.

**A Python patch run from a heredoc can silently never match.** The Bash tool
collapses a C `"\n"` (backslash-n) in a heredoc, so a `replace()` whose search
text contained one failed its assertion and wrote nothing, and a heredoc with
apostrophes broke the shell outright. → Write patch scripts with the file tool
and run them, as with the existing heredoc advice. A Makefile line continuation
(backslash, newline) in such a patch dies the same way, even in a quoted
`<<'EOF'` heredoc: the recipe came out as one long line.

**A private npad for probing, and the traps on the way.** npad finds its
settings through `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` (before 0.36
`SHGetFolderPath(CSIDL_APPDATA)`, which since Vista is a wrapper for it), and
`APPDATA=` does not move that - but overriding `USERPROFILE` for the child
does. That keeps a probe off the real settings.json, whose hash you can
compare before and after. `scripts/probe-common.ps1` automates all of it
(`New-ProbeProfile`, `Start-ProbeProcess`), so use it rather than
hand-rolling. The traps:

- It does not work when AppData is redirected (folder redirection, a roaming
  profile on a share): `HKCU\...\Explorer\User Shell Folders\AppData` is then
  an absolute path rather than `%USERPROFILE%\AppData\Roaming`, and npad would
  still use the real settings. → Read that value unexpanded (`GetValue` with
  `DoNotExpandEnvironmentNames`) and refuse to probe if it does not start with
  `%USERPROFILE%`.
- npad creates `Platima\npad` itself, so only `AppData\Roaming` has to exist
  (since 0.36 npad asks for it with `KF_FLAG_CREATE`, which should create that
  too); `New-ProbeProfile` creates `AppData\Roaming\Platima\npad` and
  `AppData\Local` anyway. If settings still cannot be set up, npad exits with
  code 1 after writing "Failed to initialize settings" to stderr, which a
  `-mwindows` exe started from PowerShell does not show: check the exit code.
- `Start-Process -WindowStyle Minimized` is `SW_SHOWMINIMIZED`, which
  activates the window. → To start it without taking focus, use
  `CreateProcess` with `STARTUPINFO.wShowWindow = SW_SHOWMINNOACTIVE`
  (`Start-ProbeProcess -WindowStyle MinimizedNoActivate`), then restore it with
  `SW_SHOWNOACTIVATE`. Dialogs npad opens are still activated within npad; only
  Windows' foreground lock keeps them from the foreground, so that cannot be
  promised.
- `PSM_PRESSBUTTON` takes `PSBTN_OK`=3, `APPLYNOW`=4, `CANCEL`=5 (0 is BACK, 1
  is NEXT - they do nothing on a sheet). Boolean settings are stored as the
  strings `"true"`/`"false"`. Never `SendMessage` a command that can raise a
  message box (Find Next with no further match): the sender blocks forever;
  `PostMessage` it, or bound it with `SendMessageTimeout`.

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

**Compiler warnings did not fail CI, so a use-after-free merged.** GCC
reported `-Wuse-after-free` for PR #19's `editor_open_file`, the build stayed
green, and nobody read the log; #25 fixed it. CI now builds with
`make WERROR=1` (adds `-Werror`): the Windows x64/arm64, Linux, unit test and
package builds. Local builds stay lenient. GCC and clang disagree, so a clean
GCC build is not enough: clang (llvm-mingw, arm64) flagged an unused
variable, a C11-only typedef redefinition and a set-but-unused global that
GCC accepted. Check `make WERROR=1 test linux windows windows-arm64` before
pushing, and fix the cause rather than silencing the warning.

**A posted `WM_KEYDOWN` already gets its own `WM_CHAR`.** Probing the Win32 Tab
key by `PostMessage`, the keydown, a hand-posted `WM_CHAR` and the keyup gave
two tabs with the feature off and spaces *plus* a tab with it on - the swallow
flag looked broken. It was not: npad's own message loop runs `TranslateMessage`
on the posted keydown, which queues the real `WM_CHAR`, so the hand-posted one
was a second character. Post the keydown and keyup only, and the flag logic
behaves as with a real key. (A logging build, in the mirror only, showed the two
`WM_CHAR`s in a minute.) Typing keys this way never touches the user's
keyboard: `GetKeyState` still reads the real modifier state, so do not test
while holding Shift.

**GitHub's release-asset listing returns 30 items per page, with no sign that
there are more.** `GET /releases/{id}/assets` gives 30 unless asked for more
(`per_page` up to 100), with no error or truncation marker. From v0.36.0 a
release has 36+ assets, and the `CHECKSUMS.txt` built from page 1 silently lost
the three .deb files, which sort last. → `gh api --paginate`, or build
`CHECKSUMS.txt` from the files you upload and verify it against a paginated
listing (`scripts/ci-checksums.sh`, `scripts/ci-release.sh`).

**A draft release is invisible to `GET /releases/tags/{tag}`** (404), and `GET
/releases` lists it only for a token with write access, so a `contents: read`
`GITHUB_TOKEN` cannot see it. `gh release upload` / `edit` / `download` find
drafts by tag themselves; raw API callers must list releases and match
`tag_name`.

**`/releases/latest` never returns a pre-release**, so a build that only asks
it can never hear of the next rc. → Pre-release builds list
`/releases?per_page=N` and pick the newest themselves, skipping drafts (which
an authenticated token would see).

**`paths-ignore` cannot re-include anything.** With `paths-ignore: '**.md'`,
a pull request that touched only LESSONS.md or CHANGELOG.md ran no CI at all -
and neither did renaming README.md or DOCUMENTATION.md, which both installers
package. → To skip CI for Markdown but still run it for the bundled docs, use
`paths:` with `'**'`, then the `'!'` exclusions, then the files to put back,
listed after them (later patterns win).

**`chown -R` inside a container on a bind-mounted runner workspace changes
ownership on the host too.** The runner (uid 1001) then cannot write its own
workspace, and a later `git clone` there fails with "Permission denied". →
Mount the checkout read-only, copy it into the container user's home, and
bring results back through a separate mount (`ci-aur-check.sh` and
release.yml's `publish-aur` both do this).

**gcc's ASan can abort before `main()` with an endless
`AddressSanitizer:DEADLYSIGNAL` log** on kernels with high mmap randomisation:
this WSL kernel (6.18; about one run in six), and GitHub's Ubuntu images at
times. The plain, `-O0` and valgrind builds of the same test pass, so it is
the environment, not a finding. → Run ASan binaries under `setarch $(uname -m)
-R` (ASLR off for that process, no root) or set `sudo sysctl -w
vm.mmap_rnd_bits=28` (CI). `make test-sanitize` and `make test-fuzz` add
`setarch` themselves (`SANITIZE_RUN`) where `personality(2)` is permitted.

**A pip-installed clang-format breaks when HOME is redirected.** The
`pip install --user clang-format==18.1.8` route (for a distro without
clang-format 18; WSL Debian's plain `clang-format` is 14) installs a Python
wrapper that imports its package from the user site directory, so a run with
HOME pointed at an isolated test folder fails with `ModuleNotFoundError:
clang_format`, and `make format-check` fails as if the code were badly
formatted. → Run format and format-check with the real HOME (only the test
runs need the isolated HOME and XDG_CONFIG_HOME), or set `PYTHONUSERBASE` to
the real `~/.local`.

**A WSL mirror of the Windows checkout is CRLF.** The working copy has
`core.autocrlf=true`, and rsync copies it as is, so make, the shell and
clang-format do not see what CI sees. → Convert the mirror's sources and
Makefile to LF after the rsync.

**A header-existence probe in the Makefile must preprocess with `$(CC)
$(CFLAGS)`**, or headers reachable only through a CFLAGS `-I` look missing
(`make linux`'s ncurses check).

**MinGW-w64 10 (Debian 12) does not define `ERROR_ELEVATION_REQUIRED` (740)**
in winerror.h, even with `_WIN32_WINNT 0x0A00`. → Guard it with `#ifndef`.

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
