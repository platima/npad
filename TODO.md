# npad TODO

Working notes for things that are parked, unverified, or waiting on a decision.
Curated release history lives in [CHANGELOG.md](CHANGELOG.md), and the
non-obvious findings worth not rediscovering live in [LESSONS.md](LESSONS.md);
this file is for open loops.

---

## 🧪 Needs a stress test

### Many instances at once - ROOT CAUSE FIXED in v0.28.7, still worth retesting

Reported 2026-08-17: opening instances with Ctrl+Shift+N until the taskbar
buttons collapsed made npad lag badly, stop responding and stop drawing, reach
**24 GB of RAM across 20 instances**, hammer the disk, and finally refuse to
stay open at all.

**Root cause found and fixed** - it was not the instance count. `settings.json`
had grown to **1.2 GB** through an escaping bug that doubled every backslash on
each save/load cycle (see LESSONS.md). Each window loads that file at startup,
so the cost was multiplied by the window count: Task Manager showed 577 MB per
idle instance and 1.7-2.5 GB for those still loading.

**Re-run on v0.28.7 and PASSED (2026-08-18).** 30 instances, no crash, all
responsive. **3.4 MB per window, 104.2 MB for 30** - linear, against 577 MB per
idle instance before the fix. That is now the documented baseline in README.md.

- [x] **Crash recovery with killed instances** - CONFIRMED 2026-08-18. Seven
      instances edited then killed; six were offered on relaunch, all restored
      correctly, and the recovery directory was emptied afterwards. No slot
      accumulation.

      The seventh is not a miss: snapshots run on a 30-second timer
      (`session_interval`), so an instance killed before its first tick has
      nothing on disk. That is the designed exposure - **up to 30 seconds of
      work, and a brand-new window is unprotected for its first 30 seconds.**
      Shortening the interval in Preferences > General trades disk writes for a
      smaller window. Snapshotting on the *first* modification instead of
      waiting for the timer would close the new-window gap specifically; not
      done, since it adds a write on every first edit and nobody has asked.

Not yet separately investigated, since the settings file explains what was
observed: whether the cross-instance settings broadcast is O(instances^2) at
this scale, and whether the session-snapshot timers add up. Measure before
assuming either is a problem.

---

## 👁️ Needs a visual inspection (v0.27.1 / v0.28.0)

Things that were verified by reading the code and by the static gate, but whose
*appearance* I cannot check - I can read control coordinates, not rendered text
metrics, and I cannot see the taskbar. Each is small enough to be a patch if it
is wrong. Nothing here is known-broken; this is the honest list of what went out
unseen.

**Highest risk first:**

- [x] **Preferences pages, every tab** - CONFIRMED GOOD 2026-08-09. v0.28.0 added `DS_FIXEDSYS` to all seven
      pages so they stop rendering in Microsoft Sans Serif inside a Segoe UI
      frame. Dialog units are font-relative and the two faces do not share a
      DLU-to-pixel ratio, so **tight labels may now clip or wrap**. Worst
      candidates: the four Markdown bullet descriptions, the Backup and Reset
      group captions, and the long checkbox labels on General and Appearance.
- [ ] **Appearance page: the new "Application icon" radio group.** Five radios
      added at y=164-226 on a page grown from 196 to 236 DLU. The property sheet
      already sizes to the taller Markdown page, so it should fit without
      resizing the sheet - confirm nothing is cut off at the bottom.
- [x] **The icon on the taskbar** - CONFIRMED GOOD 2026-08-09 ("dark taskbar
      icon looks good"). The by-name mapping stands.
- [x] **The CAPTION icon** - FIXED in v0.28.4. The two are separate slots
      (ICON_SMALL drives the caption, ICON_BIG the taskbar), established by
      probing a scratch window with different artwork in each. Automatic mode
      now matches each surface to the theme it sits on.
- [x] **Grouped taskbar button** - CONFIRMED 2026-08-18. Collapsing several
      windows into one taskbar button shows the **light** icon, because Windows
      draws a grouped button from the *executable's* icon (resource index 0 =
      IDI_NPAD) rather than the window's. Inherent and unfixable - the exe's
      icon is fixed at build time - so "always dark" and "classic" cannot reach
      that surface either. Documented in DOCUMENTATION.md.
- [ ] **Dark scroll bars actually render dark** (needs Windows 10 1809+). If
      they stay light, `SetPreferredAppMode` (uxtheme ordinal 135) did not
      resolve on that build - npad degrades silently by design.
- [~] ~~Dark status bar text~~ - MOOT. The dark status bar was reverted in
      v0.28.6; it is system-themed in every scheme again.
- [ ] **Light schemes unchanged.** The whole round is supposed to leave the
      default appearance untouched. Scroll bars, status bar, dialogs.

**Quick confirmations:**

- [x] **Text gutter width** - MEASURED 2026-08-09. 4 DIP was too tight; 9 DIP
      overshot at 16px in use. Settled on **6 DIP** in v0.28.4.


- [ ] `Alt+E, S` opens Preferences, and `Alt+E, P` pastes (they collided before).
- [ ] Status bar at a very narrow window width and at 150%/200% DPI - parts
      should collapse in order, never draw inverted (v0.27.1 clamp).
- [ ] Dragging the window larger in a dark scheme shows no white band (v0.27.1).
- [ ] The menu bar does not flash when settings change in another instance
      (v0.27.1).
- [ ] Explorer still shows a sensible icon for associated file types - the icon
      resource ids changed, and the installers refer to `npad.exe,0`.

---

## 🐛 Confirmed — FIXED in v0.27.1

All items in this section shipped in v0.27.1 (find status residue,
IsDialogMessageW gating, the g_hl_matches leak, Paste-as-Markdown greying,
Interpret-escapes staleness, the SB_SETPARTS clamp, the double menu redraw,
the dark-mode resize band, and Convert Delimiters accepting CRLF). Kept for
the root-cause notes; the remaining Convert Delimiters items (persistence and
the swap button) are features and are still open below.

### Find match info stays in the status bar after the dialog closes

Reported 2026-08-01: "when the find dialog was closed, it still has the match
info in the status bar until I alter the text."

**Root cause confirmed by reading.** `find_next` puts "Match 3 of 7" in status
part 0 via `set_status_message`. The Find/Replace dialog's `WM_DESTROY`
(`ui_win32.c:3253`) kills its timer, calls `refresh_highlights(window, false)`
to clear the highlights, and saves the dialog position — but never clears the
status message. It therefore survives until the next text change re-runs the
counts.

Fix site is that same `WM_DESTROY`: restore part 0 to whatever it should show
(the counts if `status_show_counts` is on, otherwise empty). Note the highlights
*are* cleared correctly there, so this is the one straggler, not a pattern.

Safe to fix independently of the scroll investigation below.

Sharper than the report: part 0 is otherwise written only by
`update_text_counts`, which early-returns when `status_show_counts` is off (the
default). So in the **default** configuration the message persists *forever*,
not "until I alter the text" — that it clears on edit means live counts are on.

### Two further defects found while investigating the above

- **`IsDialogMessageW` is called for every message** in the loop while the
  modeless Find dialog exists (`ui_win32.c:949`), with no check that the message
  belongs to that dialog. MSDN explicitly warns against this; it can divert Tab
  / Enter / Esc / arrow keys typed in the **editor**. Worth gating on
  `msg.hwnd == g_find_dialog || IsChild(g_find_dialog, msg.hwnd)` — and worth
  doing *before* any keyboard-driven attempt to reproduce the scrolling, since
  it could confound the results.
- **`g_hl_matches` is never freed.** Grown by `realloc` (`ui_win32.c:2697`),
  with no `free` anywhere including `ui_platform_cleanup`. Retained for process
  life — hygiene only, no performance impact.

### Product question: wrap-around search defaults ON — DECIDED, shipped v0.26.0

`find_wrap_around` defaults to **true** (`ui_win32.c:905`). Windows 10 Notepad
ships that box **unchecked**. It alters search results rather than adding to
them — a search that Notepad reports as "cannot find" silently succeeds here —
so by the project's own default-setting rule it arguably belongs OFF.

**Decided 2026-08-01: default OFF, with a Preferences > General checkbox** so it
is one tick to turn back on. Existing settings are untouched — only fresh
installs see the new default.

Adversarial review of that change found two real defects, both fixed in the same
round: the Preferences checkbox was seeded from the persisted key rather than the
live `g_wrap_around` (which the Find dialog mutates without persisting, so the
page could contradict observed behaviour and then write the stale value back),
and `reload_and_apply_settings` never re-read any `find_*` global, so promoting
the option to a documented, propagated preference gave it propagation that did
not actually work.

---

## 🔍 Needs your input

### Slow scrolling — new evidence 2026-08-01, mechanism NOT yet established

Three observations from real use, the third being the one that matters:

1. A 227-line document, searching for a term on line 131: "the first time it
   took ages to scroll down, and then after closing the dialog it scrolled all
   the way back up to where I was, but also slowly." **Not reproducible a
   second time.**
2. (Separate, confirmed, see above: the match text stays in the status bar.)
3. "After 10 minutes I went back to the document — I had previously select-all,
   copy, alt tab to another window — and it was just very slowly scrolling back
   to the top. Even whilst I was clicking around in the editor, it was still
   scrolling up."

**Why (3) is the important one:** the view moved *continuously*, for minutes,
with no user input, and kept going while the user clicked in the editor. That is
not "slow scrolling" — something was actively scrolling.

**Established so far by reading the source:**
- Every `EM_SCROLLCARET` call site is reached only from a discrete user action
  (`handle_markdown_return`, `ui_platform_set_cursor_position`,
  `paste_insert_converted`, `do_paste`, `find_next`, `show_goto_dialog`,
  `list_replace`). **None is in a timer or paint path**, so npad is not
  scrolling on a schedule.
- `refresh_font_binding` (`ui_win32.c:3681`) is the one path that deliberately
  saves and restores the scroll position — around a **full document replacement**
  via `EM_SETTEXTEX`. It early-returns unless the document contains astral
  (non-BMP) characters, and is reached from `apply_theme` and `apply_font`.
  Whether anything can reach it repeatedly is not yet established.
- `IMF_AUTOFONT` is enabled (`ui_win32.c:1044`) — flagged in the v0.21.0 round
  as costly on large documents, but 227 lines is not large.

**Do not assume this is the same bug as the vanished scroll bar below.** It may
be unrelated, and it may not be npad's bug at all — 227 lines is far too small
for any of npad's known per-paint costs to be visible, which is itself a strong
hint.

#### Investigated 2026-08-01 — no mechanism exists in npad for (3)

A four-angle investigation enumerated the complete set of view-moving
primitives in `src/`: **7 × `EM_SCROLLCARET`** and **1 × `EM_SETSCROLLPOS`**
(the latter inside `refresh_font_binding`, astral-gated, and it *restores*
rather than moves). Zero hits for `WM_VSCROLL`, `EM_LINESCROLL`,
`SetScrollPos`, `SetScrollInfo`, `ScrollWindow`, `SetCapture`, or
`SetWindowsHookEx`. All eight `SetTimer` sites were checked; none is
scroll-adjacent and none re-arms itself. There is no self-posting message loop.
**npad cannot have produced that motion.**

**Two attractive theories were raised and both were killed:**

- *Stuck RichEdit OLE drag-scroll.* Dead on a false premise: npad calls
  `CoInitializeEx` and **never `OleInitialize`** (verified — no occurrence in
  `src/`). `RegisterDragDrop` and `DoDragDrop` require full OLE
  initialisation, so RichEdit's drag-drop is almost certainly inert in this
  build and `ES_NOOLEDRAGDROP` would be a no-op. **Do not add it as a
  speculative fix.** (Falsifiable in 30 seconds: select text, press inside the
  selection and drag. No drop caret ⇒ confirmed dead.)
- *Any stuck-capture / modal-loop mechanism* (drag autoscroll, scrollbar
  auto-repeat). The user's own most vivid detail rules these **out**, not in:
  "still scrolling while I clicked around" is evidence *against* them, because
  both `DoDragDrop` and USER32's scrollbar tracking terminate on button-up and
  a fresh physical click delivers exactly that. `edit_subclass_proc` provably
  does not eat the button-up (it forwards everything to `DefSubclassProc`,
  fully consuming only `WM_MOUSEACTIVATE` and some Tab/Enter keys).

#### FALSIFIED 2026-08-01 by user evidence — it IS npad

The conclusion below (externally generated input) is **wrong** and is kept only
so it is not re-derived. The user reports the fault has occurred repeatedly
across **v0.16 to v0.25**, roughly every second day for a week, on **two
different machines**, on a **raw console** (no RDP), with a **default mouse** —
and it happens in **no other application** and **never happened in classic
Notepad**. A mouse, driver or Windows-hover cause would not spare every other
application on two machines.

Notepad uses an `EDIT` control; npad uses **RichEdit**. That is the difference,
so the motion originates inside RichEdit or in how npad drives it. The
enumeration below stands — npad issues no scroll of its own — but "therefore
external" was the wrong inference from it.

Also eliminated for free: v0.16 predates `ES_DISABLENOSCROLL` (v0.20.0), so the
always-visible scroll bar is not the cause. The document was pure ASCII pasted
from a serial console, so `refresh_font_binding` early-returns and the sole
`EM_SETSCROLLPOS` is provably inert for this case too.

**v0.26.0 ships passive telemetry** (Preferences ▸ Debug, behind
`#define NPAD_SCROLL_TELEMETRY`) to catch it in the act: input counters plus,
per repaint, whether the view moved and whether anything could account for it.
Expect it to show unexplained moves climbing while wheel and `WM_VSCROLL` stay
flat — which would confirm RichEdit is moving the view itself. If instead the
input counters climb, the external theory returns.

<details>
<summary>Superseded conclusion (kept for reference)</summary>

**What survives is a category, not a diagnosis:** a continuously *generated*
stream of scroll input originating outside npad. A finite queued backlog is
ruled out on throughput grounds, but a live stream needs no backlog, is not
cancelled by clicking (the clicks simply queue behind it), and is the only
candidate consistent with surviving ten minutes and an alt-tab. Plausible
sources — none proven, none tied to npad: a failing or stuck wheel encoder,
a mouse-driver feature (free-spin flywheel, thumb-button autoscroll), Windows'
**"Scroll inactive windows when I hover over them"** (default ON) with the
pointer parked over npad, or an RDP/VDI client replaying buffered input on
reconnect.

**Caveat on the evidence:** the single most discriminating detail is one
person's recollection of an unreproduced event, recalled ten minutes later. If
"still scrolling while I clicked" is imprecise, the stuck-capture family comes
straight back.

</details>

**Next diagnostics, falsification first:**
1. *Free.* Next occurrence: **does Explorer / notepad.exe / a browser scroll
   too?** A yes ends this investigation — it is not npad.
2. *Free.* Press **Esc** (cancels both a drag and scrollbar tracking), then
   single-click in the text and see whether the caret actually moves.
3. *Free.* Environment: RDP/Citrix/VDI? Which mouse and driver software? Is
   "scroll inactive windows on hover" enabled?
4. *Free.* Did the document contain **astral characters** (emoji)? If it is
   pure ASCII, `refresh_font_binding` early-returns and the only
   `EM_SETSCROLLPOS` in the codebase is provably inert.
5. *Cheap, and the right answer for an intermittent bug.* Add passive scroll
   telemetry to the existing Preferences ▸ Debug page (which already carries
   paint counters): count `WM_MOUSEWHEEL` / `WM_VSCROLL` / button and
   `WM_CAPTURECHANGED` in `edit_subclass_proc`, and sample `EM_GETSCROLLPOS`,
   `GetCapture()` and `GetKeyState(VK_LBUTTON)` on a debug-only timer, plus a
   counter of "scroll moved with zero input messages since last sample". That
   one instrument discriminates every remaining candidate: wheel counts
   climbing ⇒ injected input; non-null capture ⇒ stuck drag; movement with
   neither ⇒ RichEdit-internal. It is passive, so it catches the event whenever
   it next happens.

**For (1) — partly explained, the "slowly" is not.** Wrap-around search
(`g_wrap_around`, default **true** at `ui_win32.c:905`) silently wraps to the
first match and `EM_SCROLLCARET`s there, which explains a *jump* to the top;
combined with the stale status message above, the "Wrapped around — Match 1 of
N" text was still on screen after the dialog closed, which is very likely why it
felt like the jump happened at close time. That explains a jump, **not a slow
crawl**. On 227 lines the worst paint configuration is roughly 12 GDI calls +
21 `SendMessage` + 3 `AlphaBlend` — microseconds, against a measured baseline of
26 coalesced paints at 0.6 ms on a 20,000-line document. Nothing is within 100×
of the ~50 ms needed to be visible. The one non-size-invariant term is the
`AlphaBlend` at `ui_win32.c:2724`, a **stretch blit from a 1×1 DIB**, which
degrades badly over RDP/Citrix and applies only while Highlight All is on.

### Scroll bar vanished while the view stayed scrolled — NOT REPRODUCED

**Reported (v0.19.0, 2026-07-26):** "Came back to half a page of notes, and it
was scrolled down slightly (even though the text did not exceed the window
size) and there was no scroll bar. Had to arrow the cursor up to the top line
to bring it back into view."

**Status:** could not be reproduced, and the leading hypothesis was *disproved*
by measurement. v0.20.0 shipped hardening + an always-visible scroll bar as
Notepad fidelity — **not** a confirmed fix.

What was tested, all behaving correctly (RichEdit either resets the scroll
offset or leaves the bar usable):

| Scenario | Result |
|---|---|
| Text shrinks while scrolled | resets to top ✓ |
| Window grows so content fits | resets to top ✓ |
| Zoom out until content fits | resets to top ✓ |
| Word-wrap toggle while scrolled | bar stays active ✓ |
| Emoji doc + font rebind (the one path npad forces a scroll position) | resets to top ✓ |
| Small window, scroll, then grow so it fits | resets to top ✓ |

Also disproved: the theory that `apply_word_wrap` round-tripping the whole
style word permanently strips `WS_VSCROLL`. Measured — RichEdit adds and
removes that bit dynamically and recovers every time.

**If it happens again, the context is what's needed:**
- Was the document restored by session/crash recovery, or opened normally?
- Was a second npad window open at the time (settings broadcast / view sync)?
- Did the text contain emoji or other astral characters?
- Was the zoom level non-100%, or word wrap toggled recently?
- Had the window been resized or maximised/restored since the file was opened?
- Roughly how many lines, and was the window unusually short?

---

## 🧪 Needs a real-world test

### Click-through activation (v0.20.0)

**Reported:** clicking (or click-dragging to select) in an unfocused npad
window does nothing but focus it — you have to click twice.

**Fixed in v0.20.0** by claiming `WM_MOUSEACTIVATE` in the edit-control
subclass: it takes focus and returns `MA_ACTIVATE` so the click still reaches
the text. It is handled in the *subclass* rather than the frame because the
message is delivered to the child first and only reaches the frame if the
control forwards it.

**Unverified automatically** — synthetic mouse/keyboard injection does not work
in the test environment (the control case, clicking an *already-focused*
window, failed too, which proves the harness rather than the code was at
fault).

**To test:** with npad open but not focused, click once in the middle of a line
of text.
- ✅ Expected: the caret lands where you clicked; a click-drag selects.
- ❌ Bug still present: the first click only focuses the window.

---

## 📋 Parked

### v0.21.0 — performance round — MEASURED (2026-07-28)

**Neither slowdown reproduced on the test machine.** What was measured, and
what shipped in v0.21.0, is below. The original analysis follows for reference.

| Measurement | Result |
|---|---|
| Warm start (process creation → window responsive), 12 runs | **41–59 ms**, median 43 ms |
| First run of a never-before-seen copy (Defender scan path) | **43 ms median** — no different |
| Pre-`main` cost (loader, CRT, manifest), now instrumented | **~12 ms** |
| 300-notch scroll burst, 1.6 MB / 20k lines | drains in **~0 ms**, 26 coalesced paints @ 0.6 ms |
| 300-notch burst, 5.6 MB of very long lines, wrap ON | **≤5 ms** |
| Same, plus live counts and Highlight All | **≤5 ms** |

Conclusions:
- The **"~150 ms" reading was almost certainly the "deferred tasks" line**,
  which is dominated by a deliberate 50 ms timer and is not startup work. It is
  now labelled as such.
- Startup could not be made meaningfully faster because it is already ~40 ms
  warm; the win available was for the **cold** case, so the binary was made
  **26% smaller** and two pieces of per-launch dead work were removed.
- Scrolling could not be made faster because nothing measured slow. **No
  speculative rewrites were done** — the counts full-document fetch, the
  overlay's linear match walk and `IMF_AUTOFONT` were all left alone, since
  changing code that measures fast is churn, not optimisation.

### Slow launch — ROOT CAUSE FOUND (2026-07-28)

**It happens before the process exists, so neither npad's profiler nor my first
benchmark could see it.**

A v0.21.0 profile from a slow (1–2 second) launch on a work machine accounted
for only **116 ms** of it:

```
   0.0 ms  process created
  36.1 ms  main enter        <- loader / CRT / manifest
  51.4 ms  settings loaded
 107.2 ms  window created
 116.3 ms  message loop      <- npad is up; ~1 second of the wait is unexplained
```

npad's profile starts at the process-creation timestamp, so everything the
system does *first* is invisible to it: the shell resolving the launch, a
**SmartScreen reputation check** (which can make a network call for an unsigned
binary), and **anti-malware scanning the image** as its section is created.

`scripts/measure-startup.ps1` brackets the whole thing and splits it. It
reproduces the problem immediately — first (cold) run on a developer machine:

```
 run    total ms    pre-create    in-process
   1       493.4         439.8          53.6     <- cold
   2        47.4           6.5          40.9
   3        51.7           4.1          47.6
```

**89% of the slow launch is before the process object exists.** On a corporate
machine with enterprise AV, SmartScreen and a possibly network-redirected
profile, the same effect stretching to 1–2 seconds is entirely plausible.

*Correction to the earlier analysis below:* the first benchmark timed from
`Process.StartTime`, which excludes the pre-creation window — the same blind
spot as npad's own profiler. That is why it reported a flat ~43 ms and
concluded, wrongly, that the slowdown was not reproducible.

**What actually helps**
1. **Code signing** — the real fix, and the only one that removes the
   SmartScreen reputation check. Already on the README roadmap.
2. Smaller image → less to scan. Done in v0.21.0 (**26% smaller**).
3. Fewer DLLs to map and scan. Done in v0.22.0 (**12 → 8** static imports).

Items 2 and 3 were shipped as speculative hardening; this measurement shows
they target the right thing after all. Neither eliminates it — signing does.

**Left in npad's own startup**, if it ever becomes worth chasing: `settings
loaded` took 15.4 ms for 35 entries on the work machine versus 1.0 ms locally.
(Not folder redirection — that is disabled on that machine. More likely
on-access scanning of the read, or simply a slower disk; the whole machine
profiled ~3× slower than the dev box across every phase.) Small in absolute
terms either way.

**Done in v0.22.0 — delay-loading.** `winhttp`, `bcrypt`, `comdlg32` and
`msimg32` are no longer bound at load time: **12 → 8 statically imported DLLs**.
GNU `ld` has no `--delay-load`, but `dlltool --output-delaylib` builds a proper
delay-import library, and `libdelayimp` supplies the resolver — so this needed
no source changes, just a `.def` per library in `src/platform/delay/` (which
doubles as documentation of the API surface npad uses). Verified at runtime:
each DLL is absent from the process at launch, appears on first use of its
feature, and the feature works. `ole32` was left statically bound because
`CoInitializeEx` runs during startup anyway.

<details>
<summary>Original pre-measurement analysis (kept for reference)</summary>

**Startup latency** — occasionally ~0.5s to appear, while the Debug page's
profile only accounts for ~150ms.
- *Why the profile misses it:* `startup_prof_ms()` reports times relative to
  mark 0 (`"main enter"`), so everything before that — the PE loader, CRT init,
  side-by-side manifest activation — is reported as 0.0ms by construction.
- *First task is instrumentation, not optimisation:* measure from real process
  creation (`GetProcessTimes`) so the pre-`main` cost becomes visible. Optimise
  only what that shows.
- *Candidates already identified:*
  - **12 DLLs are statically imported** and resolved by the loader before
    `main()` runs. Several are rarely used on a plain launch: `winhttp` and
    `bcrypt` (update check only), `comdlg32` (file dialogs), `ole32` (COM +
    save dialog), `msimg32` (just `AlphaBlend` for the highlight overlay).
    There is no delay-import directory — delay-loading these is the obvious
    lever.
  - `shcore.dll` is `LoadLibrary`'d and immediately freed on **every** launch
    (`src/main.c:57`) for a DPI fallback path that never executes on Windows 10+
    — and the manifest already sets PerMonitorV2, making the whole block
    redundant.
  - `dwmapi.dll` is loaded and freed on every `apply_theme()` call.
  - The 228KB `.rsrc` section is over half the 424KB image.

**Scroll lag on large files** — UI lags when scrolling fast through a big
document.
- *Candidates already identified:*
  - `update_text_counts` fetches the **entire document** and converts
    UTF-16→UTF-8 (two full-size allocations) up to ~8×/second while typing.
    Only documents over ~1,000,000 chars fall back to a slower debounce.
  - `schedule_counts_update` calls `GetWindowTextLengthW` on *every* invocation
    just to decide which strategy to use.
  - `draw_highlight_overlay` walks **all** matches from index 0 on every paint
    (skipping non-visible ones one by one) and issues ~6 synchronous RichEdit
    messages per visible segment.
  - `IMF_AUTOFONT` is enabled on the control, which is known to be costly on
    large documents.
- *Note:* the Debug page's paint timer measures only the default paint and
  **excludes** the highlight overlay, so it currently under-reports.
  *(Fixed in v0.21.0 — timings now include the overlay and report a max.)*

</details>

### Unsaved work must survive an update or a Windows restart — SHIPPED in v0.25.0

Requested 2026-07-29, built the same day after the user hit it live updating
0.24.0 → 0.24.1. npad is used primarily as a scratchpad, so being asked to save
and reopen every document on each update was the main day-to-day friction.

**What was actually reported**, after a correction from the user: starting an
update from an instance with an unsaved document made *that* instance prompt to
save as it closed, and the other npad windows stayed open. Inno was not running
at that point, so this is entirely npad's own updater path — it posted
`UI_EVENT_QUIT` to its own window, which runs the ordinary save-prompting close,
and nothing ever told the other processes to close at all.

*(An earlier version of this note claimed both symptoms shared one root cause in
Inno's Restart Manager being refused. That was wrong — a tidy story invented
around `CloseApplications=yes` being set, not something the report supported.
The Restart Manager is a separate, later line of defence; the handoff path is
what makes it work too, but it is not what the user hit.)*

The underlying gap is still real and is what the fix addresses: npad had no
close path that did not prompt, so no externally-driven close could succeed
quietly.

**What shipped:** a handoff concept. `<slot>.handoff` marks a recovery slot as
parked-by-something-else, so the next launch restores it silently while genuine
crashes keep the "closed unexpectedly" prompt. `WM_QUERYENDSESSION` /
`WM_ENDSESSION`, a `g_session_ending` latch that suppresses the prompt on
externally-driven closes, an `npadCloseForHandoff` broadcast plus a bounded
wait in the updater, and `RegisterApplicationRestart` pointing each process at
its own slot.

**Correction to the note that stood here before:** it claimed an unsaved
document "would make Windows report npad as preventing shutdown". That was
wrong — with no `WM_QUERYENDSESSION` handler, `DefWindowProcW` returned TRUE,
so npad never blocked a restart. It was simply terminated, losing up to 30
seconds of typing and its window position, and came back under the crash
prompt. The fix is still right; the stated symptom was not.

**Still needs driven runtime testing** (see "Needs a real-world test" above):
multi-window park/restore, a genuine Restart Manager close driven by the real
installer, and whether `RegisterApplicationRestart` actually fires on this
machine's Windows configuration.

<details>
<summary>Original design notes (kept for reference)</summary>

**Decided behaviour:**
- When something *else* closes npad — an in-app update, or Windows Update
  restarting the machine — unsaved documents are snapshotted and reopened
  automatically, still unsaved, with no prompt in either direction. This is what
  Windows 11 Notepad does. It never writes to the user's actual files.
- A **normal** quit (X, Ctrl+W, Exit) still asks Save / Don't Save / Cancel,
  exactly as classic Notepad. Only closures npad did not initiate are silent.

**Groundwork that already exists:** `src/core/session.c` already snapshots
unsaved work to recovery slots on a timer and restores after an unclean exit
(`session_resume_enabled`, on by default). The work is mostly about *when* to
snapshot and *how* to restore without prompting, not new storage.

**Sketch:**
1. Snapshot-and-exit path used by the updater instead of
   `editor_prompt_save_changes`, plus a marker so the next launch restores
   silently rather than showing the crash-recovery prompt.
2. `RegisterApplicationRestart()` so Windows Update relaunches npad after a
   reboot, with the same marker in the restart command line.
3. `WM_QUERYENDSESSION` / `WM_ENDSESSION`: snapshot and return TRUE promptly
   rather than showing a save prompt, so npad never blocks a restart. Today an
   unsaved document would make Windows report npad as preventing shutdown.
4. Restore every window, not just the active one — the existing multi-window
   session restore already does this.

**Needs driven runtime testing** (multi-window snapshot/restore, and a real
`WM_QUERYENDSESSION`), so it is queued for when the machine is free.

</details>

### Binary files load truncated at the first NUL byte — ROOT CAUSE CONFIRMED

Reported 2026-07-31: opening a ~200 KB PNG warned it was binary (correct),
opened it anyway (correct), then showed **2 words, 5 chars, 3 lines**.

**Reproduced exactly**, with a 120,911-byte PNG through the real `file_ops`
functions:

```
bytes on disk      : 120911
string kept        : 9 bytes (stops at first NUL)
detected encoding  : 4  (NPAD_ENC_ANSI)
counts             : 2 words, 5 chars, 3 lines
```

**Cause:** npad's core passes text as **NUL-terminated `char *`**
(`file_read_text_ex` returns one; `file_count_text_stats` walks `for (p = utf8;
*p; p++)`). A PNG's first NUL is at offset 8, immediately after the 8-byte
signature `89 50 4E 47 0D 0A 1A 0A`, so everything past it is discarded. The
document really is 9 bytes — the counts are honest, the *load* is what is
broken.

The arithmetic confirms it precisely: the file is detected as ANSI (not valid
UTF-8), so CP1252 `0x89` becomes U+2030 (3 bytes in UTF-8), CRLF normalises to
LF, leaving 9 bytes; counting those gives 1 + P + N + G + 0x1A = 5 chars, two
runs = 2 words, two newlines = 3 lines.

**⚠ The dangerous part is not the counts — it is saving.** The buffer holds 9
bytes but `current_file` still points at the PNG. Type one character and press
Ctrl+S and npad writes ~10 bytes over the original 120 KB file. Atomic saves do
not help: the atomic write faithfully commits the truncated buffer. This is
reachable by anyone who opens a binary file to peek at it.

**Options, in increasing cost:**
1. **Sanitise on load** (cheapest, and closest to Notepad). When the binary path
   is taken, replace NUL bytes with a visible placeholder (U+2400 SYMBOL FOR
   NULL, or U+FFFD) so the whole file loads and round-trips visibly. Contained
   to the read path. Note this makes the buffer lossy, so it does not remove
   the save hazard by itself.
2. **Mark binary-opened documents read-only** (or force Save As), so a peek can
   never overwrite the original. Pairs well with 1 and directly addresses the
   data-loss path.
3. **Length-carrying buffers throughout** — the only way to genuinely support
   embedded NULs, and a large change: editor, UI, session, search and the
   RichEdit boundary all assume NUL-terminated strings. Windows Notepad does
   this, which is why it can display binary intact.

Recommendation when this is picked up: **2 first** (it removes the data-loss
risk on its own), then 1. Defer 3 unless binary editing becomes a real goal.

### Convert Delimiters: no memory, no swap button, and `

` can never match

Reported 2026-08-06. Three things, two of them confirmed by reading.

**1. It forgets what you last used.** Confirmed: `WM_INITDIALOG`
(`ui_win32.c:5640-5641`) hard-codes `SetDlgItemTextW(dlg, ID_DELIM_FROM, L",")`
and `ID_DELIM_TO, L"\r\n"` on *every* open. Nothing is persisted — no
`history_push`, no settings write — unlike Find/Replace, which already has
`history_load` / `history_push` (`find_hist` / `replace_hist`). Reuse that
machinery rather than inventing another: `delim_from_hist` / `delim_to_hist`
would also populate the dropdowns with recent values for free.

**2. Wanted: a swap button** between From and To, so a conversion can be
reversed without retyping. Cheap; the combo texts just trade places.

**3. `

` as the *source* can never match — root cause confirmed.**
Reported as "a new file that said it was Windows CRLF, going from `

` did
not work, I had to go from just `
`", suspected to be the pasted text. **It is
not the pasted text.** It is deterministic:

- `list_extract` (`ui_win32.c:5421`) fetches the range and then calls
  `normalize_to_lf(utf8)` (`:5439`), so the text the conversion actually matches
  against contains **only bare `
`**, whatever the file's line ending is.
- `list_replace` (`:5444`) mirrors it: normalise to LF, then emit CRLF uniformly
  into the control. So the internal round-trip is consistent.
- The status bar correctly reports the *file* as Windows (CRLF), because that is
  its on-disk attribute — but the buffer the tool sees is LF-only. So a `

`
  source matches nothing, always, in every file.

**The trap is self-inflicted:** the dialog's default *target* is `

`
(`:5641`, commented "OS default line ending"), which works fine because
`list_replace` normalises it. So npad itself teaches the user that `

` is a
valid token, and it then silently fails the moment they use it as a source.

Options when fixing: accept `

` in the source and treat it as `
`
(smallest, matches what the user meant); or drop `

` from the presets and
the default target so it is never suggested; or state the LF-only rule in the
dialog's escape hint (`ID_DELIM_HINT`). The first is the least surprising.
Whichever, the "from" and "to" presets should stop disagreeing about whether
`

` is meaningful.

### Detect that the open file changed on disk

Requested 2026-08-04. When the file npad has open is modified by something else,
offer **Reload / Save As / Continue**, with an optional "don't ask again for this
file" checkbox scoped to the session. **Off by default**, enabled from a
preferences pane — consistent with the core principle, since Windows 10
notepad.exe does not do this at all.

**Nothing for this exists yet** (checked): npad tracks *nothing* about the file
on disk — `file_ops` exposes only `file_exists` and `file_get_size`, there is no
`FILETIME` / mtime handling anywhere in `src/core`, and there is **no
`WM_ACTIVATE` handler at all** in `ui_win32.c`. Both the stamp and the hook are
new.

**Design constraints worth not rediscovering:**

- **Detect on window activation, not on a timer.** It is what other editors do,
  it is the moment the user would notice anyway, and it costs exactly nothing
  while npad is unfocused — which matters given the standing preference for
  keeping idle cost near zero. Also check immediately *before* a save, which is
  the genuinely dangerous moment (silently overwriting someone else's edit).
  `ReadDirectoryChangesW` is the event-driven alternative but needs a directory
  handle per open document, which then blocks renaming or deleting that
  directory — not worth it here.
- **npad must not trip over its own writes.** Saves are atomic (temp file +
  rename), so every save changes the file's identity on disk. The stamp has to
  be re-captured after *every* successful write, including **auto-save**, which
  is timer-driven and would otherwise fire this prompt against the user
  endlessly.
- **Stamp = size + last-write-time**, captured at open and at save. Size alone
  misses an equal-length edit; mtime alone is unreliable because some tools
  preserve it. A content hash is certain but costs a full re-read — not worth it
  unless the cheap stamp proves inadequate.
- **Reload is destructive when the buffer is unsaved** — it discards the user's
  edits. It must not be the default button, and the wording must say so. Reuse
  the existing `show_task_dialog` helper rather than a fresh `MessageBoxW`.
- **"Don't ask again" is session- and file-scoped, in memory only.** Never
  persist it: a stale suppression that outlived the session would silently hide
  real external changes. Clear it on Save As and on opening a different file.
  One document per window means it is naturally per-process state.
- **File *deleted* is a distinct case** from modified. Reload is meaningless;
  the offer should be keep-in-buffer or Save As.
- **Interaction with the v0.25.0 handoff restore:** a restored document has a
  file path but buffer content that came from a recovery snapshot, and the file
  on disk may legitimately differ. Establish the stamp deliberately on that path
  or the first activation after an update will report a phantom change.

### v0.28.0 cosmetic round — surveyed and filtered 2026-08-09

48 candidates surveyed across dialogs, menus/wording, theming and status
bar/title; filtered against a strict "appearance only" test. Rejected: anything
that changes behaviour, anything fixing an *incorrect* state (that is a bug, so
it belongs in the patch), and anything moving away from notepad.exe without
being opt-in.

**The two that carry the round are dark-mode chrome, not the icon.**

- **A1 — scroll bars stay white in dark mode.** `ES_DISABLENOSCROLL`
  (`ui_win32.c:1135`) means the vertical gutter is *always* present, so a
  full-height white bar sits against `RGB(30,30,30)`. There is no
  `SetWindowTheme` call anywhere in the file. Fix: `SetWindowTheme(edit,
  L"DarkMode_Explorer")` in `apply_theme` + `SWP_FRAMECHANGED`, with uxtheme
  ordinal 135 resolved via `GetProcAddress` guarded like the existing
  `DwmSetWindowAttribute` lookup. **Caveat:** `SetPreferredAppMode` is
  process-wide and can re-theme other common controls — verify the light path
  is byte-identical before shipping.
- **A2 — status bar text is black on dark grey.** `apply_theme` sets only
  `SB_SETBKCOLOR` to a hard-coded `RGB(45,45,45)`; there is no `WM_DRAWITEM`
  and no `SBT_OWNERDRAW`, so comctl32 keeps painting `COLOR_BTNTEXT` (black).
  Every readout — Ln/Col, zoom, Mono/Prop, EOL, encoding, match counts — is
  effectively unreadable in both dark schemes. Fix: extend `theme_colors` to
  return chrome fg/bg, owner-draw from the existing `window->status_cache`
  (already authoritative, so no new state). Keep plain Light on the
  non-owner-draw path so out-of-box pixels do not move.
- **A3 — Preferences pages render in the wrong font.** All seven prefs pages
  carry `DS_SETFONT` alone while every other dialog carries
  `DS_SETFONT | DS_FIXEDSYS`, so "MS Shell Dlg" is taken literally and
  substitutes to Microsoft Sans Serif *inside* a Segoe UI comctl32 frame. Fix
  is adding `DS_FIXEDSYS` to those seven `STYLE` lines, then re-checking tight
  statics since the DLU ratio differs between the faces.

**A4 folds into the icon work:** `LoadIconW` always returns the `SM_CXICON`
image, so the hand-tuned 16x16 already inside the .ico is *never used* — the
caption bar shows a downscaled 32px instead. Use `LoadImageW` with
`SM_CXSMICON`/`SM_CXICON` per slot, and re-run it from `WM_DPICHANGED`, which
today refreshes font and layout but never the icons.

**A5 — `Alt+E, P` pastes instead of opening Preferences.** `&Paste` and
`&Preferences` collide; S is the only free letter in that popup.

**Trivia batch (A6-A18):** "Text Files" -> "Text Documents (*.txt)" (5 sites,
label only); "Error" -> "npad" dialog captions (3 sites, matches notepad.exe);
Markdown menu mnemonics (`&Unique` vs `&Unindent`, plus four collisions against
the context menu); Find/Replace geometry + tab order as one pass; prefs
mnemonics and a control overlap at `ID_PREF_CTRL_N_WINDOW` / `ID_PREF_FIND_WRAP`;
status bar "Monospace"/"Proportional" and part widths; highlight wash derived
per scheme instead of fixed amber; Solarized Light body contrast (4.13:1, below
AA); encoding popup mnemonics; copyright year; Debug page font ignoring DPI.

**Sequencing constraint — read before scheduling.** A11 raises the status bar's
fixed part budget from 440 to ~495 DIP, which makes the *unclamped*
`SB_SETPARTS` array (`resize_controls`, `ui_win32.c:6434`) misbehave at wider
window sizes than it does today. That clamp is a bug fix, so it belongs in the
patch release — **land it first**, or A11 will look like it caused the problem.

Rejected to the patch release, not cosmetic: the `SB_SETPARTS` clamp above; the
menu bar redrawing twice per settings broadcast (`apply_list_tools_menu` and
`apply_update_indicator` both call `DrawMenuBar`); and the white band on
drag-resize in dark mode (`wc.hbrBackground = COLOR_WINDOW + 1`).

### Theme-matched app icon + bundled file-type icons

Noted 2026-07-31: the user is producing light and dark app icons so the icon
can match the active theme, plus per-association file-type icons meant to ship
**with the installer** rather than inside the executable.

**How icons are wired today, for whoever picks this up:**
- One icon, embedded: `IDI_NPAD ICON "npad.ico"` (`npad.rc:46`). It is bound
  in three places — `wc.hIcon` and `wc.hIconSm` at class registration
  (`ui_win32.c:3490`, `:3495`) and `WM_SETICON` ICON_BIG/ICON_SMALL per window
  (`ui_win32.c:1004-1007`).
- Every association's `DefaultIcon` points at `{app}\npad.exe,0` — i.e. all 14
  ProgIDs currently share the executable's icon index 0.

**Consequences worth knowing before drawing them:**
- **Windows will not swap an app icon by theme on its own.** Both variants have
  to be available and npad must re-issue `WM_SETICON` when the theme changes.
  The class-level `wc.hIcon` is fixed at registration, so the per-window
  `WM_SETICON` path is the one that matters; npad already reacts to theme
  changes (`apply_theme`), so the switch belongs there.
- Embedding both variants keeps the exe self-contained but grows `.rsrc`, which
  was already over half the image before `npad.ico` was repacked 215 KB → 104 KB
  in v0.21.0. Two icons would undo part of that, and image size feeds directly
  into cold-start scan time (see the slow-launch findings above). Loading the
  alternate variant from `{app}` with `LoadImageW(..., LR_LOADFROMFILE)` avoids
  that, at the cost of the portable single-exe build losing the theme switch.
  **That trade-off needs a decision, not a default.**
- File-type icons shipped as separate `.ico` files are straightforward: install
  them to `{app}` and point each ProgID's `DefaultIcon` at
  `{app}\<name>.ico,0` instead of `{app}\npad.exe,0`. They must be listed in
  `[Files]` with `uninsdeleteonuninstall`, and Explorer's icon cache may need a
  nudge before the change is visible.
- This overlaps the associations preferences pane below — if that pane ever
  writes ProgIDs, it must write the same `DefaultIcon` paths.

### Preferences pane for file-type associations

Requested 2026-07-31. Today associations are **installer-only**: the five
grouped tasks in `installer/npad.iss` (Text / Markdown / Data / Config / Logs)
and the matching MSI features. Once npad is installed, changing which
extensions it registers for means re-running setup, which is heavy-handed for
something a user might want to adjust once they have lived with it.

A Preferences page would let extensions be ticked and unticked at runtime.

**Constraints to design around** (all established while building the installer
side — worth not rediscovering):

- **npad cannot make itself the default handler for anything.** Since Windows 8
  the `FileExts\<ext>\UserChoice` key is hash-protected, and Windows rejects a
  programmatic write. The pane can register a ProgID and add npad to the
  Open-with list; making it the *default* still has to go through Settings ▸
  Default apps. The installer already says as much (`npad.iss:127`, `:270`) and
  offers to open that Settings page — the pane should do the same rather than
  imply it can do more.
- **Scope.** The installer writes 95 keys under `HKA` (which resolves to HKLM
  for an all-users install, HKCU otherwise) and 12 explicitly under HKCU. A
  prefs pane runs unelevated, so it can only safely write **HKCU\\Software\\
  Classes** — meaning on an all-users install the pane's changes would shadow
  rather than edit the installed ones. That needs deciding, not glossing over.
- **Ownership and cleanup.** The uninstaller removes what *it* wrote. Anything
  the pane adds later would leak unless it is either written where uninstall
  already sweeps, or tracked in settings so uninstall can find it.
- **Do not write `SupportedTypes`.** It *filters* npad out of Open-with for
  unlisted types; its absence is exactly what makes npad appear for anything.
- The existing ProgIDs are `npad.txt`, `npad.md`, `npad.markdown`, `npad.csv`,
  `npad.tsv`, `npad.json`, `npad.xml`, `npad.yaml`, `npad.yml`, `npad.toml`,
  `npad.ini`, `npad.cfg`, `npad.conf`, `npad.log`.

Per the project's core principle the pane itself is non-destructive (it only
adds npad to choosers), but anything that could displace an existing default
must stay opt-in and explicit.

### Tab inserts spaces

Requested 2026-07-26. An option on the Markdown preferences page to make Tab
insert a configurable number of spaces instead of a tab character. Parked until
wanted.

### Not doing (decided)

- **Markdown menu width** — investigated; the width comes from standard USER32
  behaviour (any item carrying a shortcut label reserves an accelerator column
  for the whole popup). Decided to keep the shortcut visible and accept it.
- **`notepad` from a terminal** — cannot be redirected to npad. `System32\
  notepad.exe` and the Windows 11 Store alias both precede npad on PATH, and
  App Paths is never consulted by cmd/PowerShell. Running `npad` works instead.
