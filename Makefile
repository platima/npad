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
# name. Point LLVM_MINGW at its install directory (scripts/install-llvm-mingw.sh
# fetches the pinned release CI uses), or put its bin/ on PATH - APPENDED, not
# prepended: llvm-mingw also ships x86_64-w64-mingw32-gcc/-dlltool/-windres, so
# ahead of MinGW GCC it takes over `make windows` too (which then builds with
# clang + lld, see WIN_LINKER below). `make windows-arm64` builds
# npad-arm64.exe with the same sources and flags as npad.exe, in its own build
# directory so the two never share an object.
LLVM_MINGW ?=
ifneq ($(LLVM_MINGW),)
ARM64_MINGW_PREFIX ?= $(LLVM_MINGW)/bin/aarch64-w64-mingw32-
else
ARM64_MINGW_PREFIX ?= aarch64-w64-mingw32-
endif
WINDOWS_ARM64_TARGET = npad-arm64.exe
# Per-architecture intermediates (delay-import libraries, the compiled .rc)
WIN_BUILD_DIR ?= build

# Source files
CORE_SOURCES = src/core/editor.c src/core/file_ops.c src/core/settings.c src/core/settings_import.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/startup_prof.c src/core/list_ops.c src/core/update_check.c src/core/html_md.c src/core/undo.c src/core/find_seed.c src/core/tab_stops.c src/core/text_scan.c src/core/ui_rules.c src/core/print_core.c src/core/time_format.c
SHARED_SOURCES = src/ui_interface.c

# Test sources
TEST_FRAMEWORK_SOURCES = tests/test_framework.c
TEST_CORE_SOURCES = src/core/file_ops.c src/core/settings.c src/core/settings_import.c src/core/session.c src/core/thread_safety.c src/core/error.c src/core/list_ops.c src/core/update_check.c src/core/html_md.c src/core/undo.c src/core/find_seed.c src/core/tab_stops.c src/core/text_scan.c src/core/ui_rules.c src/core/print_core.c src/core/time_format.c  # Core sources without UI dependencies

# Windows GUI specific
WINDOWS_GUI_SOURCES = src/platform/ui_win32.c src/platform/print_win32.c src/platform/assoc_win32.c src/platform/richedit_text.c src/platform/args_win32.c
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
# Which of the two routes: bfd (MinGW GCC + GNU binutils, the shipped x64
# build) or lld (llvm-mingw). Unless given, it follows the compiler MINGW_CC
# actually resolves to, so `make windows` still builds when llvm-mingw's bin/
# sits ahead on PATH and its x86_64-w64-mingw32-* tools (clang, lld,
# llvm-dlltool) shadow the GNU ones - the bfd route's dlltool -y would fail
# there. Recursively expanded: the compiler is only asked when a Windows
# recipe needs the answer, never for `make test` or a Linux build.
comma := ,
WIN_LINKER ?= $(if $(findstring clang,$(shell $(MINGW_CC) --version 2>/dev/null | head -n 1)),lld,bfd)
DELAY_DLLTOOL_FLAG = $(if $(filter lld,$(WIN_LINKER)),-l,-y)
# On the lld route --dependent-load-flag=0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32)
# also keeps the loader's own search for imported DLLs out of the exe's folder.
# GNU ld has no such option; there main() calls SetDefaultDllDirectories first
# thing, which covers the delay-loaded and runtime-loaded DLLs (the static
# imports are all KnownDLLs or the side-by-side comctl32).
DELAY_LINK_LLD = $(patsubst %,-Wl$(comma)--delayload=%.dll,$(DELAY_DLLS)) -Wl$(comma)--dependent-load-flag=0x800
DELAY_LINK = $(DELAY_LIBS) $(if $(filter lld,$(WIN_LINKER)),$(DELAY_LINK_LLD),-ldelayimp)
WINDOWS_GUI_LIBS = -mwindows -lcomctl32 -lgdi32 -lkernel32 -lshell32 -luser32 -lole32 -luuid $(DELAY_LINK)
WINDOWS_GUI_TARGET = npad.exe

# Windows Terminal specific
WINDOWS_TERMINAL_SOURCES = src/platform/ui_win32_terminal.c src/platform/args_win32.c
# ole32 + uuid: settings.c finds %APPDATA% with SHGetKnownFolderPath
WINDOWS_TERMINAL_LIBS = -lkernel32 -lole32 -luuid
WINDOWS_TERMINAL_TARGET = npad-terminal.exe

# macOS specific (future)
MACOS_SOURCES = src/platform/ui_cocoa.m
MACOS_LIBS = -framework Cocoa
MACOS_TARGET = npad-macos

# Linux desktop: GTK 3, one binary for both X11 and Wayland (GDK picks the
# display server at runtime). pkg-config is only consulted when this target
# is actually built - these are recursively expanded.
LINUX_GTK_SOURCES = src/platform/ui_gtk.c src/platform/ui_gtk_dialogs.c src/platform/ui_gtk_edit.c src/platform/ui_gtk_find.c src/platform/ui_gtk_lists.c src/platform/ui_gtk_prefs.c src/platform/ui_gtk_instance.c src/platform/ui_gtk_session.c src/platform/ui_gtk_update.c src/platform/print_gtk.c
LINUX_GTK_HEADERS = src/platform/ui_gtk.h src/platform/print_gtk.h
LINUX_GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
LINUX_GTK_LIBS = $(shell pkg-config --libs gtk+-3.0) -lpthread
LINUX_GTK_TARGET = npad-linux

# Linux Terminal (ncurses) specific
LINUX_TERMINAL_SOURCES = src/platform/ui_ncurses.c
LINUX_TERMINAL_LIBS = -lncurses -lpthread
LINUX_TERMINAL_TARGET = npad-linux-terminal
# The header `make linux` probes for before building the stub
NCURSES_HEADER ?= ncurses.h

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

# The desktop app, plus the ncurses stub only where ncurses' headers are
# installed: the stub does nothing yet, so it is not worth a missing package
# failing the command README gives. `make linux-terminal` still requires them.
linux: linux-gtk
	@if printf '#include <$(NCURSES_HEADER)>\n' | $(CC) $(CFLAGS) -E -x c - >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory linux-terminal; \
	else \
		echo "Skipping $(LINUX_TERMINAL_TARGET) (the unimplemented ncurses stub): $(NCURSES_HEADER) not found."; \
		echo "  Install libncurses-dev (Debian/Ubuntu) or ncurses-devel (Fedora) and run: make linux-terminal"; \
	fi

windows-terminal: $(WINDOWS_TERMINAL_TARGET)

$(WINDOWS_TERMINAL_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_TERMINAL_SOURCES) src/main.c
	$(MINGW_CC) $(CFLAGS) -o $@ $^ $(WINDOWS_TERMINAL_LIBS) $(LDFLAGS)
ifndef DEBUG
	$(MINGW_STRIP) $@
endif
	@echo "Windows Terminal build complete: $(WINDOWS_TERMINAL_TARGET)"

linux-terminal: $(LINUX_TERMINAL_TARGET)

$(LINUX_TERMINAL_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_TERMINAL_SOURCES) src/main.c
	@printf '#include <$(NCURSES_HEADER)>\n' | $(CC) $(CFLAGS) -E -x c - >/dev/null 2>&1 || { echo "ncurses development files not found ($(NCURSES_HEADER)). Install with: sudo apt-get install libncurses-dev"; exit 1; }
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
#
# Each suite is tests/test_<name>.c, linked with the core sources and the
# framework, and builds three ways from the same rule set:
#   make test           native binaries (tests/test_<name>), run in turn
#   make test-windows   Windows executables (tests/test_<name>.exe); cross-
#                       compiled anywhere, run only on Windows (MSYS2)
#   make test-sanitize  the suite under AddressSanitizer + UBSan
#   make test-fuzz      the seeded property/fuzz suite, longer, sanitized
# Every suite runs in its own fresh temporary directory with the settings
# location pointed inside it (tests/test_framework.c), so a run never writes
# into the checkout or reads the user's real settings.
TEST_SUITES = file_ops atomic_save error encoding session list_ops update_check settings html_md undo find_seed tab_stops settings_import richedit_text editor_load editor recovery editor_insert text_scan editor_reload ui_rules print_core time_format fuzz
TEST_DEPS = $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) tests/test_framework.h
# NPAD_TEST_HOOKS: on Windows a test build keeps settings under
# %NPAD_TEST_CONFIG_DIR% (which the framework sets) instead of %APPDATA%
TEST_CFLAGS = $(CFLAGS) -DNPAD_TEST_HOOKS
# Per-suite extra flags. undo.c is built with allocation failure injection, so
# the test can prove a failed record never leaves a history that would corrupt
# the text; fault injection makes each stage of a save fail on demand
# (file_ops.h).
TEST_FLAGS_undo = -DUNDO_FAULT_INJECTION
TEST_FLAGS_atomic_save = -DFILE_OPS_FAULT_INJECTION
# Per-suite extra sources, linked after the core ones. The editor suites run
# the real editor.c against a UI layer of their own instead of a window:
# test_editor and test_recovery against shared stubs (tests/stub_ui_*.c),
# test_editor_load, test_editor_insert and test_editor_reload against stubs
# in the test file.
TEST_EXTRA_richedit_text = src/platform/richedit_text.c
TEST_EXTRA_editor_load = src/core/editor.c src/core/startup_prof.c
TEST_EXTRA_editor = src/core/editor.c tests/stub_ui_editor.c
TEST_EXTRA_recovery = src/core/editor.c tests/stub_ui_recovery.c
TEST_EXTRA_editor_insert = src/core/editor.c
TEST_EXTRA_editor_reload = src/core/editor.c
# ... which every build of that suite (native, .exe, sanitized) depends on
# (the rules that add them follow the sanitizer section, where SAN_DIR is set)
TEST_EXTRA_DEPS_richedit_text = $(TEST_EXTRA_richedit_text) src/platform/richedit_text.h
TEST_EXTRA_DEPS_editor_load = $(TEST_EXTRA_editor_load)
TEST_EXTRA_DEPS_editor = $(TEST_EXTRA_editor) tests/stub_ui_editor.h
TEST_EXTRA_DEPS_recovery = $(TEST_EXTRA_recovery) tests/stub_ui_recovery.h
TEST_EXTRA_DEPS_editor_insert = $(TEST_EXTRA_editor_insert)
TEST_EXTRA_DEPS_editor_reload = $(TEST_EXTRA_editor_reload)

test: test-file-ops test-atomic-save test-error test-encoding test-session test-list-ops test-update-check test-settings test-html-md test-undo test-find-seed test-tab-stops test-settings-import test-richedit-text test-editor-load test-editor test-recovery test-editor-insert test-text-scan test-editor-reload test-ui-rules test-print-core test-time-format test-platform-stubs test-fuzz-quick
	@echo "All tests completed"

# Each stub backend must define every ui_platform_* function the shared code
# calls. The Cocoa stub is never compiled here (no macOS runner), which is how
# it once stopped linking unnoticed; the check reads the symbols the shared
# code leaves undefined from one relocatable link, then looks for each
# definition in the stub sources. The Cocoa stub is plain C apart from its
# #import, so it is also linked for real, as C, against the same object.
# Only the ui_*.c backends: the Windows terminal build also links helpers
# (args_win32.c, the wide command line) that are not a UI backend.
STUB_BACKENDS = $(MACOS_SOURCES) $(LINUX_TERMINAL_SOURCES) $(filter src/platform/ui_%,$(WINDOWS_TERMINAL_SOURCES))
STUB_CHECK_DIR = build/stub-check
test-platform-stubs: $(STUB_CHECK_DIR)/shared.o $(STUB_CHECK_DIR)/npad-macos-stub
	@echo "Running platform stub coverage check..."
	sh scripts/check-platform-stubs.sh $(STUB_CHECK_DIR)/shared.o $(STUB_BACKENDS)
	@echo "Cocoa stub links against the shared code"

$(STUB_CHECK_DIR)/shared.o: $(CORE_SOURCES) $(SHARED_SOURCES) src/main.c
	@mkdir -p $(STUB_CHECK_DIR)
	$(CC) $(CFLAGS) -nostdlib -r -o $@ $^

$(STUB_CHECK_DIR)/ui_cocoa.c: $(MACOS_SOURCES)
	@mkdir -p $(STUB_CHECK_DIR)
	sed '/^#import/d' $< > $@

$(STUB_CHECK_DIR)/npad-macos-stub: $(STUB_CHECK_DIR)/shared.o $(STUB_CHECK_DIR)/ui_cocoa.c
	$(CC) $(CFLAGS) -Isrc/platform -o $@ $^ -lpthread

test-file-ops: tests/test_file_ops
	@echo "Running file operations tests..."
	./tests/test_file_ops

test-atomic-save: tests/test_atomic_save
	@echo "Running atomic save tests..."
	./tests/test_atomic_save

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

test-settings-import: tests/test_settings_import
	@echo "Running settings import tests..."
	./tests/test_settings_import

test-richedit-text: tests/test_richedit_text
	@echo "Running RichEdit line break normalisation tests..."
	./tests/test_richedit_text

test-editor-load: tests/test_editor_load
	@echo "Running editor load/save contract tests..."
	./tests/test_editor_load

# The editor core (open, save, Save As, auto-save, external changes, timers)
# against a stub UI layer instead of a window
test-editor: tests/test_editor
	@echo "Running editor core tests..."
	./tests/test_editor

# Crash recovery and handoff at the editor level
test-recovery: tests/test_recovery
	@echo "Running crash recovery tests..."
	./tests/test_recovery

# Ctrl+drop: the prompts editor.c raises are answered and counted
test-editor-insert: tests/test_editor_insert
	@echo "Running Ctrl+drop insert tests..."
	./tests/test_editor_insert

test-text-scan: tests/test_text_scan
	@echo "Running text scan tests..."
	./tests/test_text_scan

test-editor-reload: tests/test_editor_reload
	@echo "Running editor reload tests..."
	./tests/test_editor_reload

test-ui-rules: tests/test_ui_rules
	@echo "Running window and dialog rule tests..."
	./tests/test_ui_rules

test-print-core: tests/test_print_core
	@echo "Running print pagination tests..."
	./tests/test_print_core

test-time-format: tests/test_time_format
	@echo "Running time format tests..."
	./tests/test_time_format

# The release and CI scripts' own checks (the version gate, CHECKSUMS.txt,
# installer file references), against throwaway fixtures. Shell only.
.PHONY: test-release-scripts
test: test-release-scripts

test-release-scripts:
	@echo "Running release script tests..."
	sh tests/test_release_scripts.sh

# The property/fuzz suite at its default (small) iteration count, so every
# `make test` exercises it; `make test-fuzz` runs it longer under sanitizers
test-fuzz-quick: tests/test_fuzz
	@echo "Running property/fuzz tests (quick)..."
	./tests/test_fuzz

tests/test_%: tests/test_%.c $(TEST_DEPS)
	$(CC) $(TEST_CFLAGS) $(TEST_FLAGS_$*) -o $@ $(TEST_CORE_SOURCES) $(TEST_EXTRA_$*) $(TEST_FRAMEWORK_SOURCES) $< -lpthread

# --- Windows ---------------------------------------------------------------
#
# The same suites as Windows executables, plus the Windows-only tests below.
# Every one links an asInvoker manifest: without one, UAC's installer
# detection makes test_update_check.exe ("update" in the name) demand
# elevation. test_assoc_win32.exe writes a scratch key under HKCU (removed
# afterwards), so it runs only with NPAD_ALLOW_REGISTRY_TESTS=1, here and when
# launched by hand.
# test_atomic_save is POSIX only: its faults are injected through rlimits and
# file modes that Windows does not have.
WIN_TEST_SUITES = $(filter-out atomic_save,$(TEST_SUITES))
WIN_TEST_EXES = $(patsubst %,tests/test_%.exe,$(WIN_TEST_SUITES))
WIN_TEST_RES = $(WIN_BUILD_DIR)/test_manifest.res
WIN_TEST_LIBS = -lshell32 -ladvapi32 -luser32 -lole32

$(WIN_TEST_RES): tests/test_manifest.rc tests/test.manifest
	@mkdir -p $(WIN_BUILD_DIR)
	cd tests && $(MINGW_WINDRES) test_manifest.rc -O coff -o ../$(WIN_TEST_RES)

tests/test_%.exe: tests/test_%.c $(TEST_DEPS) $(WIN_TEST_RES)
	$(MINGW_CC) $(TEST_CFLAGS) $(TEST_FLAGS_$*) -o $@ $(TEST_CORE_SOURCES) $(TEST_EXTRA_$*) $(TEST_FRAMEWORK_SOURCES) $< $(WIN_TEST_RES) $(WIN_TEST_LIBS)

tests/test_assoc_win32.exe: tests/test_assoc_win32.c src/platform/assoc_win32.c src/platform/assoc_win32.h $(TEST_FRAMEWORK_SOURCES) tests/test_framework.h $(WIN_TEST_RES)
	$(MINGW_CC) $(TEST_CFLAGS) -o $@ tests/test_assoc_win32.c src/platform/assoc_win32.c $(TEST_FRAMEWORK_SOURCES) $(WIN_TEST_RES) -ladvapi32 -lshell32 -luser32

# Windows-only: the RichEdit text boundary (src/platform/richedit_text.c)
# against a hidden msftedit control in the test's own process. Cross-compiled
# here; run tests/test_richedit_boundary.exe on Windows (make test-windows does,
# CI too). Links no settings or session code: it writes no
# configuration and no registry value, and messages only its own windows.
test-richedit-win32: tests/test_richedit_boundary.exe

tests/test_richedit_boundary.exe: tests/test_richedit_boundary.c src/platform/richedit_text.c src/platform/richedit_text.h src/core/file_ops.c src/core/error.c src/core/thread_safety.c $(TEST_FRAMEWORK_SOURCES) tests/test_framework.h $(WIN_TEST_RES)
	$(MINGW_CC) $(TEST_CFLAGS) -o $@ $(filter %.c,$^) $(WIN_TEST_RES) -luser32 -lgdi32 -lshell32

# Windows-only: UTF-8 argv from the wide command line, and settings under a
# non-ASCII directory. Cross-compiled here; run tests/test_win32_unicode.exe
# on Windows (a test VM: it writes only under %TEMP%). Not part of `make test`.
test-win32-unicode: tests/test_win32_unicode.exe

tests/test_win32_unicode.exe: tests/test_win32_unicode.c src/platform/args_win32.c $(TEST_DEPS) $(WIN_TEST_RES)
	$(MINGW_CC) $(TEST_CFLAGS) -o $@ tests/test_win32_unicode.c src/platform/args_win32.c $(TEST_CORE_SOURCES) $(TEST_FRAMEWORK_SOURCES) $(WIN_TEST_RES) -lshell32 -lole32 -luuid

test-windows-build: $(WIN_TEST_EXES) tests/test_assoc_win32.exe tests/test_richedit_boundary.exe tests/test_win32_unicode.exe

test-assoc-win32: tests/test_assoc_win32.exe

test-windows: test-windows-build
ifeq ($(OS),Windows_NT)
	@set -e; for t in $(WIN_TEST_EXES) tests/test_richedit_boundary.exe; do echo "Running $$t..."; ./$$t; done
	@if [ "$$NPAD_ALLOW_REGISTRY_TESTS" = "1" ]; then \
		echo "Running tests/test_assoc_win32.exe..."; ./tests/test_assoc_win32.exe; \
	else \
		echo "Skipped tests/test_assoc_win32.exe: set NPAD_ALLOW_REGISTRY_TESTS=1 to run it"; \
	fi
	@echo "All Windows tests completed"
else
	@echo "Windows test executables built in tests/. They run on Windows only:"
	@echo "run make test-windows there (MSYS2), or launch tests/test_*.exe."
endif

# --- Sanitizers and fuzzing --------------------------------------------------
#
# gcc's AddressSanitizer aborts at start-up ("DEADLYSIGNAL", or a shadow
# memory mapping failure) on kernels that randomise mmap with more bits than it
# expects - WSL2 and newer Ubuntu runner images among them. Running the test
# with address-space randomisation off for that one process avoids it, so
# SANITIZE_RUN uses `setarch -R` wherever personality(2) is permitted (some
# containers forbid it; there the test runs as-is). Override SANITIZE_RUN= to
# run without it.
SANITIZE_FLAGS ?= -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1
SANITIZE_RUN ?= $(shell setarch $(UNAME_M) -R true >/dev/null 2>&1 && echo setarch $(UNAME_M) -R)
SANITIZE_ENV = ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
SAN_DIR = build/sanitize
SAN_TEST_BINS = $(patsubst %,$(SAN_DIR)/test_%,$(TEST_SUITES))
# make test-fuzz: iterations per property and the PRNG seed. Both fixed by
# default so a CI failure reproduces exactly; vary FUZZ_SEED to explore.
# Like every suite, it writes its scratch files in a fresh folder under
# $(TMPDIR) (an absolute path; /tmp otherwise). Its saves fsync, so on a disk
# where that is slow (WSL, CI runners) the default count takes over ten
# minutes under ASan: `TMPDIR=/dev/shm make test-fuzz` keeps it on tmpfs.
FUZZ_ITERS ?= 5000
FUZZ_SEED ?= 20261021

$(SAN_DIR)/test_%: tests/test_%.c $(TEST_DEPS)
	@mkdir -p $(SAN_DIR)
	$(CC) $(TEST_CFLAGS) $(TEST_FLAGS_$*) $(SANITIZE_FLAGS) -o $@ $(TEST_CORE_SOURCES) $(TEST_EXTRA_$*) $(TEST_FRAMEWORK_SOURCES) $< -lpthread

test-sanitize: $(SAN_TEST_BINS)
	@set -e; for t in $(SAN_TEST_BINS); do \
		echo "Running $$t under ASan+UBSan..."; \
		$(SANITIZE_ENV) $(SANITIZE_RUN) ./$$t; \
	done
	@echo "All sanitizer tests completed"

test-fuzz: $(SAN_DIR)/test_fuzz
	@echo "Running property/fuzz tests: $(FUZZ_ITERS) iterations, seed $(FUZZ_SEED), ASan+UBSan..."
	$(SANITIZE_ENV) NPAD_FUZZ_ITERS=$(FUZZ_ITERS) NPAD_FUZZ_SEED=$(FUZZ_SEED) $(SANITIZE_RUN) ./$(SAN_DIR)/test_fuzz

# The suites with extra sources depend on them in every build
tests/test_richedit_text tests/test_richedit_text.exe $(SAN_DIR)/test_richedit_text: $(TEST_EXTRA_DEPS_richedit_text)
tests/test_editor_load tests/test_editor_load.exe $(SAN_DIR)/test_editor_load: $(TEST_EXTRA_DEPS_editor_load)
tests/test_editor tests/test_editor.exe $(SAN_DIR)/test_editor: $(TEST_EXTRA_DEPS_editor)
tests/test_recovery tests/test_recovery.exe $(SAN_DIR)/test_recovery: $(TEST_EXTRA_DEPS_recovery)
tests/test_editor_insert tests/test_editor_insert.exe $(SAN_DIR)/test_editor_insert: $(TEST_EXTRA_DEPS_editor_insert)
tests/test_editor_reload tests/test_editor_reload.exe $(SAN_DIR)/test_editor_reload: $(TEST_EXTRA_DEPS_editor_reload)

# Cleanup
clean:
	rm -f $(WINDOWS_GUI_TARGET) $(WINDOWS_TERMINAL_TARGET) $(MACOS_TARGET) $(LINUX_GTK_TARGET) $(LINUX_TERMINAL_TARGET)
	rm -f npad-*.exe npad-*linux* npad-*win32*
	find src tests -name '*.o' -delete 2>/dev/null || true
	rm -f src/platform/npad.res
	rm -rf build
	rm -rf dist/npad_* dist/npad-*.rpm dist/npad-*.pkg.tar.zst dist/npad-v*-portable-linux-*.tar.gz
	rm -f $(patsubst %,tests/test_%,$(TEST_SUITES))
	rm -f $(patsubst %,tests/test_%.exe,$(TEST_SUITES)) tests/test_assoc_win32.exe tests/test_richedit_boundary.exe tests/test_win32_unicode.exe
	rm -rf test_editor_home test_recovery_home

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
	@echo "  linux            - Build the Linux desktop app (+ the ncurses stub if ncurses is installed)"
	@echo "  linux-gtk        - Build the Linux desktop app (GTK 3; X11 and Wayland)"
	@echo "  linux-terminal   - Build the Linux Terminal stub (ncurses; needs libncurses-dev)"
	@echo "  terminal         - Build all Terminal variants (Windows + Linux)"
	@echo "  macos            - Build the macOS stub (Cocoa; needs macOS + Xcode; not functional yet)"
	@echo "  debug            - Build with debug symbols"
	@echo "  lint             - Run code linting"
	@echo "  format           - Format code with clang-format"
	@echo "  format-check     - Check code formatting"
	@echo "  test             - Run all tests"
	@echo "  test-file-ops    - Run file operations tests"
	@echo "  test-error       - Run error system tests"
	@echo "  test-encoding    - Run encoding/line-ending tests"
	@echo "  test-undo        - Run undo history tests"
	@echo "  test-editor      - Run editor core tests (open/save/auto-save, stub UI)"
	@echo "  test-windows     - Build the suite as Windows .exe files (runs them on Windows)"
	@echo "  test-sanitize    - Run the suite under AddressSanitizer + UBSan"
	@echo "  test-fuzz        - Run the seeded property/fuzz tests (FUZZ_ITERS, FUZZ_SEED)"
	@echo "  test-platform-stubs - Check the stub backends define every ui_platform_* function"
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
	@echo "  LLVM_MINGW       - llvm-mingw install dir for windows-arm64 (instead of PATH)"
	@echo "  WIN_LINKER       - bfd or lld (default: lld when MINGW_CC is clang)"
	@echo "  CPPCHECK         - cppcheck binary (CI uses 2.13; older is less strict)"
	@echo "  CLANG_FORMAT     - clang-format binary (CI uses 18)"
	@echo "  DEBUG=1          - Enable debug build"
	@echo "  VERSION          - Version string (auto-detected from git)"

.PHONY: all windows windows-gui windows-arm64 windows-terminal linux linux-gtk linux-terminal terminal macos debug debug-windows debug-linux debug-linux-gtk clean install uninstall deb rpm pacman tarball lint format format-check test test-file-ops test-atomic-save test-error test-encoding test-session test-list-ops test-update-check test-settings test-html-md test-undo test-find-seed test-tab-stops test-settings-import test-richedit-text test-editor-load test-editor test-recovery test-editor-insert test-text-scan test-editor-reload test-ui-rules test-print-core test-time-format test-platform-stubs test-fuzz-quick test-windows test-windows-build test-assoc-win32 test-richedit-win32 test-win32-unicode test-sanitize test-fuzz help detect-platform
