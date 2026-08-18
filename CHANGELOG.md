# Changelog

All notable changes to npad will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.30.1] - 2026-08-19

### 🐛 Fixed
- **The "file changed on disk" dialog was oversized with text spilling out of
  its buttons.** Its choices carry a short explanation on a second line, which
  is the *command link* style - but the dialog was not told to use it, so
  Windows drew an ordinary push button around each whole two-line string.

## [0.30.0] - 2026-08-18

### 🆕 Features
- **npad can tell you when a file you have open is changed by something else.**
  On returning to the window it offers **Reload from disk**, **Save as a
  different file**, or **Keep editing**, with a *"stop telling me about this
  file"* tick box for the rest of the session. A file that has been **deleted**
  is reported differently, since reloading it is not possible.

  **Off by default** - classic Notepad does not do this - and enabled from
  **Preferences > General**.

  It checks when the window is activated rather than polling on a timer: that
  is when you would notice anyway, and it costs nothing while npad is not in
  front. *Keep editing* is the default choice, because Reload discards your
  unsaved edits and that must never be what Enter or Escape does.

## [0.29.0] - 2026-08-18

### 🆕 Features
- **Convert Delimiters remembers what you last used**, and its two fields offer
  your recent values as dropdowns. It previously reset to the same defaults on
  every open, so a repeated conversion meant retyping both fields each time.
- **A Swap button** in Convert Delimiters, to reverse a conversion without
  retyping either field.
- The default target is now `
` rather than `
`. npad normalises the
  document to `
` internally, so the old default quietly taught a token that
  could never work as a *source* - which is what made a `
` search match
  nothing before v0.27.1.

### 🔄 Changed
- **Unsaved work is now snapshotted on the first edit**, rather than only when
  the recovery timer next fires. A brand-new window used to be unprotected for
  a full interval - 30 seconds by default - which is precisely the moment you
  have just started jotting something down. The timer handles everything after
  that, so this costs one extra write per document.

## [0.28.9] - 2026-08-18

### 🐛 Fixed
- **Opening a binary file and saving could destroy it.** npad carries text as
  NUL-terminated strings, so a file containing a NUL byte loads only up to that
  point - a 120 KB PNG becomes nine bytes. That much was merely odd; the danger
  was that the document still pointed at the original file, so typing one
  character and pressing Ctrl+S replaced the image with the fragment.

  npad now detects that it could not read a file in full and **refuses to save
  over it**, offering Save As instead so the original is never the target.
  Auto-save skips such documents outright rather than raising a file dialog
  from a timer.

  Detection happens when the file is decoded rather than from the
  binary-looking warning, because a file can contain a NUL without looking
  binary at all - and a zero *byte* is distinguished from a zero *code unit*,
  so ordinary UTF-16 documents (where every ASCII character contains a zero
  byte) are unaffected.

  Displaying binary content properly is a separate, much larger change and is
  still not attempted; this closes the way it could lose data.

## [0.28.8] - 2026-08-18

### 🐛 Fixed
- **Updating from an affected version could lose your settings.** v0.28.7
  stopped the runaway growth, but it dealt with an already-oversized
  `settings.json` by ignoring it outright - and since npad rewrites that file on
  every exit, the original was then overwritten with defaults. So the release
  that fixed the growth bug could silently discard the settings of the very
  users it was meant to help.

  npad now **salvages** instead of discarding: everything written before the
  runaway value is parsed and kept, and the original is moved aside to
  `settings.json.corrupt` rather than left to be overwritten. Nothing is
  discarded without a copy remaining.

  If you already lost settings upgrading to v0.28.7 they are unfortunately gone;
  this prevents it happening to anyone else, and preserves the file from here on.

## [0.28.7] - 2026-08-17

### 🐛 Fixed
- **settings.json grew exponentially and could reach gigabytes**, eventually
  making npad unusable. Backslashes in a value — a Windows file path, in
  practice — were escaped every time the file was written but never unescaped
  when it was read, so each save/load cycle **doubled** them. Around thirty
  cycles turns one path into a billion characters.

  In the field this produced a **1.2 GB settings file**. Every npad window
  loads that file at startup, so twenty windows consumed 21 GB of RAM between
  them, hammered the disk, and stopped responding; Preferences ▸ Apply became
  slow because each save rewrote it; and eventually npad could not start at all.

  Three separate changes, because the escaping fix alone would not rescue an
  install that is already corrupt:
  - Values are now unescaped when read, so they round-trip unchanged. This is
    the root cause.
  - An implausibly large settings file is ignored at startup, and an
    implausibly long individual value is dropped. **A corrupt file now
    self-heals on the next save instead of preventing npad from starting.**
  - A save that cannot fit every setting is refused outright rather than
    writing a truncated file. The runaway value had overflowed the write
    buffer, silently discarding *every other preference* — which is why the
    corrupt install lost its settings entirely, not just the bad one.

  If you are hit by this, npad now recovers by itself. To reclaim the disk
  space, delete `settings.json` from `%APPDATA%\Platima\npad` while npad is
  closed.

  Note that a path which had *already* doubled stops growing but is not
  repaired - npad cannot distinguish a corrupted `\\` from a deliberate one,
  since UNC paths legitimately begin with two. Such entries age out of Recent
  Files within ten file opens.

### 🧪 Tests
- The save/load round trip had no test at all, which is exactly why this
  survived so long — a single cycle looks correct, and only repetition exposes
  the doubling. Four cases now cover it, including ten consecutive cycles
  asserting the value does not grow. Verified by reverting the fix and
  confirming they fail.

## [0.28.6] - 2026-08-17

### 🔄 Changed
- **The status bar is light in every colour scheme again**, reverting the dark
  version from v0.28.5. Colouring it was only possible by dropping the visual
  style for that control, which also dropped it to classic Windows 95 rendering
  — a sunken bevel around every segment, and text jammed against each segment's
  left edge because the theme's own padding went with it. The colour was right;
  the chrome that came with it was not, and a light status bar is both familiar
  and less jarring than a sunken one.

  Doing it properly would mean npad hand-painting a control Windows normally
  owns — subclassing it, erasing the background and owner-drawing every segment
  borderless. That remains possible if it ever seems worth it; it is a lot of
  surface area for a strip of chrome.

Everything else still follows the colour scheme: the text area, title bar,
scroll bars and drop-down menus. The menu bar stays light for the separate
reason noted in v0.28.5 — Windows draws it as non-client area.

## [0.28.5] - 2026-08-17

### 🐛 Fixed
- **The status bar stayed light in dark schemes.** Its background was being set
  correctly, but with visual styles active comctl32 paints the bar from the
  theme and ignores that colour outright — so the bar stayed light while the
  text went pale grey, which read as disabled. The dark schemes now drop the
  theme for that one control so the colour takes effect. Light schemes are
  untouched.
- **Status messages no longer hide the word / character / line counts.** They
  shared the same segment, so "Checking for updates…" replaced the counts for
  as long as it was up — worst on exactly the slow operations where you might
  still want to read them. The counts now hold their place and the message
  appears beside them.
- **The installer kept re-ticking "set the bundled fonts as npad's default
  editor fonts".** It was made unchecked in v0.27.0, but Inno restores whatever
  was selected on the *previous* install, so anyone who had once enabled it got
  it back on every upgrade — quietly reapplying non-Notepad fonts. The task is
  renamed so there is no prior selection to inherit. Silent installs opt in with
  `/MERGETASKS="fontdefaults2"`.

### 📝 Notes
- **The menu bar is still light in dark schemes, and stays that way for now.**
  Windows draws it as part of the non-client area and offers no supported way to
  recolour it; doing so means owner-drawing it through undocumented messages,
  which is a lot of risk for a strip of chrome. Drop-down menus, the title bar,
  scroll bars, the status bar and the text area all follow the scheme.

## [0.28.4] - 2026-08-09

### 🎨 Appearance
- **The window's own icon now follows npad's colour scheme, while the taskbar
  icon keeps following Windows.** They are separate slots, so a light npad on a
  dark-themed Windows no longer shows a dark icon in its own light title bar.
  This is the **Automatic** mode (the default, renamed from "Follow the Windows
  light/dark setting" since it now does something more specific). Picking
  *always light*, *always dark* or *classic* still pins both surfaces, because
  an explicit choice should be taken literally.
- **Narrowed the gutter either side of the text.** v0.28.3 widened it from 4 to
  9, which measured at 16px in use - too far. It is now 6.

### 📝 Notes
- Two limits of Automatic mode, both inherent to Windows: a **grouped** taskbar
  button (several npad windows combined - the Windows 11 default) is drawn from
  the executable's icon rather than the window's, so the taskbar variant is not
  consulted there; and with **small taskbar buttons** the taskbar reads the
  caption's slot, so both surfaces show the same artwork. Neither loses the
  icon; they just stop differing.

## [0.28.3] - 2026-08-09

### 🐛 Fixed
- **Word wrap no longer inflates the line count or misdirect Go To.** With wrap
  on, npad was counting *wrapped rows* rather than lines, so a 96-line document
  with ten long lines reported 106, the Ln indicator climbed past the real line
  count, and **Go To jumped to the wrong place**. All three now count the
  document's actual lines, matching the file and each other, whether wrap is on
  or off.
- **Widened the gutter either side of the text.** It was tight enough that a
  leading `l`, `I` or `h` could visually merge into the window border. It now
  sits close to notepad.exe's, and scales with DPI instead of being a fixed
  pixel count.

## [0.28.2] - 2026-08-09

### 🐛 Fixed
- **Dark scroll bars did nothing unless Windows itself was set to dark.** npad
  asked uxtheme to *allow* dark theming, which follows the Windows app-mode
  setting rather than npad's own colour scheme. Since npad defaults to Light
  and dark is opt-in, "dark npad on a light desktop" is a common combination -
  and it was exactly the one where v0.28.0's headline fix silently did nothing,
  leaving the white scroll bar it was meant to remove. The mode is now forced
  from npad's scheme.
- **The reverse, too: a stock (light) npad on a dark-themed Windows picked up
  dark menus** against its white editor. Light schemes are now light throughout,
  whatever Windows is set to.
- **The leftmost status bar segment kept its old text and colour when switching
  schemes.** Changing scheme re-sends every segment to change its draw style,
  but an empty segment - which is the default, with counts off - was skipped as
  a no-op, so a lingering "Match 3 of 7" stayed black on the new dark
  background. Exactly the unreadable text v0.28.0 set out to fix, surviving in
  one segment.

## [0.28.1] - 2026-08-09

### 🐛 Fixed
- **v0.28.0 published without its installers.** Renaming the icon resources in
  that release left the MSI still pointing at the old `npad.ico`, so the
  installer build failed after the release had already been created — leaving
  only the portable executable, and no `setup.exe` for the in-app updater to
  download. Fixed, and CI now verifies every file the installer sources
  actually exists, so this fails on the pull request rather than half way
  through a release.

## [0.28.0] - 2026-08-09

A cosmetic release: appearance only, no behaviour changes.

### ✨ Features
- **A new app icon that follows your light/dark setting.** It tracks the
  Windows *taskbar* theme, since that is the surface the icon is shown on.
  **Preferences ▸ Appearance** offers: follow Windows, follow npad's own colour
  scheme, always light, always dark, or the **classic icon** for anyone who
  prefers it.
- The icon is now loaded at the size Windows actually asks for. It previously
  always used the 32px image, so the hand-tuned 16×16 inside the file was never
  shown — the caption bar got a downscaled one. It also refreshes when a window
  moves to a display with a different DPI.

### 🎨 Appearance
- **Dark schemes now theme the scroll bars.** The vertical bar is always
  present, so until now a full-height white strip sat against the dark text
  area. Requires Windows 10 1809 or later; older builds keep the light bars.
- **The status bar is readable in dark schemes.** Only its background was ever
  themed, so the text stayed black — every readout (Ln/Col, zoom, Mono/Prop,
  line ending, encoding, match counts) was effectively unreadable. The light
  schemes are untouched and render exactly as before.
- **The Preferences pages use the same font as the rest of the app.** They were
  falling back to Microsoft Sans Serif inside a Segoe UI frame — two typefaces
  in one window.
- **`Alt+E, P` opens Preferences instead of pasting.** It collided with
  **P**aste; Preferences is now `Alt+E, S`.
- The Markdown menu's **Uni**q**ue** no longer collides with **U**nindent.
- File dialogs say **"Text Documents (\*.txt)"**, and file-error dialogs are
  captioned **npad** — both matching notepad.exe. Filters and behaviour are
  unchanged; only the labels moved.

## [0.27.1] - 2026-08-09

### 🐛 Fixed
- **Convert Delimiters: a `\r\n` in the From field matched nothing**, in every
  file regardless of its line endings. The document is normalised to `\n`
  internally before matching, so a literal `\r\n` could never appear — while
  the dialog's own default *target* is `\r\n`, which teaches you the token and
  then fails silently when you use it as a source. `\r\n` is now accepted and
  means `\n`.
- **The Find match count stayed in the status bar after closing the dialog.**
  Worse than it looked: the counts are what would otherwise overwrite it and
  they are off by default, so in a stock configuration it persisted for the
  rest of the session.
- **The status bar drew inverted parts on a narrow window or at high DPI.** The
  part edges were computed by subtraction with no clamp, so they could go
  negative and out of order. Parts now collapse in order instead.
- **Typing Tab, Enter, Escape or an arrow key in the editor could be diverted
  to an open Find dialog.** `IsDialogMessageW` was called for every queued
  message while that dialog existed, which MSDN explicitly warns against.
- **"Paste as Markdown" was never greyed out**, so it stayed available over an
  empty clipboard and did nothing. It now follows the clipboard, and correctly
  stays enabled for HTML-only content that plain Paste cannot take.
- **"Interpret escapes" appeared or disappeared only when the Find dialog was
  reopened.** Toggling Basic Markdown support now reaches the open dialog.
- **The menu bar flashed on every settings change**, being redrawn twice — once
  for the Markdown menu and once for the update indicator.
- **Dragging the window larger in a dark scheme flashed a white band** in the
  newly exposed area. The frame painted the fixed system window colour rather
  than the scheme's.
- A memory leak: the Highlight All match list was never freed.

## [0.27.0] - 2026-08-09

### 🔄 Changed
- **npad now opens in Consolas 11 out of the box, matching notepad.exe.**
  Notepad has a single font setting and ships it as Consolas 11 — exactly what
  npad already used as its *monospace* default. But npad started every window in
  *proportional* mode, so that correct default was never reached and you got
  Segoe UI instead. New windows now start in monospace mode.

  > **Existing installs:** if you never chose a font type, this will change your
  > font on upgrade. **Ctrl+M** switches back to proportional immediately, and
  > Preferences ▸ Defaults makes it stick. Anyone who has already picked a type,
  > or set their own faces, is unaffected.

  The proportional default stays **Segoe UI**. Making it Consolas too would have
  matched Notepad equally on first launch but left the status bar reporting
  "Prop" while showing a monospace face, and Ctrl+M doing nothing at all.
- **The installer no longer sets the bundled fonts as npad's defaults unless you
  ask.** That task is now **unchecked**, because it rewrites `settings.json` to
  Intel One Mono / Roboto — a deliberate departure from the Notepad-like
  appearance npad is supposed to have out of the box. The fonts are still
  installed and available in the pickers; only the automatic override changed.
  Silent deployments opt in with `/MERGETASKS="fontdefaults"`.

### 📝 Notes
- The documented rationale for the old default was simply wrong: it claimed
  proportional mirrored classic Notepad. Notepad has never defaulted to a
  proportional face — Fixedsys, then Lucida Console, then Consolas.

## [0.26.0] - 2026-08-01

### 🔄 Changed
- **Find and Replace no longer wrap around by default**, matching notepad.exe,
  which ships that box unchecked. Wrapping changes what a search *finds* rather
  than adding to it — a term Notepad reports as missing would silently be found
  — so by the project's own rule it is now opt-in.

  Your existing setting is untouched: this only changes the default for fresh
  installs. There is now a **Preferences ▸ General ▸ "Find and Replace wrap
  around by default"** checkbox, so it no longer has to be hunted down in the
  Find dialog.
- Find options now propagate between running npad instances like every other
  shared setting. They were process-globals read once at startup, so changing
  one in another window previously had no effect here until restart.

### 🔍 Diagnostics
- **Scroll telemetry on the hidden Debug preferences page**, added to chase a
  reported fault where the view scrolls on its own. It counts the input the
  editor actually receives (wheel, `WM_VSCROLL`, buttons, capture changes) and,
  at each repaint, whether the view moved and whether anything could account for
  it. That distinguishes the three possible causes: something feeding input, a
  stuck drag or scrollbar track, or RichEdit moving the view itself.

  It is passive, allocation-free and has no timer of its own — sampling
  piggybacks on repaints, which is what scrolling causes, so it costs nothing
  while the window is idle and adds about 2 KB to the binary. The whole thing
  is behind `#define NPAD_SCROLL_TELEMETRY` and compiles out to nothing.

## [0.25.0] - 2026-07-29

### ✨ Features
- **Unsaved work now survives an update or a Windows restart.** When something
  other than you closes npad — installing an in-app update, or Windows Update
  restarting the machine — every unsaved document is parked as-is and reopened
  automatically afterwards, still unsaved, with no prompt in either direction.
  This is what npad is for as a scratchpad, and it is what Windows 11 Notepad
  does. It never writes to your actual files.

  A **normal** quit — the X, Ctrl+W, File ▸ Exit — still asks Save / Don't Save
  / Cancel exactly as before. Only closures npad did not initiate are silent.
- **npad no longer blocks a Windows restart.** It previously had no
  `WM_QUERYENDSESSION` handler at all, so Windows terminated it outright: any
  typing since the last 30-second snapshot was lost, along with the window
  position, and what came back was the "npad may have closed unexpectedly"
  prompt. npad now saves its state when Windows asks and consents immediately.
- npad registers for **automatic relaunch after an update reboot**, pointing
  Windows at its own document so several open windows come back as several
  windows. Registered for reboots and patching only — deliberately *not* for
  crashes or hangs, so a genuine crash still surfaces under the "npad may have
  closed unexpectedly" prompt rather than quietly reappearing as though nothing
  had happened. This depends on Windows' own "restart my apps" setting, so it
  is never relied on: the parked documents restore on the next launch anyway.

### 🐛 Fixed
- **Starting an update from a window with unsaved work no longer prompts you to
  save.** That window closed itself through the ordinary quit path, so it asked
  — despite the update dialog having just said npad would close. It now parks
  the document and goes.
- **Installing an update now closes every npad window, not just the one that
  started it.** The other windows are separate processes and nothing had ever
  told them to close, so they were left running and holding `npad.exe` open
  where setup needed to replace it. They are now asked to park their work and
  exit, and the update waits for them before starting the installer.
- A close requested by the installer's Restart Manager, or by Windows shutting
  down, no longer raises a save prompt either. On the installer path that
  prompt could be invisible, leaving setup apparently hung until it timed out.

### 📝 Notes
- The handoff snapshot ignores the "Restore unsaved work after a crash"
  preference. That setting means *don't nag me about crashes*; honouring it
  here would mean silently destroying the buffer of anyone who turned it off.
  Parked documents are never presented with crash wording, so the setting keeps
  its meaning.
- Only **modified** documents are parked. A saved file does not reopen itself,
  because restored content always comes back flagged as unsaved and reopening a
  clean file that way would wrongly mark it dirty.
- npad windows running a **pre-0.25.0** build cannot receive the new close
  request, so the first update after this one may still leave them open. From
  0.25.0 onwards they close cleanly.

## [0.24.1] - 2026-07-29

### 🐛 Fixed
- **Status bar numbers now follow your regional settings.** Word, character and
  line counts — and the "Match 3 of 7" / "Replaced N occurrences" messages —
  are grouped using the thousands separator Windows is configured to use, so
  they read `1,234,567` in en-AU/en-US, `1.234.567` in de-DE and `12,34,567`
  in hi-IN instead of a bare run of digits. A separator customised in Control
  Panel takes precedence over the locale default, matching every other app.
  `Ln` / `Col` are deliberately left ungrouped: they are positions rather than
  quantities, and grouping them would put a comma inside `Ln 1,234, Col 5`.
- The status bar compared only the first 63 characters of a segment when
  deciding whether its text had changed, so two different strings sharing that
  prefix would leave the old text on screen. Only reachable with very large
  counts, but grouping made the line longer, so the cache now holds a full
  segment and the length is derived from the buffer rather than hardcoded.

## [0.24.0] - 2026-07-29

### 🔄 Changed
- **The in-app updater now resolves its download from the release itself**
  instead of rebuilding the file name from the version tag. It reads the
  release's asset list — which it was already downloading — and matches on the
  end of each name. A future change to release asset naming therefore cannot
  strand npads already in the field, which is exactly what happened in v0.17.x.
  The historic name is still used as a fallback, and the SHA-256 verification
  is unchanged: only `https://github.com` URLs are accepted, and a digest
  mismatch still deletes the download and aborts.
- Releases now fail CI if an asset the updater looks for is missing. The guard
  reads the suffixes **from the updater's own source**, so renaming assets
  without updating the updater — or the reverse — is caught at release time
  rather than silently in the field.

## [0.23.0] - 2026-07-28

### ✨ Features
- **An "Install Now" button on the Updates preferences page**, next to the
  latest version. Previously, clicking the silent-notification indicator took
  you to this page, which told you an update was available but gave you no way
  to act on it — you had to press **Check Now** a second time just to get the
  prompt back. The button appears whenever a newer, non-skipped version is
  known, regardless of the notification mode: if the page says an update
  exists, the button that installs it works.

### 🐛 Bug Fixes
- **Setup now offers to relaunch npad after an in-app update.** v0.22.1 taught
  setup to offer a relaunch only when it had closed a running npad, but it
  detected that by looking for an npad window — and during an in-app update
  npad closes *itself* before setup starts, so there was nothing left to find.
  npad now tells setup explicitly (`/RELAUNCH=1`).
  - *This only takes effect from the next update onwards*: the flag is sent by
    the npad you are updating **from**, so updating from 0.22.1 or earlier will
    still not offer the relaunch.

## [0.22.1] - 2026-07-28

### 🐛 Bug Fixes
- **The save prompt's keyboard mnemonics are back.** v0.20.0's new task dialog
  dropped them, so Alt+S / Alt+N no longer worked and the letters were not
  underlined. Restored to match notepad.exe (**S**ave, Do**n**'t Save), and
  added to the binary-file and update prompts too, which never had them.

### 🔄 Changed
- **Setup only offers to relaunch npad if it had to close it.** Previously
  every install ended with a "Launch npad" checkbox, even when npad had not
  been running. Setup now checks up front and offers a **Relaunch npad** option
  only in that case. The Restart Manager is also stopped from silently
  restarting npad behind the checkbox, so exactly one instance starts, and only
  if you leave the box ticked.

## [0.22.0] - 2026-07-28

### 🚀 Performance
- **npad now loads four Windows libraries only when it actually needs them,
  instead of on every launch.** `winhttp` and `bcrypt` serve only the opt-in
  update check (off by default), `comdlg32` only the Open / Save As / Font
  dialogs, and `msimg32` only the Highlight All wash — yet all four were bound
  into the executable, so the system had to locate and map them before npad's
  code ran. That cost is largest on a cold start, which is exactly when startup
  feels slow.
  - **Statically imported DLLs: 12 → 8.**
  - Verified at runtime: each library is absent from the process at launch,
    appears the moment its feature is first used, and the feature works
    normally.
  - Built with `dlltool --output-delaylib` from a `.def` per library under
    `src/platform/delay/`, which also documents exactly which entry points npad
    uses. (GNU `ld` has no `--delay-load`; the delay-import library is how it is
    done with the GNU toolchain.)

## [0.21.0] - 2026-07-28

Performance round. Neither reported slowdown could be reproduced on the test
machine, so rather than guess, this release makes npad able to *report* where
the time goes, and removes work it should never have been doing.

### 🔄 Changed
- **The executable is 26% smaller** (425 KB → 314 KB). The application icon
  accounted for over half the binary because its larger images were stored
  uncompressed; they are now PNG-compressed. Every icon size is retained and
  verified pixel-for-pixel identical. A smaller image is quicker to read the
  first time it runs after a while, which is exactly when startup is slow.
- **The startup profile (Debug page) now measures from process creation**, not
  from npad's own entry point. Everything the system does first — loading the
  image and its DLLs, C-runtime init, manifest processing, anti-malware
  inspection — was previously invisible and reported as 0.0 ms. It is now the
  first line of the profile.
- The "deferred tasks" line is labelled as firing on a 50 ms timer, so it is
  not mistaken for startup work — it is the last number in the profile and
  reads much larger than the real cost.
- **Paint timings now include the Highlight All overlay** and report a **max**
  as well as an average. The overlay was previously excluded, so the Debug page
  under-reported precisely the case most likely to be slow, and a peak is what
  actually feels like a stutter.

### 🚀 Performance
- Removed a redundant DPI-awareness setup block that loaded and freed
  `shcore.dll` on **every launch** for a code path that cannot run on any
  supported Windows version — DPI awareness comes from the embedded manifest,
  which the system applies before npad's code runs. (Verified: npad is still
  per-monitor-v2 aware.)
- `dwmapi.dll` is now resolved once instead of being loaded and freed on every
  theme application, window creation and settings broadcast.

### 📋 Measured, for reference
On the test machine, a warm start is ~40 ms end to end (~12 ms of that before
npad's own code runs), and scrolling a 5.6 MB document of very long lines with
word wrap, live counts and Highlight All all enabled drains a 300-notch scroll
burst in under 5 ms. If you see worse, the Debug page (Ctrl+Shift+.) will now
say where it went — please send that profile.

## [0.20.0] - 2026-07-26

### ✨ Features
- **npad now appears in Windows' "Open with" list for any file type**, matching
  notepad.exe, so you can open an unusual text-ish file in npad without having
  associated that extension. This is purely additive: it adds npad to the
  chooser and to Settings > Default apps (now with an icon), and **never**
  changes which app owns a file type. Installed by default (MSI feature
  `OpenWith`).
  - Deliberately **not** written: a `SupportedTypes` list (declaring one would
    *filter* npad out of "Open with" for every type not listed — the opposite
    of the goal), and `PerceivedType` / `Content Type` values (they live on the
    shared extension keys, do nothing for "Open with", and uninstall would
    delete rather than restore values other apps may own).

### 🔄 Changed
- **The unsaved-changes prompt now matches notepad.exe**: a task dialog with
  **Save / Don't Save / Cancel** (was Yes / No / Cancel), no icon, and the
  question as the main instruction. This also fixes the cramped window-title
  inset, which was a consequence of the old dialog type rather than any
  setting.
- **Task dialogs now centre on the npad window** instead of the monitor,
  matching every other npad dialog. Affects the update-available and
  binary-file prompts too.
- **The vertical scroll bar is now always shown** (greyed when the content
  fits), like classic Notepad, instead of appearing and disappearing. As a
  bonus this keeps the text area a constant width, so crossing the
  "needs a scroll bar" threshold no longer reflows every wrapped line. With
  word wrap off, the horizontal bar behaves the same way.
- **Clicking into an unfocused npad window now works on the first click.**
  Clicking — or click-dragging to select — places the caret straight away
  instead of the click being consumed by activating the window.
- Status bar: the optional word/character/line counts are no longer jammed
  against the left window border.

### 🐛 Bug Fixes
- **"Replaced N occurrences" no longer vanishes** a fraction of a second after
  Replace All when the status-bar counts are enabled (the counts refresh armed
  by the replacements themselves used to overwrite it).
- Replace All's status message now keeps the status-bar cache in sync, so an
  identical follow-up message is no longer suppressed.
- Hardened the word-wrap toggle so it can no longer persist RichEdit's
  transient "no scroll bar needed" state into the control's style.

> **Note on the disappearing scroll bar:** a report of the vertical scroll bar
> vanishing while the view stayed scrolled could **not** be reproduced across
> six scenarios (text shrinking, window resize, zoom, word-wrap toggle, font
> rebinding), and the suspected cause was disproved by measurement — RichEdit
> restores that state correctly in every path tested. The always-visible bar and
> the style hardening above are fidelity and robustness improvements, **not** a
> confirmed fix. Please report the context if it recurs.

## [0.19.0] - 2026-07-26

### ✨ Features
- **Rich-text paste (Markdown).** When Basic Markdown support is enabled,
  pasting content that carries HTML (from a browser, word processor, etc.)
  can convert it as it lands. Two conversion depths:
  - **Lists** - `<ul>`/`<ol>` (including nesting) become list lines in your
    current bullet style, everything else flattened to plain text.
  - **Markdown** - full conversion: headings (`#`), bold (`**`), italic
    (`*`), inline code and fenced code blocks, links, images, block quotes,
    horizontal rules, and numbered ordered lists.
- **Configurable paste modes** (Preferences > Markdown > Paste): two radio
  groups set what **Ctrl+V** (default *Lists*) and **Ctrl+Shift+V** (default
  *Plain*) do - each can be Plain, Lists, or Markdown.
- **Paste as Markdown** command (Markdown menu + right-click) always does a
  full conversion, and **Ctrl+Shift+V** is a new paste accelerator.
- All of this is gated on Basic Markdown support; with it off (the default),
  every paste stays plain, exactly as before. Plain-text sources (no HTML on
  the clipboard) always paste plain regardless of mode.

### 🔧 Internal
- New platform-independent, unit-tested core module `src/core/html_md.c`
  (CF_HTML fragment extraction + a bounded HTML→Markdown converter) with
  `tests/test_html_md.c`; the converter caps output size and nesting depth so
  hostile or huge clipboard input can never run away.

## [0.18.0] - 2026-07-25

### ✨ Features
- **`npad` on PATH.** The installers now add the install folder to PATH (a
  default-on task in the setup EXE; the `PathEnv` feature in the MSI), so
  `npad` and `npad file.txt` work from Command Prompt and PowerShell. A
  per-user install edits the user PATH; an all-users install edits the machine
  PATH. (Taking over bare `notepad` in a terminal is *not* possible - the
  System32 copy and the Windows 11 Store alias precede us on PATH; the existing
  Settings shortcut to disable that alias is unchanged.)
- **Grouped file associations.** The per-extension checkboxes are now five
  groups so the list stays short: **Text** (`.txt`, default on), **Markdown &
  documents** (`.md`, `.markdown`), **Data** (`.csv`, `.tsv`, `.json`, `.xml`,
  `.yaml`, `.yml`, `.toml`), **Config** (`.ini`, `.cfg`, `.conf`) and **Logs**
  (`.log`). MSI feature ids: `AssocText` (default), `AssocMarkdown`,
  `AssocData`, `AssocConfig`, `AssocLog`.

### 🔄 Changed
- **The Help update item now reflects an available update.** It transforms in
  place from **Check for Updates...** to **Update Available (v…)...** (which
  opens the Updates page) when a newer, non-skipped version is known, and back
  again otherwise - alongside the existing **●** on the Help title.
- **Word / character / line counts update live while typing** instead of only
  after you pause. Very large documents (over ~1 MB) keep the settle-then-count
  behaviour so a full rescan does not thrash.
- **Highlight all matches tracks live** as you type in either the document or
  the search box, and re-appears correctly after the match count drops to zero
  and back.
- **Interpret escapes** moved to the bottom of the Find and Replace option
  lists and is now shown only when Basic Markdown support is enabled (it is
  part of that feature set); it stays inert when Markdown support is off.
- **Markdown preferences** now lead with a **Basic Markdown support** checkbox
  and a short list of what it enables (list tools, escape interpretation,
  delimiter replacement).

## [0.17.1] - 2026-07-23

### 🔄 Changed
- **Consistent release asset naming.** All Windows downloads now follow one
  scheme, `npad-v<version>-<type>-win-x64.<ext>`:
  - `npad-v<version>-setup-win-x64.exe` (was `npad-setup-<version>.exe`)
  - `npad-v<version>-msi-win-x64.msi` (was `npad-<version>.msi`)
  - `npad-v<version>-portable-win-x64.exe` (was `npad-<version>-windows-gui.exe`)
  Previously the three used three different conventions (only the portable
  build carried a `v`, the variant word floated between positions, and the
  MSI had none).

### 🐛 Bug Fixes
- The **in-app updater** downloads the installer under its new name; a
  release's notes now list the actual asset filenames (they previously
  showed `v`-prefixed setup/MSI names that were never produced).

> **Note for anyone already on v0.17.0 or earlier:** the built-in
> "Check for Updates" in those versions looks for the *old* installer name,
> so updating to v0.17.1+ via the in-app updater will not find the download
> - grab `npad-v0.17.1-setup-win-x64.exe` from the Releases page once by
> hand, and the updater works normally from then on.

## [0.17.0] - 2026-07-21

### ✨ Features
- **Updates preferences tab** and a full update-policy model, all opt-in
  (default **Off** - most notepad-like; a manual Help > Check for Updates
  always works). One mode picker chooses how a found update surfaces:
  - **Off** - manual checks only.
  - **Notify silently** - a **●** appears after the Help menu title plus an
    "Update Available" item that opens this tab; no dialog.
  - **Prompt me** - the update dialog appears.
  - **Download and install automatically** - the installer is downloaded and
    SHA-256-verified silently, then a single confirmation appears before it
    runs (a fully seamless no-close update is not possible: Inno/MSI replace
    the locked npad.exe, so npad closes through its normal save prompt first).
- **Check for updates on launch** (separate toggle, off by default): a single
  check fires once after the window first paints, for the primary window only.
- **Skip this version**: from the update dialog or the prefs tab; suppresses
  the dot/prompt for that version until a newer one appears.
- The Updates tab shows the current and latest-known versions and when the
  last check ran, with a **Check Now** button.
- Launch/automatic checks fail **silently** (status-bar note only); a manual
  check still reports errors. The dot/decision logic is a unit-tested core
  helper (`update_is_newer_unskipped`).

### 🐛 Bug Fixes (from an adversarial code review)
- **Reset All Preferences** now resets *every* preference, including the
  Markdown, status-bar counts, and Updates settings that a hardcoded allowlist
  had silently missed. Reworked to reset everything except a small preserved
  set (recent files, window geometry, find/replace state), so future
  preferences are reset by default; guarded by a new `test_settings` suite.
- A literal **Tab** could be silently swallowed after the Custom Indent prompt
  (the swallow flag is now armed before the modal dialog can run).
- The optional word/char/line **counts** no longer go stale when the status
  bar is hidden during edits and then re-shown.
- A pending counts refresh no longer overwrites a newer transient status
  message (e.g. "Match 3 of 7").
- Preferences > Defaults now shows the correct font type for users migrated
  from a pre-0.7 build (honours the legacy `monospace_enabled` key), so
  Applying without changing it no longer flips their default.

## [0.16.0] - 2026-07-20

### ✨ Features
- **Check for Updates** (Help menu): strictly on-demand - npad never checks
  in the background and never updates automatically. Queries the GitHub
  releases API on a worker thread (the UI never blocks), compares versions
  numerically, and offers **Download and install / View the release notes /
  Cancel** when a newer release exists. The installer and its published
  `.sha256` are downloaded to the temp directory, the SHA-256 checksum is
  verified (a mismatch deletes the file and aborts), and only then does a
  final confirmation launch the installer and close npad through the normal
  save prompt. Zero new dependencies (WinHTTP + Windows CNG).

## [0.15.0] - 2026-07-20

### ✨ Features
- **Highlight all matches**: a new checkbox in the Find and Replace dialogs
  washes every match of the search text with a translucent amber overlay,
  live as you retype (debounced) and re-painted after edits. The overlay is
  drawn over the text rather than stored in it, so it never marks the
  document modified or enters the undo history, and clears when the dialog
  closes. Capped at 10,000 matches.
- **Word / character / line counts** in the status bar (off by default;
  Preferences > Appearance). Shown in the leftmost segment and recomputed
  on a short debounce after edits, so typing, pasting and large opens stay
  smooth.

### 🔄 Changed
- The large-file warning threshold now **scales with installed RAM** when
  the preference was never set (1/64th of physical memory, clamped
  50-1024 MB); an explicit `large_file_warning_mb` value still wins.

## [0.14.0] - 2026-07-20

### ✨ Features
- **Binary-file detection**: opening a file that doesn't look like text
  (NUL bytes outside UTF-16, or mostly control characters) now prompts
  with **Cancel / Open in npad / Open with the default app** before
  anything else happens.

### 🔄 Changed
- **Defaults now follow the notepad-first principle** (documented in the
  README): out of the box npad behaves like Windows 10 notepad.exe, with
  only non-destructive enhancements enabled.
  - Auto-save is now **off** by default (it overwrites the file from a
    timer, so it's opt-in).
  - Session resume / crash recovery is now **on** by default (snapshots
    never touch the user's file).
  - New windows default to the **proportional** font.
  - Explicit values in an existing settings.json are unaffected.
- **All dialogs** (Go To Line, Convert Delimiters, Custom Indent) now open
  at the same notepad-style offset into the window as Find/Replace,
  instead of centred; the offset is a single define
  (`NPAD_DIALOG_OFFSET_X/Y`).
- Pre-open checks (binary and large-file warnings) run before the
  save-changes prompt, so declining them never costs a pointless
  "save changes?" round trip.

### 🐛 Bug Fixes
- Enter no longer doubles the marker when splitting a line right before an
  existing bullet ("- item |- second" used to become "- - second"); it
  falls back to a plain newline.
- Shift+Tab now always unindents - the selected lines, or the current line
  when nothing is selected (it previously required a selection).
- Unindent and markdown deepening now recognise whatever bullet a line
  actually carries (`* `, `- `, or the custom prefix) instead of only the
  configured format's marker, so e.g. "- " bullets unindent while the
  default format is " - ".

## [0.13.0] - 2026-07-19

### ✨ Features
- **Custom indent format**: the Indent submenu and Preferences gain a
  **Custom…** option - any prefix (escapes `\t \\ \uXXXX` allowed), prompted
  for with the saved value prefilled and remembered across sessions.
  Non-whitespace custom prefixes behave like the built-in markers
  (markdown-style nesting).
- **Tab / Shift+Tab indent** (Markdown tools): with any selection, Tab
  indents and Shift+Tab unindents using the default format; without one, Tab
  still types a tab. A new preference switches the binding back to
  **Ctrl+]** / **Ctrl+[**; menu labels follow the active binding.
- **Enter continues lists** (Markdown tools): Enter on a `* ` / `- ` /
  custom-marker line starts the next line with the same indent and marker;
  Enter on an empty bullet removes the marker and inserts a plain newline,
  ending the list.
- **Cut line / paste above** (Markdown tools): Ctrl+X with no selection cuts
  the whole current line; Ctrl+V then pastes it above the current line with
  the caret staying put, until the clipboard changes hands.
- **Find/Replace escapes**: a new "Interpret escapes" checkbox in both
  dialogs makes the fields understand `\n \r \t \\ \uXXXX`, so line breaks
  can be searched and inserted. History keeps the raw typed text.

### 🔄 Changed
- **Marker formats now include a trailing space** (`* `, `- `, ` * `, ` - `)
  for real markdown output; unindent removes the marker including the space
  (and still strips old space-less bullets).
- The **Lists** preferences page and menu are now called **Markdown**
  (settings keys are unchanged).
- Go To Line and Convert Delimiters dialogs open centered on the npad
  window instead of the top-left corner of the screen.
- Sort/Unique/Indent line scoping now follows logical lines, so word wrap
  no longer fragments the target into display lines.

### 🐛 Bug Fixes
- Help > About no longer shows the previous release's tag in the commit
  suffix of dev builds (e.g. `v0.12.0-dev (v0.11.0-2-gabc123)`); it now
  shows the plain commit hash.

## [0.12.0] - 2026-07-16

### ✨ Features
- **List tools** (opt-in; off by default for classic-Notepad compatibility).
  Enable in Preferences > Lists to add a **List** menu and matching
  right-click items:
  - **Sort** (Ascending / Descending, with a Case-sensitive toggle) and
    **Unique** (remove duplicate lines, keeping the first). A selection
    spanning multiple lines acts on those lines; otherwise the whole
    document.
  - **Convert Delimiters…** - a find/replace for delimiters with escape
    sequences (`\n \r \t \\ \uXXXX`), e.g. turn commas into line breaks or
    back. Applies to the selection or the whole document.
  - **Indent** / **Unindent** (**Ctrl+]** / **Ctrl+[**) with six formats
    (spaces, tab, `*`, `-`, ` *`, ` -`); the default is set in Preferences.
    Indenting an already-marked line adds two spaces (markdown-style
    nesting) rather than a second marker; unindent removes the current unit.
  - New `src/core/list_ops.c` carries the transforms, covered by the
    `test_list_ops` unit suite.

## [0.11.0] - 2026-07-16

### ✨ Features
- **Hidden Debug page in Preferences** (Ctrl+Shift+. or Shift+click the
  Preferences menu item): startup phase profile (per-phase timings through
  first paint), paths and counts (settings entries, recovery slots, open
  windows), live editor counters (paints with last/avg duration, selection
  changes), and a Copy Diagnostics button
- **Reset All Preferences** button on the Backup tab: restores every
  preference to its default while keeping recent files, window position and
  Find/Replace history; applies live to all open windows

### 🐛 Bug Fixes
- **Installer**: Add/Remove Programs now lists the app as "npad" instead of
  "npad version x.y.z" (the version has its own column there)

### ⚡ Performance
- **Instant startup**: the crash-recovery scan now runs just after the first
  paint instead of before the window appears, so launch feels immediate even
  with a large recovery directory
- **Realtime scrolling on large documents**: status-bar updates from
  scrolling/caret movement are coalesced onto a short timer (max ~30/s)
  instead of recomputing Ln/Col synchronously on every wheel notch and key
  repeat, and unchanged status segments are no longer repainted

## [0.10.5] - 2026-07-15

### 📝 Documentation
- README, DOCUMENTATION.md and the release-notes template now explain the
  SmartScreen/Defender workaround for the unsigned downloads: right-click →
  Properties → tick **Unblock** → Apply (or "More info" → "Run anyway"),
  ideally after verifying the SHA256 against `CHECKSUMS.txt`
- Roadmap: code-signing the releases added as a planned item

## [0.10.4] - 2026-07-15

### 🧹 Housekeeping
- **Releases ship only the implemented product**: the Windows installer,
  MSI and portable GUI exe. The Linux (X11/Wayland/terminal) and Windows
  terminal variants are unimplemented stubs - CI still compile-checks
  them, but they are no longer published as release downloads. Release
  validation now asserts the expected Windows asset set (presence,
  plausible sizes, checksum spot-check) instead of executing a stub.

## [0.10.3] - 2026-07-14

### ✨ Features
- **Installer**: new "Set the bundled fonts as npad's default editor
  fonts" task (checked by default, shown when the fonts are being
  installed or already present). Selecting it sets `monospace_font` /
  `proportional_font` in settings.json - updating an existing file in
  place while preserving every other setting - instead of the previous
  behaviour of only pre-setting fonts when no settings.json existed.
  Deselect the task (or `/MERGETASKS="!fontdefaults"` silently) to leave
  the configuration untouched.

## [0.10.2] - 2026-07-14

### 🐛 Bug Fixes
- **Version string**: builds made exactly at a release tag now report a
  clean `vX.Y.Z` (About showed `v0.10.1-dev ()` because the CI installer
  build had no git available - the MSYS2 job now installs git, and a
  git-less build falls back to `vX.Y.Z-dev` instead of empty parentheses)
- **Installer**: the post-install "open App execution aliases Settings"
  offer now only appears when the Windows 11 Store Notepad's alias stub is
  actually present (`%LOCALAPPDATA%\Microsoft\WindowsApps\notepad.exe`) -
  on machines without it, npad's App Paths entry already owns `notepad`
  and the page has nothing to do

## [0.10.1] - 2026-07-14

### 🧹 Housekeeping (public-release readiness)
- Untrack `.claude/settings.local.json` and ignore local editor/agent config
  (`.claude/`, `npad.code-workspace`)
- Move stray design assets (`icon_v1*.png`) into `assets/`
- LICENSE copyright year updated to 2025-2026
- Full-history audit: no secrets or personal paths found in any commit
- CI and release workflows moved from the self-hosted runner to GitHub-hosted
  runners (`ubuntu-latest` + explicit dependency installs) so fork PRs never
  execute on private hardware once the repository is public

## [0.10.0] - 2026-07-13

### ✨ Features
- **Windows installers.** Two flavours, both defaulting to a per-user install
  (no admin needed) with system-wide available when elevated or chosen:
  - `npad-setup-<version>.exe` (Inno Setup) - interactive installer with
    optional bundled fonts (Intel One Mono, Roboto, OpenDyslexic; SIL OFL),
    file-association tasks (.txt on by default; .log/.ini/.cfg/.conf opt-in),
    a 'notepad' alias task (on by default, per the roadmap), desktop icon,
    and silent support (`/VERYSILENT [/ALLUSERS]`). Installing fonts pre-sets
    them in a fresh settings.json (never overwrites an existing one).
  - `npad-<version>.msi` (WiX) - silent/managed deployment
    (`msiexec /i npad.msi /qn`, add `ALLUSERS=1` for machine-wide); features
    selectable via `ADDLOCAL` mirror the Inno defaults. Fonts install in
    machine-wide mode only (Windows MSI limitation: FontsFolder does not
    redirect per-user).
  - Fonts are fetched at installer-build time from SHA256-pinned upstream
    releases (`installer/fetch-fonts.ps1`); nothing binary enters the repo.
  - CI: new `installers.yml` workflow (hosted Windows runner) builds both,
    runs standalone via workflow_dispatch and attaches artifacts to releases
    from `release.yml`.

### 📝 Notes
- Setting the *default* app for a file type and disabling the Windows 11
  Store Notepad execution alias cannot be automated; the installer registers
  npad and offers the relevant Settings pages after install (documented in
  DOCUMENTATION.md).
- Uninstall removes all installer registry entries and files but keeps user
  settings and any installed fonts.

## [0.9.1] - 2026-07-13

### 🧹 Housekeeping
- Silence two cppcheck false positives on the zoom-preservation checks
  (`EM_GETZOOM` writes through pointers cppcheck cannot see), restoring a
  clean `make lint`. No behaviour change.

## [0.9.0] - 2026-07-13

### ✨ Features
- **Warn before lossy ANSI saves** (classic-Notepad parity): manually saving
  a document as ANSI when it contains characters the system code page cannot
  represent (emoji, most non-Latin scripts) now asks before replacing them
  with `?`; declining cancels the save. Auto-save silently skips such
  documents instead of prompting from a timer (crash-recovery snapshots
  still protect the content).

### 🐛 Bug Fixes
- **Emoji and other non-Latin characters render correctly after opening a
  file.** Loaded text was stamped with the configured font over the whole
  document, which replaced the fallback fonts RichEdit assigns to characters
  the configured font lacks - so emoji that displayed fine while typing came
  back as placeholder boxes after save/reopen (the bytes on disk were always
  correct). Text is now loaded via `EM_SETTEXTEX` with the default format
  set first, keeping font fallback intact; font and theme changes re-trigger
  fallback for such characters, and the window's zoom no longer resets when
  a file is loaded into it.

### 🔍 Verified
- Encoding round-trip proven byte-exact on the Windows build for all five
  encodings (UTF-8 `63 61 66 C3 A9`, UTF-8 BOM `EF BB BF ...`, UTF-16 LE
  `FF FE 63 00 ...`, UTF-16 BE `FE FF 00 63 ...`, ANSI `63 61 66 E9` for
  "café"). Note: a file containing only ASCII characters is byte-identical
  in UTF-8 and ANSI, so tools like `file` report `us-ascii` - that is
  correct UTF-8 output, not a missing encoding.

## [0.8.0] - 2026-07-11

### ✨ Features
- **Modern Save As dialog.** Save As now uses the shell's `IFileSaveDialog`,
  so it looks native and matches the Open dialog instead of the old
  pre-Vista styling. Its **Encoding** dropdown reliably applies the chosen
  encoding, and it pre-fills the current file's name and folder (Save As of
  an open document no longer defaults to "Untitled.txt").
- **Close** (Ctrl+W) closes the current window after the usual save check;
  **Close All Windows** (Ctrl+Shift+W) closes every open npad window, each
  prompting to save its own document (Cancel keeps that window open). Both
  are on the File menu.

### 🐛 Bug Fixes
- Choosing an encoding when saving now actually writes the file in that
  encoding. The previous Save dialog's encoding picker did not propagate its
  selection, so files were saved as UTF-8 regardless. (Pure-ASCII text still
  reports UTF-8, since ASCII and ANSI bytes are identical.)

### 🧹 Housekeeping
- Consolidated changelogs: removed `CHANGES.md`; `CHANGELOG.md` is the single
  curated record and `git log` is the commit-level history.

## [0.7.1] - 2026-07-10

### 🐛 Bug Fixes
- Crash recovery no longer re-offers sessions that are **still running**: a
  newly launched instance skips recovery slots whose owning process is a
  live npad.exe, so it stops prompting to "restore" work that isn't lost
  (and stops re-prompting right after you just restored).
- **Restored/opened documents now use the configured font and theme.**
  `SetWindowTextW` was dropping the RichEdit character formatting, so
  restored text showed in the control's default face; the font/colour is
  now re-applied after loading text.
- Default window size adjusted to ~56% x ~60% of the work area to match
  Notepad.
- Manually opened New Windows (Ctrl+Shift+N) now cascade like restored
  ones, instead of stacking on top of each other.
- Enabling "Sync view across all instances" now brings the other windows
  into line with the active window's font type and zoom immediately, not
  only on the next change.
- Dropped the non-working attempt to always show menu access-key underlines;
  npad now follows standard Windows behaviour (underlines on Alt).
- `settings.json` is now written atomically (temp file + rename), so an
  interrupted save cannot truncate it.

## [0.7.0] - 2026-07-10

### ✨ Features
- **Defaults** Preferences tab: default encoding, line endings, font type
  (monospace/proportional) and zoom for new windows, with a **Use Current**
  button that captures the active window's state.
- **Backup** Preferences tab (was "Files"): settings export/import.
- Font type and zoom are now **per-window view state**, like classic
  Notepad: toggling Monospace or zooming affects only that window and new
  windows start from the Defaults-tab values. Two opt-in preferences extend
  this: **"Sync view across all instances"** (Appearance) mirrors view
  changes live to every open window, and **"Auto-update defaults in real
  time"** (Defaults) makes view changes become the new defaults.
- Menu-driven settings (word wrap, status bar, fonts) now save immediately
  and propagate live to other open windows, like the Preferences dialog.
- New `DOCUMENTATION.md` describing every setting, shortcut, status-bar
  action and behaviour.

### 🐛 Bug Fixes
- The status bar's font-type segment could show the opposite mode and did
  not refresh when toggling via the menu; it now always shows the window's
  current state and updates immediately.
- Menu access-key underlines are now genuinely always visible: the fix is
  applied after the window is shown and re-applied on every activation
  (the previous attempt ran before activation and was reset).
- Preferences pages sized to 240 DLU (fixing round 6's over-correction).
- Default window width reduced to ~48% of the work area (height ~72%);
  the previous 72% width was too wide.
- Crash-restored windows cascade 80px apart (was 40px, too subtle).
- The Apply button now persists and propagates changes immediately, not
  only when the dialog closes.

### ⚠️ Versioning note
Versions 0.2.0-0.6.0 below were previously accumulated under a single
"0.2.0-dev" entry; they have been renumbered per semver (minor for feature
rounds, patch for fix-only rounds). `git log` maps every commit to these
versions.

## [0.6.0] - 2026-07-09

### ✨ Features & Fixes
- Default colour scheme is now **Light** (classic Notepad has no schemes);
  "Follow system" is still selectable.
- **Preference changes now propagate to other open npad windows** live
  (each instance reloads settings and re-applies theme/font/etc.).
- The **default window size** is now a DPI-correct fraction of the
  monitor work area, centred, instead of a fixed 800x600 - much better on
  large / high-DPI displays. The size is still remembered once you resize.
- Menu **access-key underlines are always shown** (not only while Alt is
  held), matching classic Notepad. (Completed in 0.7.0.)
- **Crash-restored extra windows now cascade** instead of stacking exactly.
- Narrowed the Preferences pages (0.5.1 overshot); long options wrap.

## [0.5.1] - 2026-07-09

### 🐛 Bug Fixes
- **Fixed** crash recovery restoring only one document when several windows
  were open. Each instance now writes its own recovery slot, and on the next
  launch npad restores one in the current window and reopens the rest in new
  windows.
- **Fixed** the Monospace toggle being stuck on the proportional font after a
  restart: `OpenDyslexic` was overriding the monospace/proportional choice
  even when the font was not installed (RichEdit then substituted a fallback
  face). OpenDyslexic is now only used when actually installed.
- **Fixed** OpenDyslexic staying enabled after saving even when the font is
  absent; it now reverts (and the checkbox unchecks) with an install hint.
- **Fixed** the Preferences window clipping long text; the pages are wider.
- **Fixed** the Preferences Apply button never enabling; pages now mark
  themselves changed so Apply activates on any edit and previews without
  closing the dialog.

## [0.5.0] - 2026-07-08

### ✨ Fonts, New Window & settings backup
- **Fixed** session recovery not being offered after a crash: the startup
  check now looks for a leftover snapshot unconditionally (the enabled flag
  was not persisted when a run was killed before a clean exit), and the flag
  is now saved to disk the moment it is toggled.
- **Fixed** the Monospace toggle appearing to do nothing: the monospace and
  proportional fonts now have distinct defaults (Consolas vs Segoe UI) and
  each has its own picker in Preferences > Appearance.
- **Fixed** the status bar clipping the line-ending text ("Windows (CRLF)").
- New Window (Ctrl+Shift+N) opens a second independent instance. A
  Preferences option swaps Ctrl+N / Ctrl+Shift+N between "New" and
  "New Window".
- OpenDyslexic font option for reading assistance (Preferences > Appearance),
  with an install hint when the font is missing.
- Export / Import settings buttons (Preferences > Files) for backup or moving
  configuration between machines.
- Preferences now has an Apply button, so changes can be previewed without
  closing the dialog, and are persisted to disk immediately.
- Removed View > Dark Mode (it clobbered a selected Solarized scheme); the
  theme is chosen in Preferences > Appearance. Solarized is credited to
  Ethan Schoonover in the README.

## [0.4.0] - 2026-07-08

### ✨ Session recovery & Find/theme polish
- Session resume / crash protection (disabled by default, configurable):
  unsaved work is snapshotted to a recovery folder on a timer, and offered
  for restoration on the next launch after an unclean exit. Snapshots are
  cleared on a clean save or exit.
- Find / Replace now remembers recent search and replace terms in dropdowns
  and shows a live "Match X of Y" count in the status bar.
- Solarized Light and Solarized Dark colour schemes, selectable from a new
  Appearance colour-scheme picker (alongside System / Light / Dark).
- The status bar's font-mode segment toggles monospace when clicked.
- Preferences moved from the File menu to the Edit menu (Ctrl+, unchanged),
  with a new General-page session-resume option.
- New pure `session` core module with unit tests for the recovery format.

## [0.3.0] - 2026-07-07

### ✨ Notepad parity & quality-of-life round
- Right-click context menu in the editor (Undo/Redo/Cut/Copy/Paste/Delete/
  Select All), with state-aware enabling.
- Find/Replace: "Wrap around" option (with a "Wrapped around" status
  indicator), compact classic-Notepad layout with the Direction group
  beside the checkboxes, dialogs open offset into the window like
  notepad.exe and remember their position; find options persist.
- Tabbed Preferences dialog (Ctrl+,): auto-save, large-file threshold,
  recent-files size and clearing; theme and status bar; default encoding
  and line endings for new files.
- Line ending conversion: Format > Line Endings, Ctrl+E cycles, applied on
  save; also available by clicking the status bar's line-ending part.
- Encoding picker in the Save dialog; encoding also changeable from the
  status bar's encoding part.
- Status bar click actions: Ln/Col opens Go To, zoom resets to 100%.
- Monospace toggle (Format > Monospace, Ctrl+M) between Consolas and the
  chosen font.
- Ctrl+Drop inserts the dropped file's contents at the caret instead of
  opening it.
- Undo depth raised from RichEdit's default 100 actions to 100,000.
- Status bar refreshes immediately after Ctrl+Scroll zoom and when the
  caret moves onto a new empty line (fixed RichEdit end-of-text line
  reporting plus stale-refresh events).

## [0.2.0] - 2026-07-07

Full review and repair release. A code audit found that several features
described in earlier entries (Find/Replace, word wrap toggling, redo,
modified-state tracking) did not actually work; this release rewrites the
affected code and implements them for real. Entries for 0.1.5-0.1.8 below
should be read with that in mind.

### 🐛 Bug Fixes
- **CRITICAL**: A failed save (e.g. disk full) deleted the file being saved
  over. Saves are now atomic: write to a temp file, verify, then rename.
- **CRITICAL**: The save-changes prompt was Yes/No and never saved -
  answering "Yes" silently discarded changes. It is now the proper
  Save / Don't Save / Cancel prompt and actually saves.
- **CRITICAL**: After saving via the close prompt, the window could never be
  closed (the modified flag was never cleared). Close via the X button and
  File > Exit now share one code path.
- **CRITICAL**: Enter and Tab did not insert characters (IsDialogMessage was
  applied to the main window and the edit control lacked ES_WANTRETURN).
- **CRITICAL**: Typing or pasting beyond 64KB was silently dropped
  (EM_LIMITTEXT 0 sets a 64KB cap on rich edit controls); the limit is now
  ~2GB via EM_EXLIMITTEXT.
- Go To Line dialog buttons did nothing (broken hand-rolled modal loop);
  it is now a real resource-based dialog, with Notepad's out-of-range message.
- The window title showed "*Untitled" after any edit even with a file open.
- Paths containing ".." (e.g. `npad ..\notes.txt`) were rejected.
- Version string rendered as "NPAD_VERSION_MAJOR...." in builds where the
  Makefile did not inject it (missing macro double-expansion).
- Word wrap toggle changed style bits that rich edit ignores; it now uses
  EM_SETTARGETDEVICE and actually wraps. Default is off (classic Notepad).
- The application manifest (common controls v6, per-monitor DPI) was no
  longer embedded; restored.
- Window position is validated against connected monitors, and the
  maximized state is saved and restored.
- Double ReleaseDC in the font dialog.

### ✨ Features
- Full Unicode support: the Win32 layer uses wide APIs end to end, with
  UTF-8 as the internal representation.
- Encoding detection and preservation: UTF-8, UTF-8 BOM, UTF-16 LE/BE
  (with or without BOM), and ANSI files round-trip unchanged.
- Line ending detection and preservation (CRLF / LF / CR), shown truthfully
  in the status bar (previously hardcoded to "Windows (CRLF)" and "UTF-8").
- Working Find / Replace dialogs (the dialog resources existed but were
  never wired up): direction, match case, whole word, F3 / Shift+F3.
- Redo is reachable: Edit > Redo menu item and Ctrl+Y.
- Drag-and-drop opens files (the WM_DROPFILES handler was missing).
- Zoom: Ctrl+Plus / Ctrl+Minus / Ctrl+0, Ctrl+Scroll, View > Zoom menu,
  real percentage in the status bar.
- Auto-save: a real timer now exists (silent save for named documents,
  default 5 minutes, configurable).
- Dark mode: editor colors, title bar and status bar; follows the system
  theme by default and can be toggled from the View menu.
- Recent Files menu (the settings plumbing existed but was unused).
- Menu items enable/disable correctly (Undo/Redo/Cut/Copy/Paste/Delete/Find).
- Edit > Delete (Del) and Edit > Time/Date (F5), like Notepad.
- Monospace default font (Consolas 11pt); font choice persists.

### 🔧 Technical Improvements
- Removed the memory-limit subsystem (working-set-based caps, paste-undo
  enforcement, per-keystroke full-document copies). A single confirmation
  prompt for very large files remains ("large_file_warning_mb" setting).
- Editor no longer copies the entire document on every keystroke.
- UTF-8 file paths work on Windows (_wfopen / MoveFileExW).
- Makefile: native MinGW builds on Windows, fixed install/uninstall/clean.
- CI now runs the unit test suite; new tests cover encoding and line-ending
  detection/round-trips, atomic write behavior, and path validation.

### ⚠️ Breaking Changes
- The `max_file_size_mb` / `max_memory_usage_mb` / `memory_limit_warnings`
  settings are gone; `large_file_warning_mb` (default 100) replaces them.
- Word wrap now defaults to off, matching classic Notepad; the previous
  state is persisted in the `word_wrap` setting.

## [0.1.8] - 2025-06-18

### 🐛 Bug Fixes
- **CRITICAL**: Fixed scrollbar behavior at launch and with word wrap toggle
- **CRITICAL**: Fixed font rendering to use proper Windows default GUI font
- **CRITICAL**: Fixed About dialog to use application icon instead of generic information icon
- **CRITICAL**: Fixed file path validation preventing legitimate file paths from being opened
- Fixed vertical scrollbar to be always visible but auto-enabled when content overflows
- Fixed horizontal scrollbar to appear only when word wrap is disabled, auto-enabled when needed
- Fixed word wrap default state to be enabled at launch (matching Windows Notepad behavior)
- Fixed file dialog "unsafe path" errors for legitimate absolute file paths
- Started removing ANSI-specific function calls and old unnecessary //FIXED comments
- Added a slightly improved icon

### ✨ Features
- **Enhanced scrollbar behavior matching Windows Notepad exactly**
  - Vertical scrollbar always visible, enabled automatically when content exceeds viewport
  - Horizontal scrollbar controlled by word wrap state, auto-enabled when content exceeds width
  - Word wrap enabled by default at launch (classic Windows Notepad behavior)
- **Improved font rendering using proper Windows system fonts**
  - Uses lfMessageFont with correct weight and character set
  - Maintains RichEdit functionality while achieving authentic font appearance
- **Enhanced About dialog with proper application icon display**

### 🔧 Technical Improvements
- Enhanced edit control creation with proper scrollbar management
- Improved font configuration using LOGFONTA and CHARFORMAT2A for accurate system font rendering
- Better file path validation that allows legitimate paths while preventing directory traversal
- Enhanced About dialog using MessageBoxIndirectA with custom icon
- Improved word wrap toggle logic with proper ES_AUTOHSCROLL flag management
- Better initial window state with correct default word wrap setting

### 📋 UI/UX Improvements
- Scrollbars now behave exactly like Windows Notepad (vertical always visible, horizontal controlled by word wrap)
- Font rendering now matches other Windows applications using proper system font configuration
- About dialog displays npad icon instead of generic information icon
- File dialogs no longer show false "unsafe path" errors for legitimate file selections
- Word wrap enabled by default for better out-of-box experience
- Improved visual consistency with Windows system UI standards

### ⚠️ Breaking Changes
None - all improvements maintain backward compatibility whilst enhancing user experience

## [0.1.7] - 2025-06-18

### 🐛 Bug Fixes
- **CRITICAL**: Fixed editor control font to use proper Windows system font instead of fixed-width font
- **CRITICAL**: Fixed window icon display to use npad.ico resource
- **CRITICAL**: Fixed title bar to properly show asterisk (*) for modified files
- **CRITICAL**: Fixed horizontal scrollbar behavior with word wrap toggle
- **CRITICAL**: Ensured vertical scrollbar is always visible and enabled as needed
- Fixed edit control margins to match authentic Windows Notepad spacing (4px left/right)
- Fixed word wrap functionality to properly toggle horizontal scrollbar visibility
- Enhanced RichEdit control font configuration to use proper Windows system font

### ✨ Features
- **Enhanced Windows Notepad UI authenticity**
  - Enhanced RichEdit control with proper Windows system font configuration
  - Correct edit control margins and spacing
  - Always-visible vertical scrollbar
  - Horizontal scrollbar controlled by word wrap state
  - Window icon properly displayed from npad.ico resource
  - Authentic title bar behavior with modification indicator

### 🔧 Technical Improvements
- Improved edit control creation with proper Windows styling flags
- Enhanced font selection using SystemParametersInfo for authentic system font rendering
- Better scrollbar management for authentic notepad behavior
- Proper icon resource loading and display
- Enhanced title update logic for modification state tracking

### 📋 UI/UX Improvements
- Edit control now uses proper Windows system font instead of fixed-width font
- Window icon displays correctly in title bar and taskbar
- Title properly shows asterisk for modified files (e.g., "*Untitled - npad")
- Vertical scrollbar always visible, enabled when content exceeds view
- Horizontal scrollbar appears only when word wrap is disabled
- Edit control margins match classic Windows Notepad exactly
- Overall appearance more authentic to original Windows Notepad

### ⚠️ Breaking Changes
None - all improvements maintain backward compatibility whilst enhancing user experience

## [0.1.6] - 2025-06-18

### 🐛 Bug Fixes
- **CRITICAL**: Fixed window title bar sizing and positioning issues
- **CRITICAL**: Fixed edit control appearance to match standard Windows Notepad behaviour
- **CRITICAL**: Fixed Enter key not creating new lines in text editor
- Fixed status bar line/column tracking that was not updating properly
- Fixed status bar zoom level display functionality
- Fixed file dialog appearance to use modern Windows UI instead of legacy interface
- Fixed dark mode being enabled by default (now defaults to system light mode)
- Removed sunken border appearance from edit control for authentic notepad look
- Fixed edit control to use proper system colours and theming

### ✨ Features
- **Enhanced Windows UI fidelity with authentic notepad appearance**
  - Switched from RichEdit to standard EDIT control for true notepad behaviour
  - Applied proper system theming and colour schemes
  - Improved window styling to match classic Windows Notepad exactly
  - Enhanced status bar updates for real-time cursor position tracking
  - Added proper Enter key handling for multiline text input (ES_WANTRETURN flag)

### 🔧 Technical Improvements
- Improved window creation with cleaner styling flags
- Enhanced status bar update mechanism with proper cursor tracking
- Better system font selection matching Windows Notepad defaults
- Removed unnecessary window edge styling for cleaner appearance
- Updated file dialogs to use modern Windows Explorer-style interface
- Enhanced edit control configuration for optimal text editing experience

### 📋 UI/UX Improvements
- Title bar now properly sized and positioned relative to menu bar
- Edit control appearance matches Windows Notepad (flat, not sunken)
- Status bar properly updates line and column numbers in real-time
- File dialogs use modern Windows appearance instead of legacy UI
- Default theme properly follows system settings (light mode default)
- Text editor now properly handles Enter key for new line creation

### ⚠️ Breaking Changes
None - all fixes maintain backward compatibility whilst improving user experience

## [0.1.5] - 2025-06-18

### 🔒 Security
- **CRITICAL**: Fixed path traversal vulnerability in file operations
- **CRITICAL**: Added buffer overflow protection in text replace operations  
- **CRITICAL**: Enhanced input validation to prevent injection attacks
- Added file size limits (100MB) to prevent resource exhaustion
- Improved path validation against directory traversal attempts

### 🐛 Bug Fixes
- **CRITICAL**: Fixed thread safety issues in settings management causing potential deadlocks
- **CRITICAL**: Corrected Windows CreateWindowEx flag separation causing UI creation failures
- Fixed missing mutex unlocks in settings operations
- Fixed memory leaks in JSON serialisation error paths
- Fixed integer overflow in replace operation size calculations
- Improved error handling for UI component creation
- Fixed resource cleanup in file operation error paths

### 🔧 Technical Improvements
- Enhanced thread-safe editor state management with mutex protection
- Added comprehensive bounds checking for all string operations
- Improved error reporting with detailed context information
- Added proper validation for all function parameters
- Enhanced memory management with overflow detection
- Improved API consistency in Windows UI implementation
- Added this changelog

### ⚠️ Breaking Changes
None - all fixes maintain backward compatibility

## [0.1.4] - 2025-06-05

### ✨ Features
- **Enhanced Windows UI with comprehensive dialogue system**
  - Added status bar with cursor position, zoom level, encoding, and line endings display
  - Upgraded to RichEdit control whilst maintaining plain-text behaviour
  - Implemented full keyboard shortcuts (Ctrl+N/O/S/Z/X/C/V/A/F/H/G, Alt+Z)
  - Added word wrap toggle functionality (Alt+Z)
  - Created custom InputBox dialogue for Go to Line feature (Ctrl+G)
  - Enhanced menu structure with new options and keyboard shortcuts
  - Improved accelerator table handling in message loop

### 🐛 Bug Fixes
- Fixed static analysis warnings in InputBox implementation
- Store return values from CreateWindow calls to satisfy cppcheck
- Store LoadLibrary return value with explicit void cast
- Applied clang-format line length adjustments to InputBox code

### 🔧 Technical Improvements
- Enhanced Windows UI with improved dialogues and message handling
- Updated version to 0.1.4 with proper version management
- Added comments explaining why handles are not actively used

## [0.1.3] - 2025-06-04

### ✨ Features
- **Comprehensive Windows UI Enhancements**
  - Added status bar with cursor position, zoom, encoding, and line endings
  - Upgraded to RichEdit control whilst maintaining plain-text behaviour
  - Implemented keyboard shortcuts (Ctrl+N/O/S/Z/X/C/V/A/F/H/G, Alt+Z)
  - Added word wrap toggle functionality (Alt+Z)
  - Created custom InputBox dialogue for Go to Line feature (Ctrl+G)
  - Added proper accelerator table handling in message loop
  - Updated window sizing to accommodate status bar
  - Enhanced menu structure with new options and shortcuts

### 🔧 Technical Improvements
- Refactored build system and centralised version management
- Renamed Windows terminal executable from npad-win32-terminal.exe to npad-terminal.exe for clarity
- Updated GitHub CI and release workflows to use new executable name
- Centralised version information in main.h with Makefile extraction using awk
- Replaced hardcoded version string in About dialogue with NPAD_VERSION macro
- Improved version consistency across build system and UI components

## [0.1.2] - 2025-06-04

### 🔒 Security
- **Path Traversal Protection**: Added comprehensive input validation and path traversal protection in file operations
- **Buffer Overflow Prevention**: Fixed buffer overflow vulnerabilities with proper bounds checking in snprintf calls
- **Memory Safety**: Added memory allocation failure checks throughout the codebase

### ✨ Features
- **Find and Replace Functionality**: Added complete find and replace functionality with case-sensitive and whole-word options
- **Thread Safety**: Implemented thread safety for settings management with mutex protection
- **Atomic Operations**: Added atomic operations for modification state tracking to prevent race conditions

### 🔧 Technical Improvements
- Integrated pthread support for Linux builds in Makefile
- Fixed DPI awareness initialisation warnings on Windows platform
- Enhanced security improvements throughout the codebase

### 📋 Testing & Quality
- **Comprehensive Error Handling System**: Added centralised error reporting with detailed context, categories, and severity levels
- **Thread-Safe Error Handling**: Implemented thread-safe error handling with timestamped logging and callback support
- **Testing Infrastructure**: Created lightweight unit testing framework with assertion macros and test statistics
- **Comprehensive Test Suite**: Added test suite for file operations (11 tests) and error system tests (6 tests)
- **Build Integration**: Integrated testing targets into Makefile with individual and combined test execution
- All 38 test assertions pass successfully validating core functionality

## [0.1.1] - 2025-06-04

### 🏗️ Build System & CI/CD
- **First Successful CI Build**: Achieved first successful continuous integration build
- Enhanced CI tests with detailed echo statements for Windows and Linux builds
- Improved echo statements and fixed syntax errors in CI tests
- Enhanced CI workflow and build system for Windows and Linux variants
- Added stripping to release builds and tidied up CI for passing tests
- Added executable permission to macOS and Linux build targets
- Updated .gitignore to exclude build artifacts properly

### 🔧 Technical Improvements
- **DPI Awareness**: Implemented DPI awareness for Windows platform
- **Windows Terminal UI**: Added Windows Terminal UI stub implementation
- **Build Targets**: Updated Makefile for new targets and cleanup
- **Resource Management**: Added Windows manifest and resource files
- Removed obsolete build artifacts

### 📋 Platform Support
- Enhanced support for multiple build variants
- Improved cross-platform build compatibility
- Fixed formatting and build system issues

## [0.1.0] - 2025-06-02 to 2025-06-04

### 🎉 Initial Development Phase
- **Core Editor Implementation**
  - Basic text editing functionality with Windows Win32 UI
  - File operations (New, Open, Save, Save As)  
  - Text editing operations (Cut, Copy, Paste, Select All, Undo)
  - Cross-platform UI abstraction layer
  - Settings management with JSON storage

### 🏗️ Multi-Platform Foundation  
- **Platform Support**
  - Windows implementation using Win32 API
  - Linux implementation stubs (X11, Wayland)
  - macOS implementation stub (Cocoa)
  - Terminal implementation stub (ncurses)
  - Cross-compilation support in build system

### 🔧 Developer Infrastructure
- **Build System**: Comprehensive build system with Makefile
- **CI/CD Pipeline**: GitHub Actions continuous integration and deployment
- **Code Quality**: Code formatting with clang-format (K&R style)
- **IDE Integration**: VS Code integration with tasks and launch configurations
- **Documentation**: Comprehensive README, contributing guidelines, and project structure

### 🏛️ Architecture
- Clean separation between core logic and platform-specific UI
- Thread safety primitives with mutex support  
- Centralised error handling and logging foundation
- JSON-based settings storage with platform-specific paths
- Cross-compilation capabilities and automated dependency management

### 📋 Project Infrastructure
- MIT Licence
- GitHub repository setup with professional structure
- Contributor guidelines and coding standards
- Automated release workflow
- GitHub Sponsors funding setup
- Version management and semantic versioning adoption

---

## Development Milestones

### Scrollbar & Font Refinements Focus (v0.1.8)
Perfected the final details of Windows Notepad authenticity by fixing scrollbar behavior, font rendering, dialog icons, and file path validation. Achieved pixel-perfect scrollbar behavior and proper system font rendering that matches Windows UI standards exactly.

### Windows Notepad Authenticity Focus (v0.1.7)
Achieved near-perfect Windows Notepad visual and behavioral authenticity. Fixed font rendering, window iconography, title bar behavior, scrollbar management, and edit control spacing to match the original Windows Notepad exactly.

### UI Fidelity Focus (v0.1.6)
Major improvements to Windows UI authenticity, fixing visual inconsistencies and ensuring npad matches classic Windows Notepad appearance and behaviour exactly. Enhanced real-time status tracking and modern dialog interfaces.

### Security & Stability Focus (v0.1.2-0.1.5)
Major emphasis on security hardening, thread safety, and comprehensive testing. Addressed critical vulnerabilities whilst building a robust testing framework.

### UI Enhancement Phase (v0.1.3-0.1.4)  
Significant improvements to Windows user experience with modern UI elements, keyboard shortcuts, and enhanced dialogue systems whilst maintaining classic Notepad simplicity.

### Build System Maturation (v0.1.1-0.1.2)
Established reliable CI/CD pipeline, cross-platform build support, and automated testing infrastructure.

### Foundation Building (v0.1.0)
Core architecture establishment, multi-platform groundwork, and basic text editor functionality implementation.

---

*For detailed technical changes, see individual commit messages in the project repository.*