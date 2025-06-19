# Changelog

All notable changes to npad will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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