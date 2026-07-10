/*
 * npad - Lightweight cross-platform text editor
 * Main entry point
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#ifdef NPAD_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h>

// DPI awareness function declarations for older SDK compatibility
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT) -4)
#endif

// Function declarations to avoid warnings
__declspec(dllimport) BOOL WINAPI SetProcessDPIAware(void);
#endif

#include "core/editor.h"
#include "core/error.h"
#include "core/settings.h"
#include "core/thread_safety.h"
#include "ui_interface.h"

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

static void parse_command_line(int argc, char *argv[]);
static void show_help(void);
static void show_version(void);

// Cascade offset for a window spawned to reopen an extra crashed session
// (0 for a normally launched window). Set from --cascade.
int g_cascade_index = 0;

int main(int argc, char *argv[]) {
    DEBUG_PRINT("npad starting...");

#ifdef NPAD_PLATFORM_WINDOWS
    // Enable high DPI awareness as early as possible (before any UI calls)
    // Try different DPI awareness methods in order of preference
    HMODULE user32 = GetModuleHandle("user32.dll");
    HMODULE shcore = LoadLibrary("shcore.dll");

    // Windows 10 1703+ - Per-Monitor V2 (best option)
    if (user32) {
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextFunc pSetProcessDpiAwarenessContext;

        // Use intermediate void* cast to suppress warning
        FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        pSetProcessDpiAwarenessContext = (SetProcessDpiAwarenessContextFunc) (void *) proc;

        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        // Windows 8.1+ fallback - Per-Monitor V1
        else if (shcore) {
            typedef HRESULT(WINAPI * SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);
            SetProcessDpiAwarenessFunc pSetProcessDpiAwareness;

            proc = GetProcAddress(shcore, "SetProcessDpiAwareness");
            pSetProcessDpiAwareness = (SetProcessDpiAwarenessFunc) (void *) proc;

            if (pSetProcessDpiAwareness) {
                pSetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            }
        }
        // Windows Vista+ fallback - System DPI aware
        else {
            SetProcessDPIAware();
        }
    }

    if (shcore) {
        FreeLibrary(shcore);
    }
#endif

    // Parse command line arguments
    parse_command_line(argc, argv);

    // Initialize thread safety
    if (!thread_safety_init()) {
        fprintf(stderr, "Failed to initialize thread safety\n");
        return 1;
    }

    // Initialize settings
    if (!settings_init()) {
        fprintf(stderr, "Failed to initialize settings\n");
        thread_safety_cleanup();
        return 1;
    }

    // Initialize the UI system
    if (!ui_init()) {
        NPAD_ERROR_FATAL(NPAD_ERROR_UI, 0, "main initialization", "Failed to initialize UI system");
        settings_cleanup();
        thread_safety_cleanup();
        npad_error_cleanup();
        return 1;
    }

    // Initialize the editor core
    if (!editor_init()) {
        NPAD_ERROR_FATAL(NPAD_ERROR_EDITOR, 0, "main initialization",
                         "Failed to initialize editor system");
        ui_cleanup();
        settings_cleanup();
        thread_safety_cleanup();
        npad_error_cleanup();
        return 1;
    }

    // Create main window
    Window *main_window = ui_create_main_window();
    if (!main_window) {
        NPAD_ERROR_FATAL(NPAD_ERROR_UI, 0, "main initialization", "Failed to create main window");
        editor_cleanup();
        ui_cleanup();
        settings_cleanup();
        thread_safety_cleanup();
        npad_error_cleanup();
        return 1;
    }

    // Window geometry: use the saved state if present, otherwise a large
    // DPI-correct default centred on the work area (first run).
    int x, y, width, height;
    bool maximized = false;
    if (settings_has_key("window_width")) {
        settings_load_window_state(&x, &y, &width, &height, &maximized);
    } else {
        ui_get_default_window_rect(&x, &y, &width, &height);
    }

    // Cascade recovery windows so restored sessions do not stack exactly
    if (g_cascade_index > 0) {
        x += g_cascade_index * 80;
        y += g_cascade_index * 80;
    }

    ui_set_window_size(main_window, width, height);
    ui_set_window_position(main_window, x, y);

    // Attach the window to the editor before it becomes visible so early
    // UI events always see a valid main window
    editor_set_main_window(main_window);

    // Show the window, restoring maximized state if applicable
    ui_show_window(main_window);
    if (maximized) {
        ui_set_window_maximized(main_window, true);
    }

    // Open startup file if specified
    extern char *g_startup_file;
    if (g_startup_file) {
        editor_open_file(g_startup_file);
    }

    DEBUG_PRINT("Entering message loop...");

    // Main message loop
    int result = ui_message_loop();

    DEBUG_PRINT("Message loop exited with code: %d", result);

    // Save window state (restored geometry plus maximized flag)
    ui_get_window_position(main_window, &x, &y);
    ui_get_window_size(main_window, &width, &height);
    settings_save_window_state(x, y, width, height, ui_is_window_maximized(main_window));

    // Save settings
    settings_save();

    // Cleanup
    editor_cleanup();
    ui_cleanup();
    settings_cleanup();
    thread_safety_cleanup();
    npad_error_cleanup();

    DEBUG_PRINT("npad shutting down");

    return result;
}

static void parse_command_line(int argc, char *argv[]) {
    // Handle command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help();
            exit(0);
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            show_version();
            exit(0);
        } else if (strcmp(argv[i], "--recover") == 0) {
            // Internal: reopen a specific crash-recovery slot in this instance
            if (i + 1 < argc) {
                editor_set_recover_slot(argv[++i]);
            }
        } else if (strcmp(argv[i], "--cascade") == 0) {
            // Internal: stagger this recovery window's position
            if (i + 1 < argc) {
                g_cascade_index = atoi(argv[++i]);
            }
        } else if (argv[i][0] != '-') {
            // Assume it's a filename to open
            editor_set_startup_file(argv[i]);
        } else {
            NPAD_ERROR_ERROR(NPAD_ERROR_INVALID_PARAM, 0, argv[i],
                             "Unknown command line option: %s", argv[i]);
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Use --help for usage information\n");
            exit(1);
        }
    }
}

static void show_help(void) {
    printf("npad - Lightweight cross-platform text editor\n");
    printf("Usage: npad [options] [filename]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("\n");
    printf("If no filename is specified, npad starts with a new document.\n");
}

static void show_version(void) {
#ifdef NPAD_VERSION
    printf("npad version %s\n", NPAD_VERSION);
#else
    printf("npad version 0.1.0-dev\n");
#endif
    printf("Built on %s at %s\n", __DATE__, __TIME__);
    printf("https://github.com/platima/npad\n");
    printf("\nFollows Semantic Versioning (SemVer) - https://semver.org/\n");
}
