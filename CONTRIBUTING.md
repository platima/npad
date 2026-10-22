# Contributing to npad 🤝

Thank you for your interest in contributing to npad! This document provides guidelines and information for contributors.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Development Setup](#development-setup)
- [Building and Testing](#building-and-testing)
- [Code Style](#code-style)
- [Submitting Changes](#submitting-changes)
- [Branches, Versions and the Changelog](#branches-versions-and-the-changelog)
- [Platform-Specific Development](#platform-specific-development)
- [Release Process](#release-process)

## Code of Conduct

- 🎯 **Stay focused** - Keep contributions aligned with npad's minimalist philosophy
- 🚫 **No bloat** - Resist feature creep and unnecessary complexity
- ⚡ **Performance first** - Optimize for speed and minimal resource usage
- 📝 **Document changes** - Clear commit messages and code comments
- 🤝 **Be respectful** - Constructive feedback and collaboration

**The core principle decides every default:** out of the box npad behaves
like Windows 10 notepad.exe. A non-destructive enhancement that does not
change that behaviour (crash recovery, the status bar, atomic saves) is on by
default; anything that alters it or could destroy data (auto-save, following
the system colour scheme, the Markdown tools) is off by default and opt-in.
The product name is always lowercase: **npad**.

## Development Setup

### Prerequisites

#### Linux (Recommended for development)
```bash
# Run the setup script to install all dependencies
./scripts/setup-runner.sh
```

The script installs the compilers, the MinGW-w64 cross compiler, the GTK 3
and ncurses development files, nfpm and llvm-mingw (for the Windows on Arm
build, in `/opt/llvm-mingw`). It prefers clang-format 18 and warns when it
cannot get it, or when cppcheck is not 2.13: those are the versions CI uses,
and other versions disagree with them.

Or manually install:
```bash
sudo apt-get install build-essential pkg-config gcc-mingw-w64 libgtk-3-dev cppcheck clang-format-18
```

If your distribution has no clang-format 18 package, `pip install --user
clang-format==18.1.8` gets it; then run `CLANG_FORMAT=~/.local/bin/clang-format
make format-check`.

#### Windows
- MinGW-w64 (MSYS2, or Git Bash with a MinGW toolchain) and `make`; the
  Makefile finds it by itself (override with `MINGW_CC`, `MINGW_WINDRES`,
  `MINGW_STRIP`)
- Git for Windows
- For the installers only: Inno Setup 6 and the WiX 6 dotnet tool
  (`dotnet tool install --global wix --version 6.0.2`)

#### macOS
- Xcode Command Line Tools

### Getting Started

1. **Fork and clone the repository:**
   ```bash
   git clone https://github.com/platima/npad.git
   cd npad
   ```

2. **Set up your development environment:**
   ```bash
   # Install dependencies (Linux)
   ./scripts/setup-runner.sh
   
   # Test the build system
   make help
   make test
   make lint
   make windows
   ```

3. **Open in VS Code (recommended):**
   ```bash
   code .
   ```
   - Use Ctrl+Shift+P → "Tasks: Run Task" → "build windows"
   - Debug with F5 (requires debug build)

Before changing a subsystem, read its section of [LESSONS.md](LESSONS.md):
it records what turned out to work differently than it looks (RichEdit line
handling, the installers, dark mode, tooling traps such as shell heredocs
mangling C escapes), and is worth adding to when you find another.

## Building and Testing

### Build Commands

```bash
# Show all available targets
make help

# Build for current platform
make

# Cross-compile for Windows
make windows        # Windows x64 build
make windows-arm64 LLVM_MINGW=/opt/llvm-mingw
                    # Windows on Arm (npad-arm64.exe). Or append (never
                    # prepend) llvm-mingw's bin/ to PATH: ahead of MinGW GCC
                    # it would also take over the x64 build.

# Native builds
make linux          # Linux desktop app (npad-linux), plus the ncurses stub
                    # when libncurses-dev is installed
make linux-gtk      # Just the desktop app (GTK 3, X11 and Wayland)
make macos          # macOS stub (when implemented)

# Debug builds
make debug
make debug-windows

# Warnings as errors, as CI builds
make windows linux-gtk WERROR=1

# Clean build artifacts
make clean
```

### Code Quality

```bash
# Run linting (cppcheck; CI uses 2.13, which is stricter than 2.10)
make lint

# Check code formatting (canonical against clang-format 18, as in CI)
make format-check

# Auto-format code
make format

# All quality checks
make lint && make format-check
```

`make format` and `make format-check` cover `src/` only.

### Testing

```bash
make test
```

`make test` builds and runs every unit suite: file operations and atomic
saves, errors, encodings and line endings, settings and Import Settings,
crash recovery, undo, Find, the editor core against a stub UI, printing
layout, the Markdown list tools and HTML paste, the update check and more.
It also runs a short seeded fuzz pass, the release scripts' self-test, and a
check that every stub backend (Cocoa, ncurses, Windows console) defines each
`ui_platform_*` function the shared code calls. Each suite runs in its own
temporary folder with its own settings location, so the tests never write
into the source tree or touch your real npad settings; `NPAD_TEST_KEEP=1`
keeps a suite's folder and prints its path.

| Target | What it does |
|--------|--------------|
| `make test-<suite>` | One suite, e.g. `make test-encoding` or `make test-editor` |
| `make test-windows` | Builds every suite as a Windows `.exe` with MinGW, and on Windows (MSYS2) runs them. The registry suite, `test_assoc_win32.exe`, writes a scratch HKCU key and runs only with `NPAD_ALLOW_REGISTRY_TESTS=1` |
| `make test-sanitize` | Every suite under AddressSanitizer and UBSan |
| `make test-fuzz` | The property/fuzz suite under the sanitizers. `FUZZ_ITERS` (default 5000) and `FUZZ_SEED` tune it; a failure prints the seed, the iteration and the input |
| `make test-richedit-win32`, `make test-win32-unicode` | Cross-compile Windows-only tests of the RichEdit text boundary and of Unicode command lines and paths, to run on a Windows test machine |

Under WSL, gcc's AddressSanitizer can loop on `AddressSanitizer:DEADLYSIGNAL`
before `main` runs; the Makefile runs sanitised suites under `setarch -R` for
you where it can.

What the unit tests cannot reach - dialogs, menus, painting, printing to a
real printer, installers - still needs a manual check on a real desktop.
Say in the pull request what you checked by hand. CI then builds every
platform with warnings as errors and runs the suites on Linux, on Windows x64
and on Windows on Arm.

### Developer scripts (Windows)

`scripts/measure-startup.ps1`, `probe-prefs.ps1` and `probe-installer.ps1`
start their own npad (or setup), touch only that process and the ones it
started, tracked by PID, close it gracefully, and report anything that would
not exit rather than killing it. The two that run npad give it a temporary
private profile, so your settings are left alone. `measure-startup.ps1`
refuses to run while npad is open unless you pass `-AllowRunning`;
`probe-installer.ps1` refuses `/SILENT` and `/VERYSILENT` and leaves the
wizard before anything is installed.

## Code Style

npad uses a consistent C coding style enforced by clang-format (`.clang-format`,
based on LLVM):

### Key Style Points
- **Language**: C99, built with `-Wall -Wextra`; CI adds `-Werror`
  (`WERROR=1`), so fix warnings rather than silencing them
- **Indentation**: 4 spaces (no tabs); `case` labels are indented inside `switch`
- **Line length**: 100 characters maximum
- **Braces**: attached (K&R): the opening brace stays at the end of the line,
  for functions as well as `if`, `for` and `while`
- **Pointers**: the `*` goes with the name: `char *content`, `const char *path`
- **Casts**: a space after the cast: `(int) len`
- **Naming**: `snake_case` for functions and variables, a `g_` prefix for
  globals, `UPPER_CASE` for macros
- **Comments**: Clear, concise documentation of why, not what
- **Dependencies**: none beyond system libraries; use the native platform
  APIs (wide `W` APIs on Windows, with UTF-8 in the core)

### Example Code Style
```c
// Copy text into a new buffer the caller frees; NULL if out of memory
static char *copy_text(const char *text, size_t *out_len) {
    size_t len = strlen(text);
    char *copy = (char *) malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, text, len + 1);
    if (out_len) {
        *out_len = len;
    }
    return copy;
}
```

### Automatic Formatting
```bash
# Format all code
make format

# Check formatting without changes
make format-check
```

## Submitting Changes

### Pull Request Process

1. **Create a feature branch** from the branch your change belongs on (see
   [Branches](#branches)):
   ```bash
   git checkout -b feature/your-feature-name linux
   ```

2. **Make your changes following the style guide**

3. **Test thoroughly:**
   ```bash
   make test
   make lint
   make format-check
   make clean && make windows WERROR=1
   make clean && make linux-gtk WERROR=1
   ```

4. **Commit with clear messages.** Write multi-line messages to a file and
   use `git commit -F <file>`: a `-m` message containing quotes is easily
   broken by the shell.

5. **Push and create a pull request** against the branch you started from:
   ```bash
   git push origin feature/your-feature-name
   ```

### Commit Message Format

The subject names the area, then says what changed: `Linux: Arch packages,
install tests and an AUR npad-bin package`, `Preferences: an Editor page`,
`Docs: ...`. The body explains what changed and why, wrapped at about 72
characters.

### What to Include in PRs
- 📝 Clear description of changes
- 🧪 Testing steps or evidence (what you ran, and what you checked by hand)
- 📚 Documentation updates: any new setting, shortcut or behaviour goes in
  [DOCUMENTATION.md](DOCUMENTATION.md), and a user-visible change gets a
  [CHANGELOG.md](CHANGELOG.md) entry
- 🎯 Single focus per PR (avoid mixing unrelated changes)

## Branches, Versions and the Changelog

### Branches

- **`main`** takes only releases and Windows fixes. Every push to it runs the
  full CI build, so it is never the place for work in progress. It is
  protected: changes land only through a pull request, so even a one-line fix
  goes branch, then pull request, then merge.
- **`linux`** is where Linux port work integrates: feature branches merge
  into `linux`, not `main`, and `linux` merges into `main` when a release
  ships it. Windows fixes made on `main` are merged back into `linux`.
- The website lives in [`site/`](site/) and is deployed to <https://npad.dev>
  from `main`.

### Version numbers

npad follows [Semantic Versioning](https://semver.org/). While it is 0.x, a
**minor** bump (`0.36.0`) is a feature round or a breaking change and a
**patch** bump (`0.36.1`) is a fix-only round.

The version lives in two places that must agree: `src/main.h` and
`src/platform/npad.rc` (CI checks this on every change). Bump it **once per
release cycle**, measured from the latest release tag (`git describe --tags
--abbrev=0`):

- If the branch's version is still the latest release's, bump it (minor or
  patch as above) and open a new `CHANGELOG.md` heading for it.
- If it is already above the latest release, this cycle has its number: do
  not bump again. Add your notes under that heading, and raise a patch bump to
  minor only if your change adds a feature to a fix-only cycle.
- Work rounds on `linux` and on feature branches are not releases; they do
  not get a number each.

### The changelog

Add curated notes to [CHANGELOG.md](CHANGELOG.md) under the version heading
(the commit-level history lives in `git log`). Every version entry opens with
a `### ✨ Highlights` list: three to six bullets on what a user would notice,
in plain language, naming the platform when a change is not on both. It is
published as the GitHub release notes, so keep it current as changes land;
a release without it fails before anything is published.

## Platform-Specific Development

### Windows (Win32)
- **Primary implementation** in `src/platform/ui_win32.c`
- Uses native Win32 API (the wide `W` functions) for maximum performance;
  the core passes UTF-8 and the Win32 layer converts at the API boundary
- Handle Windows-specific features (registry, file associations)

### Linux (GTK 3: X11 & Wayland)
- **Beta** in `src/platform/ui_gtk*.c`: every Windows feature, one binary for
  both display servers, now being tested on real desktops
- [LINUX_PORT.md](LINUX_PORT.md) has the conventions, the testing recipe and
  the deliberate differences

### macOS (Cocoa)
- **Future implementation** in `src/platform/ui_cocoa.m` (a stub today)
- Use native Cocoa APIs
- Follow macOS Human Interface Guidelines

### Terminal (ncurses)
- **Future implementation** in `src/platform/ui_ncurses.c` (Unix) and
  `src/platform/ui_win32_terminal.c` (Windows console), stubs today
- Cross-platform terminal support
- Keyboard-driven interface

### Adding Platform Support

1. Implement all functions in `ui_interface.h`
2. Update `Makefile` with platform-specific targets
3. Add CI/CD build support
4. Update documentation

A new `ui_platform_*` function needs a definition in every backend, the stubs
included: `make test` (`make test-platform-stubs`) fails until each one has it.

## Release Process

Releases are built, validated and published by GitHub Actions
(`.github/workflows/release.yml`) when a version tag is pushed to `main`.

1. **Check the commit is ready, locally:**
   ```bash
   ./scripts/release.sh v1.2.3
   ```
   It runs the version gate (`scripts/check-release-version.sh`: the tag is
   well formed, and `src/main.h`, `src/platform/npad.rc` and the newest
   `CHANGELOG.md` entry with its Highlights name the version; a final release
   also needs a dated entry, `## [1.2.3] - YYYY-MM-DD`, and the AppStream
   metainfo's newest `<release>` to be this version), checks that the checkout
   is a clean `main` level with `origin/main` and that the tag is new, then
   runs `make lint`, `format-check`, `test`, `windows` and `linux-gtk` with
   `WERROR=1`. It tags nothing: it prints the dry-run and tag commands.

2. **Dry run** (builds, signs and validates every asset, publishes nothing):
   ```bash
   gh workflow run release.yml --ref main -f version=v1.2.3
   ```

3. **Tag and push**, with `./scripts/release.sh v1.2.3 --push` (asks first)
   or by hand:
   ```bash
   git tag -a v1.2.3 -m "Release v1.2.3"
   git push origin v1.2.3
   ```

4. **GitHub Actions then:**
   - runs the same version gate before anything is built
   - builds and tests the Windows downloads (x64 and arm64, code-signed) and
     the Linux ones (x86_64, aarch64, riscv64)
   - creates the release as a **draft**, attaches every file with its
     `.sha256` and `CHECKSUMS.txt`, validates the set, and only then
     publishes it, so the in-app updater never sees a half-uploaded release.
     A failed run leaves an unpublished draft, which a re-run of the same tag
     reuses (delete it to start over); a tag that is already published is
     refused.

The download names are an API (the in-app updater finds its installer by the
end of the name): see "Release asset names" in [CLAUDE.md](CLAUDE.md), and
never rename a Windows download.

### Pre-release Checklist

Before tagging a release:
- [ ] `make test`, `make lint` and `make format-check` pass, and the Windows
      and Linux builds are clean with `WERROR=1`
- [ ] The version is bumped once for the cycle in `src/main.h` and
      `src/platform/npad.rc`
- [ ] `CHANGELOG.md` has the version's entry, dated for a final release, with
      a current `### ✨ Highlights` list
- [ ] For a final release, the first `<release>` in
      `packaging/linux/io.github.platima.npad.metainfo.xml` is this version,
      with the same date as the `CHANGELOG.md` entry
- [ ] [DOCUMENTATION.md](DOCUMENTATION.md) and the [README](README.md)
      describe every new setting, shortcut and behaviour
- [ ] **npad.dev is current.** The site in [`site/`](site/) deploys from
      `main`, so review it against DOCUMENTATION.md and the Highlights before
      the release reaches `main`: the download names and architectures on
      `site/download/`, each platform's status and features on the home and
      features pages, and the shortcuts, command line, file locations and
      Preferences pages on `site/docs/`
- [ ] The dry run passed

## Getting Help

- 💬 **Discussions**: Use GitHub Discussions for questions
- 🐛 **Issues**: Report bugs via GitHub Issues
- 📧 **Contact**: Reach out to @platima for major contributions

## Recognition

Contributors will be recognized in:
- GitHub contributors list
- Release notes for significant contributions
- Special mention for platform implementations

---

Thank you for helping make npad better! 🎉
