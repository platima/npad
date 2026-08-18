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

**Mnemonics collide silently.** `&Paste` and `&Preferences` in one popup meant
`Alt+E, P` pasted. Check the whole popup's set, including items appended to the
*context* menu from elsewhere.

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

## Installers

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
