# npad Makefile
# Cross-platform text editor

# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS =

# Platform detection
UNAME_S := $(shell uname -s 2>/dev/null || echo "Windows")
UNAME_M := $(shell uname -m 2>/dev/null || echo "unknown")

# Cross-compilation support
ifdef CROSS_COMPILE
CC = $(CROSS_COMPILE)gcc
endif

# Windows toolchain: cross-compile from Linux by default, or use the native
# MinGW toolchain when building on Windows (MSYS2 / Git Bash)
ifeq ($(OS),Windows_NT)
MINGW_CC ?= gcc
MINGW_WINDRES ?= windres
MINGW_STRIP ?= strip
else
MINGW_CC ?= x86_64-w64-mingw32-gcc
MINGW_WINDRES ?= x86_64-w64-mingw32-windres
MINGW_STRIP ?= x86_64-w64-mingw32-strip
endif

# Source files
CORE_SOURCES = src/core/editor.c src/core/file_ops.c src/core/settings.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/startup_prof.c src/core/list_ops.c
SHARED_SOURCES = src/ui_interface.c

# Test sources
TEST_FRAMEWORK_SOURCES = tests/test_framework.c
TEST_CORE_SOURCES = src/core/file_ops.c src/core/settings.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/list_ops.c  # Core sources without UI dependencies

# Windows GUI specific
WINDOWS_GUI_SOURCES = src/platform/ui_win32.c
WINDOWS_GUI_LIBS = -mwindows -lcomctl32 -lcomdlg32 -lgdi32 -lkernel32 -lshell32 -luser32 -lshcore -lole32 -luuid
WINDOWS_GUI_TARGET = npad.exe

# Windows Terminal specific
WINDOWS_TERMINAL_SOURCES = src/platform/ui_win32_terminal.c
WINDOWS_TERMINAL_LIBS = -lkernel32
WINDOWS_TERMINAL_TARGET = npad-terminal.exe

# macOS specific (future)
MACOS_SOURCES = src/platform/ui_cocoa.m
MACOS_LIBS = -framework Cocoa
MACOS_TARGET = npad-macos

# Linux X11 specific
LINUX_X11_SOURCES = src/platform/ui_x11.c
LINUX_X11_LIBS = -lX11 -lpthread
LINUX_X11_TARGET = npad-linux-x11

# Linux Wayland specific (future)
LINUX_WAYLAND_SOURCES = src/platform/ui_wayland.c
LINUX_WAYLAND_LIBS = -lwayland-client -lpthread
LINUX_WAYLAND_TARGET = npad-linux-wayland

# Linux Terminal (ncurses) specific
LINUX_TERMINAL_SOURCES = src/platform/ui_ncurses.c
LINUX_TERMINAL_LIBS = -lncurses -lpthread
LINUX_TERMINAL_TARGET = npad-linux-terminal

# Version information - use header version or fallback to git
# Not using awk here due to an issue found with awk 5.2.1 that I could not fix and was mangling strings
MAJOR = $(shell grep '^#define NPAD_VERSION_MAJOR' src/main.h | cut -d' ' -f3)
MINOR = $(shell grep '^#define NPAD_VERSION_MINOR' src/main.h | cut -d' ' -f3)
PATCH = $(shell grep '^#define NPAD_VERSION_PATCH' src/main.h | cut -d' ' -f3)
RELEASE = $(shell grep '^#define NPAD_VERSION_RELEASE' src/main.h | cut -d' ' -f3 | tr -d '"')
BASE_VERSION = v$(MAJOR).$(MINOR).$(PATCH)
HEADER_VERSION = $(BASE_VERSION)-$(RELEASE)
GIT_EXACT_TAG = $(shell git describe --tags --exact-match --dirty 2>/dev/null)
# Bare short hash (plus -dirty), NOT git describe --tags: describe leads with
# the most recent tag, so a dev build's About box would read "v0.12.0-dev
# (v0.11.0-2-gabc123)" and look like the wrong version.
GIT_COMMIT_RAW = $(shell git rev-parse --short HEAD 2>/dev/null)
ifneq ($(GIT_COMMIT_RAW),)
GIT_COMMIT = $(GIT_COMMIT_RAW)$(shell git diff-index --quiet HEAD -- 2>/dev/null || echo -dirty)
else
GIT_COMMIT =
endif
# Building exactly at the matching release tag (clean tree): plain "v0.10.1".
# Otherwise a dev build: "v0.10.2-dev (abc1234)"; without git available
# (e.g. some CI shells), just "v0.10.2-dev" - never empty parens.
ifeq ($(GIT_EXACT_TAG),$(BASE_VERSION))
VERSION ?= $(BASE_VERSION)
else ifneq ($(GIT_COMMIT),)
VERSION ?= $(HEADER_VERSION) ($(GIT_COMMIT))
else
VERSION ?= $(HEADER_VERSION)
endif
CFLAGS += -DNPAD_VERSION='"$(VERSION)"'

# Debug build support
ifdef DEBUG
CFLAGS += -g -DDEBUG -O0
LDFLAGS += -g
else
# Release build optimizations
CFLAGS += -Os -DNDEBUG
LDFLAGS += -s
endif

# Default target - detect platform and build
.DEFAULT_GOAL := detect-platform

# Build every variant this system can build (Windows cross + Linux native)
all: windows linux
# Note: macOS builds require macOS host system with Xcode/clang

detect-platform:
ifeq ($(CC),i686-w64-mingw32-gcc)
	$(MAKE) windows
else ifeq ($(CC),x86_64-w64-mingw32-gcc)
	$(MAKE) windows
else ifeq ($(findstring mingw,$(CC)),mingw)
	$(MAKE) windows
else ifeq ($(OS),Windows_NT)
	$(MAKE) windows
else ifeq ($(UNAME_S),Darwin)
	$(MAKE) macos
else ifeq ($(UNAME_S),Linux)
	$(MAKE) linux
else
	@echo "Unknown platform: $(UNAME_S)"
	@echo "Trying Linux build..."
	$(MAKE) linux
endif

# Platform-specific builds
windows: windows-gui windows-terminal

windows-gui: $(WINDOWS_GUI_TARGET)

$(WINDOWS_GUI_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_GUI_SOURCES) src/main.c src/platform/npad.rc
	cd src/platform && $(MINGW_WINDRES) npad.rc -O coff -o npad.res
	$(MINGW_CC) $(CFLAGS) -o $@ $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_GUI_SOURCES) src/main.c src/platform/npad.res $(WINDOWS_GUI_LIBS) $(LDFLAGS)
ifndef DEBUG
	$(MINGW_STRIP) $@
endif
	@echo "Windows GUI build complete: $(WINDOWS_GUI_TARGET)"

macos: $(MACOS_TARGET)

$(MACOS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(MACOS_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(MACOS_LIBS) $(LDFLAGS)
	@echo "macOS build complete: $(MACOS_TARGET)"

linux-x11: $(LINUX_X11_TARGET)

$(LINUX_X11_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_X11_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LINUX_X11_LIBS) $(LDFLAGS)
	@echo "Linux X11 build complete: $(LINUX_X11_TARGET)"

linux-wayland: $(LINUX_WAYLAND_TARGET)

$(LINUX_WAYLAND_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_WAYLAND_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LINUX_WAYLAND_LIBS) $(LDFLAGS)
	@echo "Linux Wayland build complete: $(LINUX_WAYLAND_TARGET)"

linux: linux-x11 linux-wayland linux-terminal

windows-terminal: $(WINDOWS_TERMINAL_TARGET)

$(WINDOWS_TERMINAL_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_TERMINAL_SOURCES) src/main.c
	$(MINGW_CC) $(CFLAGS) -o $@ $^ $(WINDOWS_TERMINAL_LIBS) $(LDFLAGS)
ifndef DEBUG
	$(MINGW_STRIP) $@
endif
	@echo "Windows Terminal build complete: $(WINDOWS_TERMINAL_TARGET)"

linux-terminal: $(LINUX_TERMINAL_TARGET)

$(LINUX_TERMINAL_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_TERMINAL_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LINUX_TERMINAL_LIBS) $(LDFLAGS)
	@echo "Linux Terminal build complete: $(LINUX_TERMINAL_TARGET)"

# Cross-platform terminal builds
terminal: windows-terminal linux-terminal

# Development targets
debug:
	$(MAKE) DEBUG=1

debug-linux:
	$(MAKE) linux DEBUG=1

debug-windows:
	$(MAKE) windows DEBUG=1

debug-windows-gui:
	$(MAKE) windows-gui DEBUG=1

debug-windows-terminal:
	$(MAKE) windows-terminal DEBUG=1

debug-linux-terminal:
	$(MAKE) linux-terminal DEBUG=1

debug-linux-x11:
	$(MAKE) linux-x11 DEBUG=1

debug-linux-wayland:
	$(MAKE) linux-wayland DEBUG=1

# Cross-compilation targets
windows-cross:
	$(MAKE) windows CC=x86_64-w64-mingw32-gcc

# Code quality
lint:
	@command -v cppcheck >/dev/null 2>&1 || { echo "cppcheck not found. Install with: sudo apt-get install cppcheck"; exit 1; }
	cppcheck --enable=all --std=c99 --platform=win32A \
		--suppress=missingIncludeSystem \
		--suppress=unusedFunction \
		--inline-suppr \
		--error-exitcode=1 \
		src/

# Formatting is canonical against clang-format 18 (what ubuntu-latest CI
# runs). If your distro ships another major version, install a pinned one
# (e.g. pip install clang-format==18.1.8) and point CLANG_FORMAT at it.
CLANG_FORMAT ?= clang-format

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "$(CLANG_FORMAT) not found. Install with: sudo apt-get install clang-format"; exit 1; }
	find src/ -name "*.c" -o -name "*.h" | xargs $(CLANG_FORMAT) -i

format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "$(CLANG_FORMAT) not found. Install with: sudo apt-get install clang-format"; exit 1; }
	find src/ -name "*.c" -o -name "*.h" | xargs $(CLANG_FORMAT) --dry-run --Werror

# Testing targets
test: test-file-ops test-error test-encoding test-session test-list-ops
	@echo "All tests completed"

test-file-ops: tests/test_file_ops
	@echo "Running file operations tests..."
	./tests/test_file_ops

test-error: tests/test_error
	@echo "Running error system tests..."
	./tests/test_error

test-encoding: tests/test_encoding
	@echo "Running encoding tests..."
	./tests/test_encoding

test-session: tests/test_session
	@echo "Running session recovery tests..."
	./tests/test_session

test-list-ops: tests/test_list_ops
	@echo "Running list operations tests..."
	./tests/test_list_ops

tests/test_file_ops: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_file_ops.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

tests/test_error: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_error.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

tests/test_encoding: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_encoding.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

tests/test_session: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_session.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

tests/test_list_ops: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_list_ops.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Cleanup
clean:
	rm -f $(WINDOWS_GUI_TARGET) $(WINDOWS_TERMINAL_TARGET) $(MACOS_TARGET) $(LINUX_X11_TARGET) $(LINUX_WAYLAND_TARGET) $(LINUX_TERMINAL_TARGET)
	rm -f npad-*.exe npad-*linux* npad-*win32*
	find src tests -name '*.o' -delete 2>/dev/null || true
	rm -f src/platform/npad.res
	rm -f tests/test_file_ops tests/test_error tests/test_encoding tests/test_session tests/test_list_ops
	rm -f tests/test_file_ops.exe tests/test_error.exe tests/test_encoding.exe tests/test_session.exe tests/test_list_ops.exe

# Installation
install:
ifeq ($(OS),Windows_NT)
	@echo "On Windows, copy npad.exe to a directory on your PATH, for example:"
	@echo "  mkdir %LOCALAPPDATA%\Programs\npad"
	@echo "  copy npad.exe %LOCALAPPDATA%\Programs\npad\"
else
	install -D $(LINUX_X11_TARGET) $(DESTDIR)/usr/local/bin/npad-x11
	install -D $(LINUX_WAYLAND_TARGET) $(DESTDIR)/usr/local/bin/npad-wayland
	install -D $(LINUX_TERMINAL_TARGET) $(DESTDIR)/usr/local/bin/npad-terminal
	ln -sf npad-x11 $(DESTDIR)/usr/local/bin/npad
endif

uninstall:
ifeq ($(OS),Windows_NT)
	@echo "Remove the directory you copied npad.exe into, for example:"
	@echo "  rmdir /s %LOCALAPPDATA%\Programs\npad"
else
	rm -f $(DESTDIR)/usr/local/bin/npad $(DESTDIR)/usr/local/bin/npad-x11 $(DESTDIR)/usr/local/bin/npad-wayland $(DESTDIR)/usr/local/bin/npad-terminal
endif

# Help
help:
	@echo "npad build system"
	@echo ""
	@echo "Targets:"
	@echo "  (default)        - Auto-detect platform and build"
	@echo "  all              - Build Windows and Linux variants"
	@echo "  windows          - Build all Windows variants (GUI + Terminal)"
	@echo "  windows-gui      - Build Windows GUI version"
	@echo "  windows-terminal - Build Windows Terminal version"
	@echo "  linux            - Build all Linux variants (X11 + Wayland + Terminal)"
	@echo "  linux-x11        - Build for Linux with X11"
	@echo "  linux-wayland    - Build for Linux with Wayland"
	@echo "  linux-terminal   - Build for Linux Terminal (ncurses)"
	@echo "  terminal         - Build all Terminal variants (Windows + Linux)"
	@echo "  macos            - Build for macOS"
	@echo "  debug            - Build with debug symbols"
	@echo "  lint             - Run code linting"
	@echo "  format           - Format code with clang-format"
	@echo "  format-check     - Check code formatting"
	@echo "  test             - Run all tests"
	@echo "  test-file-ops    - Run file operations tests"
	@echo "  test-error       - Run error system tests"
	@echo "  test-encoding    - Run encoding/line-ending tests"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install to system (Linux)"
	@echo "  uninstall        - Remove from system (Linux)"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC               - C compiler to use"
	@echo "  MINGW_CC         - MinGW compiler for Windows builds"
	@echo "  DEBUG=1          - Enable debug build"
	@echo "  VERSION          - Version string (auto-detected from git)"

.PHONY: all windows windows-gui windows-terminal linux linux-x11 linux-wayland linux-terminal terminal macos debug debug-windows debug-linux clean install uninstall lint format format-check test test-file-ops test-error test-encoding test-session test-list-ops help detect-platform
