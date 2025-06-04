# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

npad is a lightweight, cross-platform text editor inspired by classic Windows Notepad. It's built in C using native platform APIs (Win32, X11, Cocoa) for maximum performance and minimal resource usage. The project emphasizes simplicity and speed over feature richness.

## Build System

**Primary build commands:**
- `make` or `make all` - Auto-detect platform and build
- `make windows` - Build for Windows x64 using MinGW
- `make linux` - Build for Linux with X11
- `make debug` - Build with debug symbols enabled
- `make clean` - Clean build artifacts

**Code quality commands:**
- `make lint` - Run cppcheck static analysis (requires cppcheck)
- `make format` - Auto-format code with clang-format
- `make format-check` - Verify code formatting without changes

**Cross-compilation:**
The project is designed for cross-compilation from Linux to Windows using MinGW-w64. The setup script `./scripts/setup-runner.sh` installs necessary dependencies.

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
- `src/platform/` - Platform-specific UI implementations (ui_win32.c, ui_x11.c, etc.)
- `src/main.c` - Application entry point and initialization sequence

**State management:**
- `EditorState` struct in `src/core/editor.h` maintains editor state
- Settings stored as JSON in platform-appropriate locations
- Window state persistence for position/size across sessions

## Development Notes

**Code style:**
- C99 standard with -Wall -Wextra compiler flags
- clang-format configuration in `.clang-format`
- No external dependencies beyond system libraries

**Platform targets:**
- Windows: Win32 API (primary focus, most complete)
- Linux: X11 & Wayland (planned)  
- macOS: Cocoa (planned)
- Terminal: ncurses (planned)

**Key design principles:**
- Native APIs over frameworks
- Zero external dependencies
- Performance and minimal resource usage
- Classic Notepad simplicity with quality-of-life improvements

The codebase is currently in early development with Windows Win32 implementation being the primary focus.