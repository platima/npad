# npad TODO

Working notes for things that are parked, unverified, or waiting on a decision.
Curated release history lives in [CHANGELOG.md](CHANGELOG.md); this file is for
open loops.

---

## 🔍 Needs your input

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
