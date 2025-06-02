# Contributing to npad 🤝

Thank you for your interest in contributing to npad! This document provides guidelines and information for contributors.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Development Setup](#development-setup)
- [Building and Testing](#building-and-testing)
- [Code Style](#code-style)
- [Submitting Changes](#submitting-changes)
- [Platform-Specific Development](#platform-specific-development)
- [Release Process](#release-process)

## Code of Conduct

- 🎯 **Stay focused** - Keep contributions aligned with npad's minimalist philosophy
- 🚫 **No bloat** - Resist feature creep and unnecessary complexity
- ⚡ **Performance first** - Optimize for speed and minimal resource usage
- 📝 **Document changes** - Clear commit messages and code comments
- 🤝 **Be respectful** - Constructive feedback and collaboration

## Development Setup

### Prerequisites

#### Linux (Recommended for development)
```bash
# Run the setup script to install all dependencies
./scripts/setup-runner.sh
```

Or manually install:
```bash
sudo apt-get install build-essential gcc-mingw-w64 libx11-dev cppcheck clang-format
```

#### Windows
- MinGW-w64 or Visual Studio Build Tools
- Git for Windows

#### macOS
- Xcode Command Line Tools

### Getting Started

1. **Fork and clone the repository:**
   ```bash
   git clone https://github.com/platima/npad
   cd npad
   ```

2. **Set up your development environment:**
   ```bash
   # Install dependencies (Linux)
   ./scripts/setup-runner.sh
   
   # Test the build system
   make help
   make lint
   make windows-x64
   ```

3. **Open in VS Code (recommended):**
   ```bash
   code .
   ```
   - Use Ctrl+Shift+P → "Tasks: Run Task" → "build windows"
   - Debug with F5 (requires debug build)

## Building and Testing

### Build Commands

```bash
# Show all available targets
make help

# Build for current platform
make

# Cross-compile for Windows
make windows-x64    # 64-bit Windows
make windows-x86    # 32-bit Windows

# Native builds
make linux          # Linux
make macos          # macOS (when implemented)

# Debug builds
make debug
make debug-windows

# Clean build artifacts
make clean
```

### Code Quality

```bash
# Run linting
make lint

# Check code formatting
make format-check

# Auto-format code
make format

# All quality checks
make lint && make format-check
```

### Testing

Currently testing is done through:
1. **Build verification** - All platforms must compile cleanly
2. **Manual testing** - Run the executable and verify functionality
3. **CI/CD validation** - GitHub Actions run automated checks

## Code Style

npad uses a consistent C coding style enforced by clang-format:

### Key Style Points
- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters maximum
- **Braces**: Linux style (opening brace on same line for functions)
- **Naming**: `snake_case` for functions and variables
- **Comments**: Clear, concise documentation

### Example Code Style
```c
/*
 * Function documentation
 * Brief description of what the function does
 */
bool editor_save_file(void)
{
    if (!g_editor.current_file) {
        // Handle case where no file is open
        return editor_save_file_as("Untitled.txt");
    }
    
    char* content = ui_get_text(g_editor.main_window);
    if (!content) {
        return false;
    }
    
    // Save and cleanup
    bool success = file_write_text(g_editor.current_file, content);
    free(content);
    
    return success;
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

1. **Create a feature branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes following the style guide**

3. **Test thoroughly:**
   ```bash
   make lint
   make format-check
   make clean && make windows-x64
   make clean && make linux
   ```

4. **Commit with clear messages:**
   ```bash
   git commit -m "feat: add auto-save functionality
   
   - Implement auto-save timer with configurable interval
   - Add settings for enabling/disabling auto-save
   - Update UI to show auto-save status"
   ```

5. **Push and create pull request:**
   ```bash
   git push origin feature/your-feature-name
   ```

### Commit Message Format

Use conventional commits for consistency:
- `feat:` New features
- `fix:` Bug fixes
- `docs:` Documentation changes
- `style:` Code style changes (formatting, etc.)
- `refactor:` Code refactoring
- `test:` Adding or modifying tests
- `build:` Build system changes

### What to Include in PRs
- 📝 Clear description of changes
- 🧪 Testing steps or evidence
- 📚 Documentation updates if needed
- 🎯 Single focus per PR (avoid mixing unrelated changes)

## Platform-Specific Development

### Windows (Win32)
- **Primary implementation** in `src/platform/ui_win32.c`
- Uses native Win32 API for maximum performance
- Handle Windows-specific features (registry, file associations)

### Linux (X11)
- **Future implementation** in `src/platform/ui_x11.c`
- Currently contains stubs - contributions welcome!
- Should support both X11 and Wayland

### macOS (Cocoa)
- **Future implementation** in `src/platform/ui_cocoa.m`
- Use native Cocoa APIs
- Follow macOS Human Interface Guidelines

### Terminal (ncurses)
- **Future implementation** in `src/platform/ui_ncurses.c`
- Cross-platform terminal support
- Keyboard-driven interface

### Adding Platform Support

1. Implement all functions in `ui_interface.h`
2. Update `Makefile` with platform-specific targets
3. Add CI/CD build support
4. Update documentation

## Release Process

### Automated Releases

Releases are automated via GitHub Actions:

1. **Create and push a tag:**
   ```bash
   # Use the release script (recommended)
   ./scripts/release.sh v1.2.3
   
   # Or manually
   git tag -a v1.2.3 -m "Release v1.2.3"
   git push origin v1.2.3
   ```

2. **GitHub Actions automatically:**
   - Runs linting and builds
   - Cross-compiles for all platforms
   - Creates GitHub release with binaries
   - Updates release badges

### Version Numbers

npad follows [Semantic Versioning](https://semver.org/):
- `v1.0.0` - Major version (breaking changes)
- `v1.1.0` - Minor version (new features)
- `v1.1.1` - Patch version (bug fixes)

### Pre-release Checklist

Before creating a release:
- [ ] All tests pass
- [ ] Code is formatted and linted
- [ ] Documentation is updated
- [ ] Version number follows SemVer
- [ ] CHANGELOG is updated (if exists)

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