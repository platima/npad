# npad Makefile
# Cross-platform text editor

# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
# WERROR=1 turns every compiler warning into an error (CI sets it)
ifeq ($(WERROR),1)
CFLAGS += -Werror
endif
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
MINGW_DLLTOOL ?= dlltool
else
MINGW_CC ?= x86_64-w64-mingw32-gcc
MINGW_WINDRES ?= x86_64-w64-mingw32-windres
MINGW_STRIP ?= x86_64-w64-mingw32-strip
MINGW_DLLTOOL ?= x86_64-w64-mingw32-dlltool
endif

# Windows on Arm (arm64) is cross-compiled with llvm-mingw (clang + lld; MinGW
# GCC has no arm64 Windows target), whose tools carry the target in their
# name: put its bin/ on PATH (scripts/install-llvm-mingw.sh fetches the pinned
# release CI uses) or point ARM64_MINGW_PREFIX at it. `make windows-arm64`
# builds npad-arm64.exe with the same sources and flags as npad.exe, in its own
# build directory so the two never share an object.
ARM64_MINGW_PREFIX ?= aarch64-w64-mingw32-
WINDOWS_ARM64_TARGET = npad-arm64.exe
# Per-architecture intermediates (delay-import libraries, the compiled .rc)
WIN_BUILD_DIR ?= build

# Source files
CORE_SOURCES = src/core/editor.c src/core/file_ops.c src/core/settings.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/startup_prof.c src/core/list_ops.c src/core/update_check.c src/core/html_md.c src/core/undo.c src/core/find_seed.c src/core/tab_stops.c
SHARED_SOURCES = src/ui_interface.c

# Test sources
TEST_FRAMEWORK_SOURCES = tests/test_framework.c
TEST_CORE_SOURCES = src/core/file_ops.c src/core/settings.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/list_ops.c src/core/update_check.c src/core/html_md.c src/core/undo.c src/core/find_seed.c src/core/tab_stops.c  # Core sources without UI dependencies

# Windows GUI specific
WINDOWS_GUI_SOURCES = src/platform/ui_win32.c src/platform/print_win32.c src/platform/assoc_win32.c
# Delay-loaded DLLs: none of these are needed to open a window, so binding them
# statically made the loader page them in on every launch - which costs most on
# a cold start, exactly when startup feels slow. winhttp/bcrypt serve only the
# opt-in update check, comdlg32 only the file/font dialogs, msimg32 only the
# Highlight All wash. dlltool builds a delay-import library from each .def in
# src/platform/delay/, and libdelayimp supplies the resolver that loads the DLL
# on the first real call. The matching -l flags must NOT also be passed, or the
# normal import library wins and the DLL is bound statically again.
DELAY_DLLS = winhttp bcrypt comdlg32 msimg32
#
# llvm-mingw (the arm64 build, WIN_LINKER=lld) gets there another way:
# llvm-dlltool has no -y, but lld delay-loads any DLL named with --delayload,
# taking the thunks from an ordinary import library and the resolver from
# libmingwex. So there the .def files become plain import libraries (still the
# only place those entry points are declared) and each DLL is named once more.
DELAY_LIBS = $(patsubst %,$(WIN_BUILD_DIR)/lib%_delay.a,$(DELAY_DLLS))
WIN_RES = $(WIN_BUILD_DIR)/npad.res
WIN_LINKER ?= bfd
comma := ,
ifeq ($(WIN_LINKER),lld)
DELAY_DLLTOOL_FLAG = -l
DELAY_LINK = $(DELAY_LIBS) $(patsubst %,-Wl$(comma)--delayload=%.dll,$(DELAY_DLLS))
else
DELAY_DLLTOOL_FLAG = -y
DELAY_LINK = $(DELAY_LIBS) -ldelayimp
endif
WINDOWS_GUI_LIBS = -mwindows -lcomctl32 -lgdi32 -lkernel32 -lshell32 -luser32 -lole32 -luuid $(DELAY_LINK)
WINDOWS_GUI_TARGET = npad.exe

# Windows Terminal specific
WINDOWS_TERMINAL_SOURCES = src/platform/ui_win32_terminal.c
WINDOWS_TERMINAL_LIBS = -lkernel32
WINDOWS_TERMINAL_TARGET = npad-terminal.exe

# macOS specific (future)
MACOS_SOURCES = src/platform/ui_cocoa.m
MACOS_LIBS = -framework Cocoa
MACOS_TARGET = npad-macos

# Linux desktop: GTK 3, one binary for both X11 and Wayland (GDK picks the
# display server at runtime). pkg-config is only consulted when this target
# is actually built - these are recursively expanded.
LINUX_GTK_SOURCES = src/platform/ui_gtk.c src/platform/ui_gtk_dialogs.c src/platform/ui_gtk_edit.c src/platform/ui_gtk_find.c src/platform/ui_gtk_lists.c src/platform/ui_gtk_prefs.c src/platform/ui_gtk_instance.c src/platform/ui_gtk_update.c src/platform/print_gtk.c
LINUX_GTK_HEADERS = src/platform/ui_gtk.h src/platform/print_gtk.h
LINUX_GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
LINUX_GTK_LIBS = $(shell pkg-config --libs gtk+-3.0) -lpthread
LINUX_GTK_TARGET = npad-linux

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
# A pre-release tag of this version (v0.10.2-rc1, clean tree) is spelled as
# the tag, so the About box and the updater know it is a release candidate.
GIT_PRERELEASE_TAG = $(filter-out %-dirty,$(filter $(BASE_VERSION)-%,$(GIT_EXACT_TAG)))
ifeq ($(GIT_EXACT_TAG),$(BASE_VERSION))
VERSION ?= $(BASE_VERSION)
else ifneq ($(GIT_PRERELEASE_TAG),)
VERSION ?= $(GIT_PRERELEASE_TAG)
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

# The GUI only: the terminal variant is an unimplemented stub and does not ship
windows-arm64:
	$(MAKE) $(WINDOWS_ARM64_TARGET) WINDOWS_GUI_TARGET=$(WINDOWS_ARM64_TARGET) \
		WIN_BUILD_DIR=build/arm64 WIN_LINKER=lld \
		MINGW_CC=$(ARM64_MINGW_PREFIX)clang \
		MINGW_WINDRES=$(ARM64_MINGW_PREFIX)windres \
		MINGW_STRIP=$(ARM64_MINGW_PREFIX)strip \
		MINGW_DLLTOOL=$(ARM64_MINGW_PREFIX)dlltool

# Delay-import libraries, one per DLL, built from the .def files that document
# exactly which entry points npad uses
$(WIN_BUILD_DIR)/lib%_delay.a: src/platform/delay/%.def
	@mkdir -p $(WIN_BUILD_DIR)
	$(MINGW_DLLTOOL) -d $< -D $*.dll $(DELAY_DLLTOOL_FLAG) $@

$(WINDOWS_GUI_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_GUI_SOURCES) src/main.c src/platform/npad.rc $(DELAY_LIBS)
	@mkdir -p $(WIN_BUILD_DIR)
	cd src/platform && $(MINGW_WINDRES) npad.rc -O coff -o ../../$(WIN_RES)
	$(MINGW_CC) $(CFLAGS) -o $@ $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_GUI_SOURCES) src/main.c $(WIN_RES) $(WINDOWS_GUI_LIBS) $(LDFLAGS)
ifndef DEBUG
	$(MINGW_STRIP) $@
endif
	@echo "Windows GUI build complete: $(WINDOWS_GUI_TARGET)"

macos: $(MACOS_TARGET)

$(MACOS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(MACOS_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(MACOS_LIBS) $(LDFLAGS)
	@echo "macOS build complete: $(MACOS_TARGET)"

linux-gtk: $(LINUX_GTK_TARGET)

$(LINUX_GTK_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_GTK_SOURCES) $(LINUX_GTK_HEADERS) src/main.c
	@pkg-config --exists gtk+-3.0 || { echo "GTK 3 development files not found. Install with: sudo apt-get install libgtk-3-dev"; exit 1; }
	$(CC) $(CFLAGS) $(LINUX_GTK_CFLAGS) -o $@ $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_GTK_SOURCES) src/main.c $(LINUX_GTK_LIBS) $(LDFLAGS)
	@echo "Linux build complete: $(LINUX_GTK_TARGET)"

linux: linux-gtk linux-terminal

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

debug-linux-gtk:
	$(MAKE) linux-gtk DEBUG=1

# Cross-compilation targets
windows-cross:
	$(MAKE) windows CC=x86_64-w64-mingw32-gcc

# Code quality
#
# Linting is canonical against cppcheck 2.13 (what ubuntu-latest CI runs).
# Older versions are LESS strict and have twice passed changes that CI then
# rejected, so if your distro ships an older one, build 2.13 and point
# CPPCHECK at it rather than trusting a local pass.
CPPCHECK ?= cppcheck

lint:
	@command -v $(CPPCHECK) >/dev/null 2>&1 || { echo "$(CPPCHECK) not found. Install with: sudo apt-get install cppcheck"; exit 1; }
	$(CPPCHECK) --enable=all --std=c99 --platform=win32A \
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
test: test-file-ops test-error test-encoding test-session test-list-ops test-update-check test-settings test-html-md test-undo test-find-seed test-tab-stops
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

test-update-check: tests/test_update_check
	@echo "Running update-check tests..."
	./tests/test_update_check

test-settings: tests/test_settings
	@echo "Running settings tests..."
	./tests/test_settings

test-html-md: tests/test_html_md
	@echo "Running HTML/Markdown tests..."
	./tests/test_html_md

test-undo: tests/test_undo
	@echo "Running undo history tests..."
	./tests/test_undo

test-find-seed: tests/test_find_seed
	@echo "Running find seeding tests..."
	./tests/test_find_seed

test-tab-stops: tests/test_tab_stops
	@echo "Running tab stop tests..."
	./tests/test_tab_stops

# Windows-only: the file type association module against a registry sandbox.
# Cross-compiled here; run tests/test_assoc_win32.exe on Windows. Not part of
# `make test`, which runs natively on Linux.
test-assoc-win32: tests/test_assoc_win32.exe

tests/test_assoc_win32.exe: tests/test_assoc_win32.c src/platform/assoc_win32.c $(TEST_FRAMEWORK_SOURCES)
	$(MINGW_CC) $(CFLAGS) -o $@ $^ -ladvapi32 -lshell32 -luser32

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

tests/test_update_check: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_update_check.c
	$(CC) $(CFLAGS) -o tests/test_update_check $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_update_check.c

tests/test_settings: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_settings.c
	$(CC) $(CFLAGS) -o tests/test_settings $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_settings.c

tests/test_html_md: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_html_md.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Built with allocation failure injection compiled into undo.c, so the test can
# prove a failed record never leaves a history that would corrupt the text
tests/test_undo: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_undo.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -DUNDO_FAULT_INJECTION -o $@ $^ -lpthread

tests/test_find_seed: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_find_seed.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

tests/test_tab_stops: $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_tab_stops.c
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Cleanup
clean:
	rm -f $(WINDOWS_GUI_TARGET) $(WINDOWS_TERMINAL_TARGET) $(MACOS_TARGET) $(LINUX_GTK_TARGET) $(LINUX_TERMINAL_TARGET)
	rm -f npad-*.exe npad-*linux* npad-*win32*
	find src tests -name '*.o' -delete 2>/dev/null || true
	rm -f src/platform/npad.res
	rm -rf build
	rm -rf dist/npad_* dist/npad-*.rpm dist/npad-*.pkg.tar.zst dist/npad-v*-portable-linux-*.tar.gz
	rm -f tests/test_file_ops tests/test_error tests/test_encoding tests/test_session tests/test_list_ops tests/test_update_check tests/test_settings tests/test_html_md tests/test_undo tests/test_find_seed tests/test_tab_stops
	rm -f tests/test_file_ops.exe tests/test_error.exe tests/test_encoding.exe tests/test_session.exe tests/test_list_ops.exe tests/test_update_check.exe tests/test_settings.exe tests/test_html_md.exe tests/test_undo.exe tests/test_find_seed.exe tests/test_tab_stops.exe

# Installation (Linux). PREFIX defaults to /usr/local; DESTDIR stages the tree
# for packaging. Only the GTK desktop app is installed: the ncurses variant is
# still an unimplemented stub.
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APP_ID = io.github.platima.npad
LINUX_PKG_DIR = packaging/linux
LINUX_ICON_SIZES = 16 24 32 48 64 128 256
# The standard icon, and the dark artwork the window switches to under a dark
# scheme (the launcher always uses the standard one)
LINUX_ICON_NAMES = $(APP_ID) $(APP_ID)-dark

# Build first (make linux-gtk); install does not, so `sudo make install` never
# compiles as root.
install:
ifeq ($(OS),Windows_NT)
	@echo "On Windows, copy npad.exe to a directory on your PATH, for example:"
	@echo "  mkdir %LOCALAPPDATA%\Programs\npad"
	@echo "  copy npad.exe %LOCALAPPDATA%\Programs\npad\"
else
	@test -f $(LINUX_GTK_TARGET) || { echo "$(LINUX_GTK_TARGET) not built. Run: make linux-gtk"; exit 1; }
	install -D -m 755 $(LINUX_GTK_TARGET) $(DESTDIR)$(BINDIR)/npad
	install -D -m 644 $(LINUX_PKG_DIR)/$(APP_ID).desktop $(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop
	install -D -m 644 $(LINUX_PKG_DIR)/$(APP_ID).metainfo.xml $(DESTDIR)$(DATADIR)/metainfo/$(APP_ID).metainfo.xml
	for n in $(LINUX_ICON_NAMES); do for s in $(LINUX_ICON_SIZES); do \
		install -D -m 644 $(LINUX_PKG_DIR)/icons/hicolor/$${s}x$${s}/apps/$${n}.png \
			$(DESTDIR)$(DATADIR)/icons/hicolor/$${s}x$${s}/apps/$${n}.png || exit 1; \
	done; done
endif

uninstall:
ifeq ($(OS),Windows_NT)
	@echo "Remove the directory you copied npad.exe into, for example:"
	@echo "  rmdir /s %LOCALAPPDATA%\Programs\npad"
else
	rm -f $(DESTDIR)$(BINDIR)/npad $(DESTDIR)$(BINDIR)/npad-terminal
	rm -f $(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop
	rm -f $(DESTDIR)$(DATADIR)/metainfo/$(APP_ID).metainfo.xml
	for n in $(LINUX_ICON_NAMES); do for s in $(LINUX_ICON_SIZES); do \
		rm -f $(DESTDIR)$(DATADIR)/icons/hicolor/$${s}x$${s}/apps/$${n}.png; \
	done; done
endif

# Linux packages in dist/. All are described once, in
# packaging/linux/nfpm.yaml, and built by nfpm (a single static binary: CI
# installs a pinned copy with scripts/install-nfpm.sh). Build in a Debian 12
# container, as release CI does, so the libc floor stays low enough for
# Debian 12 and Ubuntu 22.04.
#   deb: npad_<version>_<arch>.deb     Depends: libgtk-3-0t64 | libgtk-3-0
#   rpm: npad-<version>-1.<arch>.rpm   Requires: gtk3
#   pacman: npad-<version>-1-<arch>.pkg.tar.zst   depends=gtk3 (Arch Linux,
#           Arch Linux ARM, Arch RISC-V)
# The architecture is the binary's own (amd64, arm64, riscv64; the rpm and the
# Arch package say x86_64, aarch64, riscv64), so a binary built elsewhere - CI compiles riscv64
# under QEMU - packages on any host with nfpm. PKG_ARCH, if set, must match it.
# Without nfpm, `make deb` falls back to dpkg-deb (scripts/build-deb.sh); the
# the rpm and the Arch package need nfpm.
DEB_VERSION = $(MAJOR).$(MINOR).$(PATCH)
PKG_ARCH ?=

deb: $(LINUX_GTK_TARGET)
ifneq ($(shell command -v nfpm 2>/dev/null),)
	sh scripts/build-package.sh deb $(DEB_VERSION) $(LINUX_GTK_TARGET) dist $(PKG_ARCH)
else
	@echo "nfpm not found, using dpkg-deb (scripts/install-nfpm.sh installs nfpm)"
	sh scripts/build-deb.sh $(DEB_VERSION) $(LINUX_GTK_TARGET) dist $(PKG_ARCH)
endif

rpm: $(LINUX_GTK_TARGET)
	sh scripts/build-package.sh rpm $(DEB_VERSION) $(LINUX_GTK_TARGET) dist $(PKG_ARCH)

pacman: $(LINUX_GTK_TARGET)
	sh scripts/build-package.sh arch $(DEB_VERSION) $(LINUX_GTK_TARGET) dist $(PKG_ARCH)

# The portable download: dist/npad-v<version>-portable-linux-<arch>.tar.gz, the same
# binary plus the launcher entry, icons, metainfo and a README.txt, to run in
# place or copy under ~/.local. Needs no nfpm.
tarball: $(LINUX_GTK_TARGET)
	sh scripts/build-package.sh tar $(DEB_VERSION) $(LINUX_GTK_TARGET) dist $(PKG_ARCH)

# Help
help:
	@echo "npad build system"
	@echo ""
	@echo "Targets:"
	@echo "  (default)        - Auto-detect platform and build"
	@echo "  all              - Build Windows and Linux variants"
	@echo "  windows          - Build all Windows variants (GUI + Terminal)"
	@echo "  windows-gui      - Build Windows GUI version"
	@echo "  windows-arm64    - Build npad-arm64.exe (Windows on Arm; needs llvm-mingw)"
	@echo "  windows-terminal - Build Windows Terminal version"
	@echo "  linux            - Build all Linux variants (desktop + Terminal)"
	@echo "  linux-gtk        - Build the Linux desktop app (GTK 3; X11 and Wayland)"
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
	@echo "  test-undo        - Run undo history tests"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install to PREFIX (default /usr/local; honours DESTDIR)"
	@echo "  uninstall        - Remove from PREFIX"
	@echo "  deb              - Build dist/npad_<version>_<arch>.deb (Linux; nfpm or dpkg-deb)"
	@echo "  rpm              - Build dist/npad-<version>-1.<arch>.rpm (Linux; needs nfpm)"
	@echo "  pacman           - Build dist/npad-<version>-1-<arch>.pkg.tar.zst (Arch Linux; needs nfpm)"
	@echo "  tarball          - Build dist/npad-v<version>-portable-linux-<arch>.tar.gz (Linux portable)"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC               - C compiler to use"
	@echo "  MINGW_CC         - MinGW compiler for Windows builds"
	@echo "  CPPCHECK         - cppcheck binary (CI uses 2.13; older is less strict)"
	@echo "  CLANG_FORMAT     - clang-format binary (CI uses 18)"
	@echo "  DEBUG=1          - Enable debug build"
	@echo "  VERSION          - Version string (auto-detected from git)"

.PHONY: all windows windows-gui windows-arm64 windows-terminal linux linux-gtk linux-terminal terminal macos debug debug-windows debug-linux debug-linux-gtk clean install uninstall deb rpm pacman tarball lint format format-check test test-file-ops test-error test-encoding test-session test-list-ops test-update-check test-settings test-html-md test-undo help detect-platform
