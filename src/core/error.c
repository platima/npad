/*
 * npad - Error Handling System Implementation
 * Centralized error reporting and logging
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "error.h"
#include "thread_safety.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Global error state
static npad_error_info_t g_last_error = { 0 };
static npad_error_callback_t g_error_callback = NULL;
static bool g_error_initialized = false;
static npad_mutex_t g_error_mutex;

// Default error callback - prints to stderr
static void default_error_callback(const npad_error_info_t *error) {
    if (!error)
        return;

    // Get current timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // Format error message
    const char *level_str = npad_error_level_to_string(error->level);
    const char *category_str = npad_error_category_to_string(error->category);

    fprintf(stderr, "[%s] %s:%s [%s:%d in %s()] %s\n", timestamp, level_str, category_str,
            error->file, error->line, error->function, error->message);

    if (strlen(error->context) > 0) {
        fprintf(stderr, "  Context: %s\n", error->context);
    }

    if (error->code != 0) {
        fprintf(stderr, "  Error Code: %d\n", error->code);
    }

    // Flush stderr to ensure immediate output
    fflush(stderr);

    // For fatal errors, abort the program
    if (error->level == NPAD_ERROR_FATAL) {
        fprintf(stderr, "Fatal error encountered. Terminating program.\n");
        fflush(stderr);
        abort();
    }
}

void npad_error_init(void) {
    if (g_error_initialized)
        return;

    if (!npad_mutex_init(&g_error_mutex)) {
        // Can't use our error system here, use basic stderr
        fprintf(stderr, "Failed to initialize error system mutex\n");
        return;
    }

    g_error_callback = default_error_callback;
    g_error_initialized = true;
}

void npad_error_cleanup(void) {
    if (!g_error_initialized)
        return;

    npad_mutex_destroy(&g_error_mutex);
    g_error_initialized = false;
}

void npad_error_set_callback(npad_error_callback_t callback) {
    if (!g_error_initialized)
        return;

    npad_mutex_lock(&g_error_mutex);
    g_error_callback = callback ? callback : default_error_callback;
    npad_mutex_unlock(&g_error_mutex);
}

void npad_error_report(npad_error_level_t level, npad_error_category_t category, int code,
                       const char *file, int line, const char *function, const char *context,
                       const char *format, ...) {
    if (!g_error_initialized) {
        npad_error_init();
    }

    npad_mutex_lock(&g_error_mutex);

    // Clear previous error
    memset(&g_last_error, 0, sizeof(g_last_error));

    // Set basic error info
    g_last_error.level = level;
    g_last_error.category = category;
    g_last_error.code = code;
    g_last_error.line = line;

    // Copy file name (basename only for cleaner output)
    const char *basename = strrchr(file, '/');
    if (!basename)
        basename = strrchr(file, '\\');
    basename = basename ? basename + 1 : file;
    snprintf(g_last_error.file, sizeof(g_last_error.file), "%.63s", basename);

    // Copy function name
    snprintf(g_last_error.function, sizeof(g_last_error.function), "%.63s",
             function ? function : "unknown");

    // Copy context
    snprintf(g_last_error.context, sizeof(g_last_error.context), "%.255s", context ? context : "");

    // Format error message
    va_list args;
    va_start(args, format);
    vsnprintf(g_last_error.message, sizeof(g_last_error.message), format, args);
    va_end(args);

    // Call error callback
    if (g_error_callback) {
        g_error_callback(&g_last_error);
    }

    npad_mutex_unlock(&g_error_mutex);
}

const npad_error_info_t *npad_error_get_last(void) {
    if (!g_error_initialized)
        return NULL;
    return &g_last_error;
}

bool npad_error_has_error(void) {
    if (!g_error_initialized)
        return false;
    return g_last_error.level >= NPAD_ERROR_ERROR;
}

void npad_error_clear(void) {
    if (!g_error_initialized)
        return;

    npad_mutex_lock(&g_error_mutex);
    memset(&g_last_error, 0, sizeof(g_last_error));
    npad_mutex_unlock(&g_error_mutex);
}

const char *npad_error_level_to_string(npad_error_level_t level) {
    switch (level) {
        case NPAD_ERROR_INFO:
            return "INFO";
        case NPAD_ERROR_WARNING:
            return "WARN";
        case NPAD_ERROR_ERROR:
            return "ERROR";
        case NPAD_ERROR_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

const char *npad_error_category_to_string(npad_error_category_t category) {
    switch (category) {
        case NPAD_ERROR_NONE:
            return "NONE";
        case NPAD_ERROR_MEMORY:
            return "MEMORY";
        case NPAD_ERROR_FILE_IO:
            return "FILE_IO";
        case NPAD_ERROR_NETWORK:
            return "NETWORK";
        case NPAD_ERROR_INVALID_PARAM:
            return "INVALID_PARAM";
        case NPAD_ERROR_SYSTEM:
            return "SYSTEM";
        case NPAD_ERROR_UI:
            return "UI";
        case NPAD_ERROR_EDITOR:
            return "EDITOR";
        case NPAD_ERROR_SETTINGS:
            return "SETTINGS";
        case NPAD_ERROR_THREAD:
            return "THREAD";
        case NPAD_ERROR_UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
    }
}