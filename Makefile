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

# Source files
CORE_SOURCES = src/core/editor.c src/core/file_ops.c src/core/settings.c
SHARED_SOURCES = src/ui_interface.c

# Windows specific
WINDOWS_SOURCES = src/platform/ui_win32.c
WINDOWS_LIBS = -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32 -lcomctl32
WINDOWS_TARGET = npad.exe

# macOS specific (future)
MACOS_SOURCES = src/platform/ui_cocoa.m
MACOS_LIBS = -framework Cocoa
MACOS_TARGET = npad

# Linux specific (future)
LINUX_SOURCES = src/platform/ui_x11.c
LINUX_LIBS = -lX11
LINUX_TARGET = npad

# ncurses specific (future)
NCURSES_SOURCES = src/platform/ui_ncurses.c
NCURSES_LIBS = -lncurses
NCURSES_TARGET = npad-term

# Version information
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "v0.1.0-dev")
CFLAGS += -DNPAD_VERSION=\"$(VERSION)\"

# Debug build support
ifdef DEBUG
CFLAGS += -g -DDEBUG -O0
LDFLAGS += -g
endif

# Default target
all: detect-platform

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
windows: $(WINDOWS_TARGET)

$(WINDOWS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(WINDOWS_LIBS) $(LDFLAGS)
	@echo "Windows build complete: $(WINDOWS_TARGET)"

macos: $(MACOS_TARGET)

$(MACOS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(MACOS_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(MACOS_LIBS) $(LDFLAGS)
	@echo "macOS build complete: $(MACOS_TARGET)"

linux: $(LINUX_TARGET)

$(LINUX_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LINUX_LIBS) $(LDFLAGS)
	@echo "Linux build complete: $(LINUX_TARGET)"

terminal: $(NCURSES_TARGET)

$(NCURSES_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(NCURSES_SOURCES) src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(NCURSES_LIBS) $(LDFLAGS)
	@echo "Terminal build complete: $(NCURSES_TARGET)"

# Development targets
debug: 
	$(MAKE) DEBUG=1

debug-windows:
	$(MAKE) windows DEBUG=1

debug-linux:
	$(MAKE) linux DEBUG=1

# Cross-compilation targets
windows-x64:
	$(MAKE) windows CC=x86_64-w64-mingw32-gcc

windows-x86:
	$(MAKE) windows CC=i686-w64-mingw32-gcc

# Code quality
lint:
	@command -v cppcheck >/dev/null 2>&1 || { echo "cppcheck not found. Install with: sudo apt-get install cppcheck"; exit 1; }
	cppcheck --enable=all --std=c99 --platform=win32A \
		--suppress=missingIncludeSystem \
		--suppress=unusedFunction \
		--error-exitcode=1 \
		src/

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found. Install with: sudo apt-get install clang-format"; exit 1; }
	find src/ -name "*.c" -o -name "*.h" | xargs clang-format -i

format-check:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found. Install with: sudo apt-get install clang-format"; exit 1; }
	find src/ -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror

# Cleanup
clean:
	rm -f $(WINDOWS_TARGET) $(MACOS_TARGET) $(LINUX_TARGET) $(NCURSES_TARGET)
	rm -f npad-*.exe npad-*-x64 npad-*-x86
	rm -f *.o src/**/*.o

# Installation
install: $(WINDOWS_TARGET)
ifeq ($(OS),Windows_NT)
	copy $(WINDOWS_TARGET) C:\Windows\System32\
else
	install -D $(LINUX_TARGET) $(DESTDIR)/usr/local/bin/npad
endif

uninstall:
ifeq ($(OS),Windows_NT)
	del C:\Windows\System32\$(WINDOWS_TARGET) 2>nul || echo "File not found"
else
	rm -f $(DESTDIR)/usr/local/bin/npad
endif

# Help
help:
	@echo "npad build system"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Auto-detect platform and build"
	@echo "  windows          - Build for Windows"
	@echo "  windows-x64      - Cross-compile for Windows x64"
	@echo "  windows-x86      - Cross-compile for Windows x86"
	@echo "  linux            - Build for Linux"
	@echo "  macos            - Build for macOS"
	@echo "  terminal         - Build terminal version"
	@echo "  debug            - Build with debug symbols"
	@echo "  lint             - Run code linting"
	@echo "  format           - Format code with clang-format"
	@echo "  format-check     - Check code formatting"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install to system"
	@echo "  uninstall        - Remove from system"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC               - C compiler to use"
	@echo "  DEBUG=1          - Enable debug build"
	@echo "  VERSION          - Version string (auto-detected from git)"

.PHONY: all windows macos linux terminal debug debug-windows debug-linux windows-x64 windows-x86 clean install uninstall lint format format-check help detect-platform