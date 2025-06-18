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
- [License](#license)
- [Author](#author)

## Philosophy 💭

npad aims to recreate the beloved simplicity of original Windows Notepad while adding essential quality-of-life improvements. No AI integration, no spell check - just fast, reliable text editing.

## Features ✨

- **⚡ Lightning fast** - Native API implementation with minimal overhead
- **🌍 Cross-platform** - Windows, macOS, Linux and CLI
- **💨 Minimal resource usage** - Tiny memory footprint and fast startup
- **🎯 Classic interface** - Familiar Notepad-style UI and behavior
- **📦 Easy install** - Installable by portable exe, or exe installer, with MSI, winget, and others planned
- **🚀 Easy launch** - Optionally 'notepad' in Windows
- **👀 Familiar** - Default functionality matches notepad.exe almost 1:1, with the exception of dark/light mode
- **🔧 Quality-of-life enhancements:**
  - Optional auto-save (enabled by default, configurable)
  - Optional tabbed interface (disabled by default, configurable)
  - Resume session / crash protection (disabled by default, configurable)
  - Dark / light theme support (follows system theme by default, configurable)
  - Improved upport for differing line terminators (Windows, Linux, macOS)
  - JSON settings storage
  - Open large files without hanging
  - Easy monospace toggle
  - Additional keyboard shortcuts (Word Wrap, Go-To Line, Monospace, etc)
  - Line ending conversiosn
  - Full undo / redo history
  - Improved find and replace functionality
  - Remembers window position and size
  - Multi-language support

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
- 🚫 No automatic updates
- 🚫 No background services

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

## Get 📥

### Pre-Built

Download the portable EXE or installer from the [**Releases page**](https://github.com/platima/npad/releases).

**Available options:**
- 📦 **Portable EXE** - Just download and run, no installation required
- 🛠️ **EXE Installer** - Traditional Windows installer with registry integration
- 📋 **MSI Package** - Coming soon for enterprise deployment
- 🎁 **Winget** - Coming soon for `winget install npad`

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

## Use 🎯

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
  - [ ] File operations (New, Open, Save, Save As)
  - [ ] Initial menu items
  - [ ] Initial status bar
  - [ ] Zoom via Ctrl+/-, Ctrl Scroll
  - [ ] RTL/LTR support
  - [ ] High DPI support
  - [ ] Encoding options in Save Dialog
  - [ ] Drag and drop to open
  - [ ] Word wrap support (add Alt+Z shortcut)
  - [ ] Font chooser
  - [ ] Find/Replace dialog
- [ ] **Core Enhancements**
  - [ ] Settings system with JSON storage
  - [ ] Auto-save feature (enabled by default)
  - [ ] Session resume / crash protection (disabled by default)
  - [ ] Dark/light theme support (follow system by default)
  - [ ] Unlimited Undo & Redo
  - [ ] Improvements to Find & Replace dialogs
    - [ ] Recent list (configurable)
    - [ ] Show match count
    - [ ] 'Find Previous'
    - [ ] Wrap-around indicator
    - [ ] Correct \[tab\] order
  - [ ] Monospace toggle
    - [ ] Menu / Keyboard shortcut (Ctrl+M)
    - [ ] Status bar click
  - [ ] Zoom improvements
    - [ ] Menu / Keyboard shortcut (Ctrl+Z)
    - [ ] Status bar click
  - [ ] Go-To Line via
    - [ ] Menu / Keyboard shortcut (Ctrl+G)
    - [ ] Status bar click
  - [ ] Convert line endings between LF, CR LF, CR
    - [ ] Menu / Keyboard shortcut (Ctrl+E)
    - [ ] Status bar click
  - [ ] Ctrl+Drop to insert instead of open
  - [ ] 'Recent' menu (optional)
- [ ] **Advanced Features**
  - [ ] Tabbed interface (optional)
  - [ ] Multi-language support
  - [ ] Settings window
  - [ ] Full undo/redo history
  - [ ] Custom keyboard shortcuts
  - [ ] Solarized color schemes
- [ ] **Distribution & Installation**
  - [ ] Portable EXE build
  - [ ] Windows installer (EXE)
  - [ ] MSI package
  - [ ] Winget integration
  - [ ] Optional 'notepad' command replacement
- [ ] **Cross-Platform Expansion**
  - [ ] macOS Cocoa implementation
  - [ ] Linux X11/Wayland implementation
  - [ ] ncurses terminal implementation
  - [ ] Windows Terminal implementation
- [ ] **Future Considerations**
  - [ ] Plugin system evaluation
  - [ ] RegEx find/replace option

  
## Changelog 🛠

See [CHANGELOG.md](CHANGELOG.md) file for details.

## License 📄

MIT License - See [LICENSE](LICENSE) file for details.

## Author 👨‍💻

**Platima** - [GitHub Profile](https://github.com/platima)

---

*"Sometimes the best software is the software that gets out of your way."* ✨
