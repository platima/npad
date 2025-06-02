# npad Makefile
# Cross-platform text editor

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2

# Platform detection
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Source files
CORE_SOURCES = src/core/editor.c src/core/file_ops.c src/core/settings.c
SHARED_SOURCES = src/ui_interface.c

# Windows specific
WINDOWS_SOURCES = src/platform/ui_win32.c
WINDOWS_LIBS = -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32
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

# Default target
all: detect-platform

detect-platform:
ifeq ($(OS),Windows_NT)
	$(MAKE) windows
else ifeq ($(UNAME_S),Darwin)
	$(MAKE) macos
else ifeq ($(UNAME_S),Linux)
	$(MAKE) linux
endif

# Platform-specific builds
windows: $(WINDOWS_TARGET)

$(WINDOWS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(WINDOWS_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(WINDOWS_LIBS)
	@echo "Windows build complete: $(WINDOWS_TARGET)"

macos: $(MACOS_TARGET)

$(MACOS_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(MACOS_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(MACOS_LIBS)
	@echo "macOS build complete: $(MACOS_TARGET)"

linux: $(LINUX_TARGET)

$(LINUX_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(LINUX_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LINUX_LIBS)
	@echo "Linux build complete: $(LINUX_TARGET)"

terminal: $(NCURSES_TARGET)

$(NCURSES_TARGET): $(CORE_SOURCES) $(SHARED_SOURCES) $(NCURSES_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(NCURSES_LIBS)
	@echo "Terminal build complete: $(NCURSES_TARGET)"

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: windows

clean:
	rm -f $(WINDOWS_TARGET) $(MACOS_TARGET) $(LINUX_TARGET) $(NCURSES_TARGET)
	rm -f *.o src/**/*.o

install: $(WINDOWS_TARGET)
ifeq ($(OS),Windows_NT)
	copy $(WINDOWS_TARGET) C:\Windows\System32\
else
	cp $(TARGET) /usr/local/bin/
endif

.PHONY: all windows macos linux terminal debug clean install detect-platform