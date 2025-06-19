---
applyTo: '**'
---
## Project Overview
You are assisting with **npad**, a lightweight, cross-platform text editor inspired by classic Windows Notepad. The project prioritizes simplicity, speed, and minimal resource usage while adding essential quality-of-life improvements.

## Core Philosophy & Design Principles
- **Simplicity First**: Recreate beloved Windows Notepad simplicity with minimal, essential enhancements
- **Performance Critical**: Native API implementation, minimal overhead, tiny memory footprint
- **No Bloat Policy**: Resistance to feature creep and unnecessary complexity
- **Native Over Frameworks**: Direct platform APIs for maximum performance
- **Zero Dependencies**: Self-contained executable with no external requirements

## Architecture
```
Core Logic (platform-independent)
    ↓
UI Interface Layer (abstraction)
    ↓
Platform Implementation (Win32/Cocoa/X11/ncurses)
```

## Platform Support Status
- **Windows**: ✅ Active Development (Win32 API) - PRIMARY FOCUS
- **macOS**: 🚧 Planned (Cocoa)
- **Linux**: 🚧 Planned (X11 & Wayland)
- **Terminal**: 🚧 Planned (ncurses, Windows Console API)

## Absolutely Forbidden Features (99-100% locked out)
- 🚫 AI integration (100% forbidden)
- 🚫 Spellcheck
- 🚫 Syntax highlighting
- 🚫 Bloated frameworks (preferably not even .NET)
- 🚫 Rich text editing
- 🚫 Forced features
- 🚫 Automatic updates
- 🚫 Background services

## Features Under Consideration (may be added)
- 🎨 Optional Solarized coloring
- 🔌 Optional plugin support
- 📃 Optional native Markdown support
- 🔍 Optional RegEx in find/replace
- ⌨️ Optional custom keyboard shortcuts

## Current Development Status
See Project README.md

## Technical Requirements
- **Build System**: Makefile-based with cross-compilation support
- **Languages**: C/C++ for native performance
- **Windows**: MinGW-w64 or Visual Studio Build Tools
- **Settings**: JSON format in standard system locations
- **Versioning**: Follows Semantic Versioning (SemVer)

## Key Quality Standards
1. Must match notepad.exe functionality almost 1:1 at the starting point / core
2. Lightning fast startup and operation
3. Minimal memory footprint
4. Cross-platform compatibility through abstraction layer
5. No external dependencies in final executable

## Distribution Goals
- Portable EXE (primary)
- EXE Installer
- MSI Package
- Winget integration
- Optional 'notepad' command replacement
- RPM and similar repositories for Linux

## When Providing Assistance
- Use Australian English in all output
- Prioritise performance and simplicity over features
- Suggest native API solutions over framework approaches
- Consider cross-platform implications in design decisions
- Maintain focus on core text editing functionality
- Respect the "no bloat" philosophy
- Always consider Windows implementation first (primary platform)
- Think about memory usage and startup time impact
- Ensure suggestions align with classic Notepad behavior
- Code format must abide by the rules in .clang-format
- Do not add "// FIXED" comments to code

## Files & Structure Context
- Settings: JSON format in `%APPDATA%\Platima\npad\settings.json` (Windows)
- Build: Uses Makefile with `make windows`, `make linux`, etc.
- VS Code integration available with C/C++ extension
- GitHub Actions for CI/CD with automated releases

## Author Information
- **Author**: Platima
- **License**: MIT
- **Repository**: https://github.com/platima/npad
- **Sponsorship**: Available via GitHub Sponsors