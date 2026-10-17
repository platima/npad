# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

npad is a lightweight, cross-platform text editor inspired by classic Windows Notepad. It's built in C using native platform APIs (Win32, X11, Cocoa) for maximum performance and minimal resource usage. The project emphasizes simplicity and speed over feature richness.

## Build System

**Primary build commands:**
- `make` - Auto-detect platform and build
- `make all` - Build all Windows and Linux variants
- `make windows` - Build for Windows x64 using MinGW (cross or native)
- `make windows-arm64` - Build `npad-arm64.exe` (Windows on Arm) with llvm-mingw
  on PATH (`scripts/install-llvm-mingw.sh` fetches CI's pinned copy)
- `make linux` - Build Linux variants (GTK 3 desktop `npad-linux`, ncurses stub)
- `make debug` - Build with debug symbols enabled
- `make clean` - Clean build artifacts

**Code quality commands:**
- `make lint` - Run cppcheck static analysis (requires cppcheck)
- `make format` - Auto-format code with clang-format
- `make format-check` - Verify code formatting without changes
  (canonical against clang-format 18, matching CI; override the binary with
  `CLANG_FORMAT=... make format-check` if your distro ships another major)
- `make test` - Build and run the unit test suites (file ops, errors, encoding)

**Installers (Windows-only build step):**
- `pwsh installer/build-installers.ps1` - builds `dist/npad-v<v>-setup-win-x64.exe`
  (Inno Setup, interactive) and `dist/npad-v<v>-msi-win-x64.msi` (WiX, silent). Requires
  Inno Setup 6 + the WiX dotnet tool + a built npad.exe; bundled fonts are
  fetched from SHA256-pinned releases by `installer/fetch-fonts.ps1`.
  `-Arch arm64` builds the `-win-arm64` set from `npad-arm64.exe`.
- CI equivalent: `.github/workflows/installers.yml` (workflow_dispatch, and
  called from release.yml to attach installers to releases)
- Release dry run (nothing published): `gh workflow run release.yml -f version=v<v>`
  once release.yml is on main; until then run installers.yml and packages.yml
  by hand on the branch

**Cross-compilation:**
The project is designed for cross-compilation from Linux to Windows using MinGW-w64 (`x86_64-w64-mingw32-gcc`). On Windows itself, a native MinGW toolchain (MSYS2 / Git Bash) is used automatically; override with `MINGW_CC`/`MINGW_WINDRES`/`MINGW_STRIP`. The setup script `./scripts/setup-runner.sh` installs necessary dependencies on Linux.

## Architecture

npad uses a clean layered architecture separating platform-specific UI from core logic:

```
Core Logic (platform-independent)
    ↓
UI Interface Layer (abstraction)
    ↓  
Platform Implementation (Win32/X11/Cocoa/ncurses)
```

**Key components:**
- `src/core/` - Platform-independent editor logic, file operations, settings
- `src/ui_interface.h/c` - UI abstraction layer defining events and window operations
- `src/platform/` - Platform-specific UI implementations (ui_win32.c, ui_gtk*.c, etc.)
- `src/main.c` - Application entry point and initialization sequence

**State management:**
- `EditorState` struct in `src/core/editor.h` maintains editor state
- Settings stored as JSON in platform-appropriate locations
- Window state persistence for position/size/maximized across sessions
- All editor logic runs on the single UI thread; editor state is not locked

**Text handling conventions:**
- Core code passes UTF-8 strings; the Win32 layer converts to UTF-16 at the API boundary (wide APIs throughout)
- `file_read_text_ex`/`file_write_text_ex` detect and preserve each file's encoding (UTF-8/UTF-8 BOM/UTF-16 LE/BE/ANSI) and line endings (CRLF/LF/CR)
- Editor saves are atomic (temp file + verify + rename); a failed save must never destroy the existing file

## Development Notes

**Before touching a subsystem, read [LESSONS.md](LESSONS.md).** It is the
register of non-obvious findings that already cost time or shipped a
regression — status bar theming, icon slots, dark mode, RichEdit line
handling, the installers, and the tooling traps (cppcheck version skew, shell
heredocs mangling C escapes, `git commit -m` quoting). Each entry is
*what looks true → what is actually true → what to do*. Add to it whenever
something turns out to work differently than it reasonably appears to.

**Code style:**
- C99 standard with -Wall -Wextra compiler flags
- clang-format configuration in `.clang-format`
- No external dependencies beyond system libraries

**Platform targets:**
- Windows: Win32 API (primary focus, most complete)
- Linux: GTK 3 for X11 & Wayland (in progress - see LINUX_PORT.md)  
- macOS: Cocoa (planned)
- Terminal: ncurses (planned)

**Key design principles:**
- Native APIs over frameworks
- Zero external dependencies
- Performance and minimal resource usage
- Classic Notepad simplicity with quality-of-life improvements
- **Core principle**: out of the box npad mimics Windows 10 notepad.exe.
  Non-destructive enhancements that don't change that core behavior (crash
  recovery, status bar, atomic saves) are ON by default; anything that
  alters or could destroy it (auto-save, system color scheme, Markdown
  tools, live view sync) is OFF by default and opt-in. Apply this rule when
  choosing any new setting's default.
- The product name is always lowercase: **npad** (in UI strings, docs and
  installer alike)

**Settings & propagation model:**
- Shared settings save to settings.json immediately when changed (menus or
  Preferences) and propagate live to all open instances via a registered
  window message; font type and zoom are per-window view state seeded from
  the Defaults preferences (see DOCUMENTATION.md for the full model)

**Branches:**
- `main` takes only releases and Windows fixes: every push to it runs the full
  CI build, so it is never the place for work in progress. It is protected:
  changes land only through a pull request (a direct push is rejected), so
  even a one-line fix goes branch, then PR, then merge
- Linux port work integrates on the `linux` branch (feature branches merge
  into `linux`, not `main`); `linux` merges into `main` when a release ships
  it. Windows fixes made on `main` are merged back into `linux`
- The website lives in `site/` (deployed to https://npad.dev by Pages); the
  signed apt/dnf repos live in the separate `platima/npad-packages` repo
  (https://pkg.npad.dev)

**Versioning & changelog conventions (every change):**
- Bump the version per semver in `src/main.h` AND `src/platform/npad.rc`
  (0.x: minor = feature rounds / breaking changes, patch = fix-only rounds),
  **once per release cycle**, measured from the latest release tag
  (`git describe --tags --abbrev=0`):
  - If the branch's version is still the latest release's, bump it (minor or
    patch as above) and open a new `CHANGELOG.md` heading for it.
  - If it is already above the latest release, this cycle has its number:
    do NOT bump again. Add your notes under that existing heading, and raise
    patch to minor only if this round adds features to a fix-only cycle.
  - Work rounds on `linux` and feature branches are not releases. Bumping per
    round there produced 0.34, 0.35 and 0.36 after v0.33.0, all unreleased,
    which had to be renumbered back to v0.34.0 before the release.
- Add curated notes to `CHANGELOG.md` under the version heading (the
  commit-level history lives in `git log`, so there is no separate changes file)
- Every version entry opens with a `### ✨ Highlights` list before its other
  sections. This is the release announcement: 3-6 bullets on what a user
  would notice, in plain language, naming the platform when a change is not
  on both, with fixes included where users hit them. Keep it current as rounds
  land in the cycle. `scripts/release-notes.sh` publishes it as the GitHub
  release notes, and the release fails before anything is published if it is
  missing.
- Document any new setting/shortcut/behavior in `DOCUMENTATION.md`

The codebase is currently in early development with Windows Win32 implementation being the primary focus.