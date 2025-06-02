# npad

A lightweight, cross-platform text editor inspired by the simplicity and speed of classic Windows Notepad. Built with native APIs for maximum performance and minimal resource usage.

## Philosophy

npad aims to recreate the beloved simplicity of original Windows Notepad while adding essential quality-of-life improvements. No AI integration, no spell check - just fast, reliable text editing.

## Features

- **Lightning fast** - Native API implementation with minimal overhead
- **Cross-platform** - Windows, macOS, Linux and CLI
- **Minimal resource usage** - Tiny memory footprint and fast startup
- **Classic interface** - Familiar Notepad-style UI and behavior
- **Easy install** - Installable by portable exe, or exe installer, with MSI, winget, and others planned
- **Easy launch** - Optionally 'notepad' in Windows
- **Familiar** - Default functionality matches notepad.exe almost 1:1, with the exception of dark/light mode
- **Quality-of-life enhancements:**
  - Optional auto-save (enabled by default, configurable)
  - Optional tabbed interface (disabled by default, configurable)
  - Resume session / crash protection (ddisabled by default, configurable)
  - Dark / light theme support (follows system theme by default, configurable)
  - Full undo / redo history
  - Improved find and replace functionality
  - Remembers window position and size
  - Multi-language support

_**Note: Windows support will be first, for the obvious reasons, as compile-yourself, a portable EXE, or as an EXE installer. Other platforms and installers will come with time or contributions**_

## Why

Quite simple really; I'll be forced to use Windows 11 at work in October, and the new Windows Notepad is bloated, slow, crashes, and suffers from the forced-injection of AI and other garbage features. Microsoft has recently annouced even more 'features' (read; bloat), thus I figure it's now a race

Whilst yes, you can remove the app redirect or uninstall it, it appears you cannot truly remove it and revert to the original notepad.

And yes, there are many great other options such as Sublime Text, Notepad++, NeoVim, and more, and I have used them all - and still do use some of them - they do not align with the notepad.exe design philosophy that npad is adopting.

If you want a 1:1 Windows 7 notepad.exe please see [Windows 7 Games for Windows 11 and Windows 10](https://win7games.com/).

## What npad is not

The below 'features' are 99% locked-in as not going to happen. 100% for the first one, actually. That does not mean that they could not be added via plugins later - if developed - but these items fall outside of my design goals for this.

- No AI integration
- No spellcheck
- No syntax highlighting
- No bloated frameworks, not even .Net (ideally, I may change my mind later)
- No rich text editing
- No forced features
- No automatic updates
- No background services

## What features will be considered

These items may or may not be added, and are essentially subject to my whim, but suggestions may be raised as an 'Issue'

- Optional Solarized colouring for dark and light modes
- Optional plugin support
- Optional RegEx in find and replace
- Optional custom keyboard shortcuts

## Platform Support

| Platform | Status | Backend |
|----------|--------|---------|
| Windows  | ✅ Active Development | Win32 API |
| macOS    | 🚧 Planned | Cocoa |
| Linux    | 🚧 Planned | X11/Wayland |
| *nix Terminal | 🚧 Planned | ncurses |
| Windows Terminal | 🚧 Planned | Windows Console API |

## Sponsor This Project

(CLAUDE FILL THIS IN)

## Get

### Pre-Built

Downloaded portable EXE or installer from the Releases page (CLAUDE TURN THIS INTO A LINK AND ADD MORE DETAIL)

### Compile Yourself

#### Prerequisites
- **Windows**: MinGW-w64 or Visual Studio Build Tools
- **macOS**: Xcode Command Line Tools
- **Linux**: GCC and development headers for X11

(CLAUDE FILL THIS IN: clone repo, compile with VS Code, etc)

## Use

### Running from Windows Run dialog
After installation, simply press `Win+R` and type `npad` to launch.

You can also type 'npad' in any File Explorer address bar, and npad will launch and show you an Open dialog in that location.

## Design Principles

1. **Native over frameworks** - Direct platform APIs instead of cross-platform UI libraries
2. **Performance first** - Optimized for speed and low resource usage
3. **Simplicity preserved** - Core Notepad functionality remains unchanged
4. **Quality enhancements only** - New features improve workflow without adding complexity
5. **No dependencies** - Self-contained executable with no external requirements

## Architecture

npad uses a clean abstraction layer that separates platform-specific UI code from core logic:

```
Core Logic (platform-independent)
    ↓
UI Interface Layer (abstraction)
    ↓
Platform Implementation (Win32/Cocoa/X11/ncurses)
```

## Contributing

Contributions are welcome! Please focus on:
- Performance optimizations
- Bug fixes
- Platform-specific implementations
- Quality-of-life improvements that maintain simplicity

## Roadmap

- [x] Project structure and build system
- [ ] Windows Win32 implementation
- [ ] Core text editing functionality
- [ ] File operations (New, Open, Save, Save As)
- [ ] Find/Replace dialog
- [ ] Settings system
- [ ] Auto-save feature
- [ ] Tabbed interface
- [ ] Theme support
- [ ] macOS Cocoa implementation
- [ ] Linux X11 implementation
- [ ] ncurses terminal implementation

## License

MIT License - See [LICENSE](LICENSE) file for details.

## Author

**Platima** - [GitHub Profile](https://github.com/platima)

---

*"Sometimes the best software is the software that gets out of your way."*
