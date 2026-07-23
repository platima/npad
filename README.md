<img align="right" src="https://visitor-badge.laobi.icu/badge?page_id=platima.npad" height="20" />

# npad 📝

[![Build Status](https://github.com/platima/npad/workflows/CI/badge.svg)](https://github.com/platima/npad/actions/workflows/ci.yml)
[![Release](https://github.com/platima/npad/workflows/Release/badge.svg)](https://github.com/platima/npad/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/platima/npad)](https://github.com/platima/npad/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, cross-platform text editor inspired by the simplicity and speed of classic Windows Notepad. Built with native APIs for maximum performance and minimal resource usage.

**Versioning**: This project follows [Semantic Versioning](https://semver.org/) (SemVer) for predictable and reliable version management.

## Table of Contents
- [Philosophy](#philosophy)
- [Features](#features)
- [Why](#why)
- [What npad is not](#what-npad-is-not)
- [What will be considered](#what-features-will-be-considered)
- [Configuration](#configuration)
- [Platform Support](#platform-support)
- [Sponsor This Project](#sponsor-this-project)
- [Get npad](#get-npad)
- [Use npad](#use-npad)
- [Design Principles](#design-principles)
- [Architecture](#architecture)
- [Contributing](#contributing)
- [Roadmap](#roadmap)
- [Acknowledgements](#acknowledgements)
- [License](#license)
- [Author](#author)

## Philosophy 💭

npad aims to recreate the beloved simplicity of original Windows Notepad while adding essential quality-of-life improvements. No AI integration, no spell check - just fast, reliable text editing. It's like notepad, but quicker and easier. (And yes, the name is **npad**, always lowercase.)

**Core principle: out of the box, npad behaves as close to Windows 10 notepad.exe as possible.** Enhancements are judged by one rule:

- **Non-destructive enhancements that don't change the original core behaviour are ON by default** - e.g. crash recovery (it never touches your file and doesn't alter how editing works), the status bar, recent files, atomic saves.
- **Anything that alters or could destroy that original behaviour is OFF by default and opt-in** - e.g. auto-save (it overwrites your file from a timer), following the system colour scheme (notepad is always light), and the Markdown list tools (they change what Tab/Enter/Ctrl+X do).

## Features ✨

Current features (Windows build):

- **⚡ Lightning fast** - Native API implementation with minimal overhead
- **💨 Minimal resource usage** - Tiny memory footprint and fast startup
- **🎯 Classic interface** - Familiar Notepad-style UI and behavior
- **🌍 Unicode throughout** - Detects and preserves UTF-8, UTF-8 BOM, UTF-16 LE/BE and ANSI encodings
- **↩️ Line ending aware** - Detects and preserves Windows (CRLF), Unix (LF) and Mac (CR) line endings, shown in the status bar
- **🔧 Quality-of-life enhancements:**
  - Tabbed Preferences dialog (Edit menu, Ctrl+,) with an Apply button: General, Appearance, Defaults, Markdown, Backup and Updates pages (see [DOCUMENTATION.md](DOCUMENTATION.md) for every setting)
  - Optional auto-save (disabled by default - it overwrites your file, so it's opt-in; configurable)
  - Session resume / crash recovery (enabled by default - non-destructive, snapshots never touch your file); restores every window that was open, each in its own instance
  - Theme support: light (default), dark, follow-system, and Solarized Light / Dark colour schemes (chosen in Preferences); changes apply to all open windows live
  - Opens at a large default sized to your display (remembered once you resize)
  - Find / Replace with direction, match case, whole word, wrap-around and an optional highlight-all-matches overlay (Ctrl+F / Ctrl+H, F3 / Shift+F3)
  - Find remembers recent search / replace terms, and shows a live match count
  - Undo / Redo (Ctrl+Z / Ctrl+Y) with a deep undo history
  - Zoom (Ctrl+Plus / Ctrl+Minus / Ctrl+0, Ctrl+Scroll)
  - Word wrap toggle (Alt+Z), Go To Line (Ctrl+G), Time/Date (F5)
  - Line ending conversion (Format menu, Ctrl+E to cycle, or click the status bar)
  - Encoding picker in the Save dialog (or click the status bar); warns before an ANSI save would lose characters
  - Right-click context menu (Undo/Redo/Cut/Copy/Paste/Delete/Select All)
  - Status bar click actions: Ln/Col opens Go To, zoom resets, font mode toggles monospace, line ending and encoding open pickers
  - New Window (Ctrl+Shift+N) opens a second independent instance; a Preferences option can make Ctrl+N do this instead
  - Close (Ctrl+W) closes the current window, Close All Windows (Ctrl+Shift+W) closes them all - each save-checked
  - Separate monospace and proportional fonts, each with its own picker; font type and zoom are per-window (Ctrl+M / Ctrl+Scroll), with configurable defaults, a 'Use Current' capture, and optional live sync across windows or auto-updating defaults
  - OpenDyslexic font option for reading assistance (Preferences > Appearance; requires the font to be installed)
  - Optional **Markdown list tools** (Preferences > Markdown, off by default): Sort and Unique lines, Convert Delimiters (a find/replace for delimiters with `\n`/`\t`/`\uXXXX` escapes), Indent/Unindent (Tab / Shift+Tab on a selection, or Ctrl+] / Ctrl+[ by preference) with markdown-style markers incl. a custom prefix, Enter continuing list markers, and cut-line/paste-above on Ctrl+X/Ctrl+V - in a Markdown menu and the right-click menu
  - Optional escape interpretation (`\n`, `\t`, `\uXXXX`, ...) in Find / Replace via a checkbox in the dialogs
  - Recent files menu (size configurable)
  - Drag-and-drop to open files; Ctrl+Drop inserts the file at the caret
  - Export / Import settings (Preferences > Backup) for backup or moving between machines
  - Atomic saves - a failed save can never destroy the existing file
  - JSON settings storage; remembers window position, size and maximized state
  - Confirmation prompt before opening very large files (threshold scales with the system's RAM by default)
  - Binary-file detection on open, offering Cancel / Open in npad / Open with the default app
  - Optional word / character / line counts in the status bar (off by default, debounced so large edits stay smooth)
  - Check for Updates (Help menu) with an opt-in Updates preferences tab: off by default, or notify silently (a Help-menu dot), prompt, or download-and-install-automatically; optional launch check, Skip this version, SHA-256-verified download

Planned (see the [Roadmap](#roadmap)):

- 🚧 macOS, Linux (X11/Wayland) and terminal builds
- 🚧 Optional tabbed interface
- 🚧 Multi-language support
- 🚧 Custom keyboard shortcuts
- 🚧 Portable EXE, installer, MSI and winget distribution

_**Note: Windows support will be first, for the obvious reasons, as compile-yourself, a portable EXE, or as an EXE installer. Other platforms and installers will come with time or contributions**_

## Why 🤔

Quite simple really; I'll be forced to use Windows 11 at work in October, and the new Windows Notepad is bloated, slow, crashes, and suffers from the forced-injection of AI and other garbage features. Microsoft has recently announced even more 'features' (read; bloat), thus I figure it's now a race

Whilst yes, you can remove the app redirect or uninstall it, it appears you cannot truly remove it and revert to the original notepad.

And yes, there are many great other options such as Sublime Text, Notepad++, NeoVim, and more, and I have used them all - and still do use some of them - they do not align with the notepad.exe design philosophy that npad is adopting.

If you want a 1:1 Windows 7 notepad.exe please see [Windows 7 Games for Windows 11 and Windows 10](https://win7games.com/).

## What npad is not ❌

The below 'features' are 99% locked-in as not going to happen. 100% for the first one, actually. That does not mean that they could not be added via plugins later - if developed - but these items fall outside of my design goals for this.

- 🚫 No AI integration
- 🚫 No spellcheck
- 🚫 No syntax highlighting
- 🚫 No bloated frameworks, not even .Net (ideally, I may change my mind later)
- 🚫 No rich text editing
- 🚫 No forced features
- 🚫 No *forced* or silent updates — update checking is off by default; an opt-in auto-install mode exists but still asks once before running the installer
- 🚫 No background services — the only optional network call is an on-launch update check you have to turn on

## What features will be considered 🤷

These items may or may not be added, and are essentially subject to my whim, but suggestions may be raised as an 'Issue'

- 🎨 Optional Solarized colouring for dark and light modes
- 🔌 Optional plugin support
- 📃 Optional native Markdown support
- 🔍 Optional RegEx in find and replace
- ⌨️ Optional custom keyboard shortcuts

## Configuration 🔧

npad stores its settings in JSON format in standard system locations:
- **Linux**: `~/.config/npad/settings.json`
- **Windows**: `%APPDATA%\Platima\npad\settings.json`
- **macOS**: `~/Library/Application Support/npad/settings.json` (when implemented)

Settings include window position, auto-save preferences, theme selection, and other user preferences. All settings are optional - npad works perfectly with default values.

## Platform Support 🖥️

| Platform | Status | Backend |
|----------|--------|---------|
| Windows  | ✅ Active Development | Win32 API |
| macOS    | 🚧 Planned | Cocoa |
| Linux    | 🚧 Planned | X11 & Wayland |
| *nix Terminal | 🚧 Planned | ncurses |
| Windows Terminal | 🚧 Planned | Windows Console API |

## Sponsor This Project 💝

Love the project? Consider sponsoring its development! Your support helps dedicate more time to making npad the best lightweight text editor it can be.

**[💖 Sponsor on GitHub](https://github.com/sponsors/platima)**

Every sponsorship, no matter the size, is deeply appreciated and goes directly toward:
- 🚀 Faster development and new features
- 🐛 Bug fixes and stability improvements
- 📚 Better documentation and guides
- 🌍 Cross-platform support expansion

Your support makes a real difference in keeping this project alive and thriving! ✨

## Get npad 📥

### Pre-Built

Download from the [**Releases page**](https://github.com/platima/npad/releases):

- 🛠️ **`npad-v<version>-setup-win-x64.exe`** - Windows installer. Per-user by
  default (no admin needed); system-wide when run elevated or chosen in the
  dialog. Optional bundled fonts (Intel One Mono, Roboto, OpenDyslexic), file
  associations, and a 'notepad' alias. Silent: `/VERYSILENT [/ALLUSERS]`.
- 📋 **`npad-v<version>-msi-win-x64.msi`** - For silent/enterprise deployment:
  `msiexec /i npad-v<version>-msi-win-x64.msi /qn` (add `ALLUSERS=1` for
  machine-wide; features selectable via `ADDLOCAL`).
- 📦 **`npad-v<version>-portable-win-x64.exe`** - Portable: download and run, no
  installation required.

See [DOCUMENTATION.md](DOCUMENTATION.md#installation-windows) for the full
installer reference (tasks, features, fonts, the Windows 11 notepad alias).

> **SmartScreen / Defender blocking the download?** Releases are not
> code-signed yet, so Windows may block the installer or portable exe.
> Right-click the downloaded file → **Properties** → tick **Unblock** →
> **Apply** (or choose "More info" → "Run anyway" on the SmartScreen
> prompt). Verify the SHA256 against `CHECKSUMS.txt` first if unsure.

**Planned:** 🎁 **Winget** - for `winget install npad`.

### Compile Yourself

#### Prerequisites
- **Windows**: MinGW-w64 or Visual Studio Build Tools
- **macOS**: Xcode Command Line Tools  
- **Linux**: GCC and development headers for X11

#### Build Instructions

1. **Clone the repository:**
   ```bash
   git clone https://github.com/platima/npad.git
   cd npad
   ```

2. **Install dependencies (Linux):**
   ```bash
   # Automated setup (recommended)
   ./scripts/setup-runner.sh
   
   # Or manually install
   sudo apt-get install build-essential gcc-mingw-w64 libx11-dev
   ```

3. **Build for your platform:**
   ```bash
   # Windows (cross-compile from Linux)
   make windows        # Windows x64 build
   
   # Native Linux
   make linux
   
   # macOS (when implemented)
   make macos
   
   # See all options
   make help
   ```

4. **Development with VS Code:**
   - Open the project folder in VS Code
   - Install the C/C++ extension
   - Use Ctrl+Shift+P → "Tasks: Run Task" → "build windows"
   - Debug with F5 (requires `make debug`)

5. **Code quality checks:**
   ```bash
   make lint           # Run static analysis
   make format-check   # Verify code formatting
   make format         # Auto-format code
   ```

## Use npad 🎯

### Running from Windows Run dialog
After installation, simply press `Win+R` and type `npad` to launch.

You can also type 'npad' in any File Explorer address bar, and npad will launch and show you an Open dialog in that location.

## Design Principles 🏗️

1. **🔧 Native over frameworks** - Direct platform APIs for maximum performance
2. **⚡ Performance first** - Optimized for speed and minimal resource usage  
3. **🎯 Simplicity preserved** - Core functionality stays true to classic Notepad
4. **✨ Quality enhancements only** - New features improve workflow without complexity
5. **📦 Zero dependencies** - Self-contained executable with no external requirements
6. **🚫 No bloat** - Resistance to feature creep and unnecessary additions

## Architecture 🏛️

npad uses a clean abstraction layer that separates platform-specific UI code from core logic:

```
Core Logic (platform-independent)
    ↓
UI Interface Layer (abstraction)
    ↓
Platform Implementation (Win32/Cocoa/X11/ncurses)
```

## Contributing 🤝

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

**Focus areas:**
- ⚡ Performance optimizations
- 🐛 Bug fixes  
- 🖥️ Platform-specific implementations
- ✨ Quality-of-life improvements that maintain simplicity

**Development:**
- 🔧 Self-hosted CI/CD with cross-compilation
- 🧪 Automated testing and linting
- 📦 Automated releases via GitHub Actions
- 🎯 Code formatting and quality checks

## Roadmap 🗺️

- [x] **Setup**
  - [x] GitHub repository
  - [x] Project structure, README, etc
  - [x] Basic code structure
  - [x] Build, test & release pipelines
- [ ] **Initial Notepad features**
  - [x] Initial launchable Win32 application
  - [x] Core text editing functionality
  - [x] File operations (New, Open, Save, Save As)
  - [x] Initial menu items
  - [x] Initial status bar
  - [x] Zoom via Ctrl+/-, Ctrl Scroll
  - [ ] RTL/LTR support
  - [x] High DPI support
  - [x] Encoding options in Save Dialog
  - [x] Drag and drop to open
  - [x] Word wrap support (add Alt+Z shortcut)
  - [x] Font chooser
  - [x] Find/Replace dialog
- [ ] **Core Enhancements**
  - [x] Settings system with JSON storage
  - [x] Auto-save feature (enabled by default)
  - [x] Session resume / crash protection (disabled by default)
  - [x] Dark/light theme support (follow system by default)
  - [x] Deep Undo & Redo history (100,000 actions)
  - [x] Improvements to Find & Replace dialogs
    - [x] Recent list (configurable)
    - [x] Show match count
    - [x] 'Find Previous'
    - [x] Wrap-around option and indicator
    - [x] Correct \[tab\] order
  - [x] Monospace toggle
    - [x] Menu / Keyboard shortcut (Ctrl+M)
    - [x] Status bar click
  - [x] Zoom improvements
    - [x] Menu / Keyboard shortcuts (Ctrl+Plus / Ctrl+Minus / Ctrl+0)
    - [x] Status bar click (resets to 100%)
  - [x] Go-To Line via
    - [x] Menu / Keyboard shortcut (Ctrl+G)
    - [x] Status bar click
  - [x] Convert line endings between LF, CR LF, CR
    - [x] Detect and preserve line endings (shown in status bar)
    - [x] Menu / Keyboard shortcut (Ctrl+E)
    - [x] Status bar click
  - [x] Ctrl+Drop to insert instead of open
  - [x] 'Recent' menu (optional)
  - [x] New Window (Ctrl+Shift+N; configurable Ctrl+N behaviour)
  - [x] Separate monospace / proportional fonts
  - [x] OpenDyslexic font option (reading assistance)
  - [x] Config backup / export & import
  - [x] Optional Markdown list tools (Sort, Unique, Convert Delimiters, Indent/Unindent incl. custom prefix, Enter list continuation, cut line/paste above; off by default)
  - [x] Escape interpretation option in Find / Replace
  - [x] Binary-file detection on open (Cancel / Open in npad / Open with the default app)
  - [x] All dialogs open at a consistent notepad-style offset into the window
  - [x] 'Highlight all' matches in Find / Find & Replace
  - [x] Optional word / character / line count in the status bar (off by default)
  - [x] Large-file lag warning proportional to the system's capabilities (default threshold scales with RAM)
  - [x] Self-updating: opt-in update modes (off by default; notify / prompt / auto), launch check, Skip this version, SHA-256-verified download
- [ ] **Advanced Features**
  - [ ] Tabbed interface (optional)
  - [ ] Multi-language support
  - [x] Settings window (tabbed Preferences dialog, Edit menu, Ctrl+,)
  - [x] Full undo/redo history
  - [ ] Custom keyboard shortcuts
  - [x] Solarized color schemes
- [ ] **Distribution & Installation**
  - [x] Portable EXE build (every release's `-portable-win-x64.exe` runs standalone)
  - [x] Windows installer (EXE)
    - [x] Bundle & offer open-source fonts (Intel One Mono, Roboto, OpenDyslexic), pre-setting them in the config
    - [x] Default to associating the `notepad` app execution alias on Windows 11
    - [x] On install, offer to open Settings to remove any existing `notepad` app execution alias (if it cannot be done programmatically)
  - [x] MSI package
  - [ ] Winget integration
  - [ ] Code-sign the release binaries and installers (removes the SmartScreen/Defender unblock step)
  - [x] Optional 'notepad' command replacement (App Paths alias task in both installers)
- [ ] **Cross-Platform Expansion**
  - [ ] macOS Cocoa implementation
  - [ ] Linux X11/Wayland implementation
  - [ ] ncurses terminal implementation
  - [ ] Windows Terminal implementation
- [ ] **Future Considerations**
  - [ ] Plugin system evaluation
  - [ ] RegEx find/replace option

  
## Documentation 📖

See [DOCUMENTATION.md](DOCUMENTATION.md) for every setting, keyboard
shortcut, status-bar action and behaviour.

## Changelog 🛠

See [CHANGELOG.md](CHANGELOG.md) for curated release notes; the commit-level
history lives in `git log`.

## Acknowledgements 🙏

- **Solarized** colour schemes by [Ethan Schoonover](https://ethanschoonover.com/solarized/).
- Fonts planned for bundling with the installer, under open licences:
  - [Intel One Mono](https://github.com/intel/intel-one-mono) (SIL Open Font License)
  - [Roboto](https://github.com/googlefonts/roboto-3-classic) (Apache License 2.0)
  - [OpenDyslexic](https://opendyslexic.org) (SIL Open Font License) - reading assistance

## License 📄

MIT License - See [LICENSE](LICENSE) file for details.

## Author 👨‍💻

**Platima** - [GitHub Profile](https://github.com/platima)

---

*"Sometimes the best software is the software that gets out of your way."* ✨
