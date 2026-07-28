/*
 * npad - Startup profiling implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef _WIN32
// clock_gettime/CLOCK_MONOTONIC are POSIX, hidden by plain -std=c99
#define _POSIX_C_SOURCE 199309L
#endif

#include "startup_prof.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct {
    const char *phase;
    double ms; // Monotonic milliseconds (absolute; deltas taken vs process start)
} ProfMark;

static ProfMark g_marks[STARTUP_PROF_MAX];
static int g_mark_count = 0;

// Monotonic-clock value corresponding to when the OS created this process.
// Timing from the first mark would hide everything the loader does before
// main() runs - image and dependent-DLL loading, CRT init, side-by-side
// manifest activation, and any anti-malware inspection of the image - which is
// exactly where an unexplained slow start tends to live.
static double g_process_start_ms = 0.0;
static int g_base_ready = 0;

static double now_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (double) counter.QuadPart * 1000.0 / (double) freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1000000.0;
#endif
}

// Milliseconds this process has already been alive, or -1 when unavailable.
// Wall-clock and the monotonic clock are different time bases, so this is
// measured as an elapsed span and then projected onto the monotonic clock.
static double age_ms(void) {
#ifdef _WIN32
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1.0;
    }
    GetSystemTimeAsFileTime(&now);
    ULONGLONG c = ((ULONGLONG) created.dwHighDateTime << 32) | created.dwLowDateTime;
    ULONGLONG n = ((ULONGLONG) now.dwHighDateTime << 32) | now.dwLowDateTime;
    if (n <= c) {
        return -1.0;
    }
    return (double) (n - c) / 10000.0; // 100ns units -> ms
#else
    return -1.0; // No portable equivalent; fall back to first-mark-relative
#endif
}

void startup_prof_mark(const char *phase) {
    if (!phase || g_mark_count >= STARTUP_PROF_MAX) {
        return;
    }
    double t = now_ms();
    if (!g_base_ready) {
        double age = age_ms();
        // Project the process-creation instant onto the monotonic clock. If the
        // OS cannot tell us, fall back to the old behaviour (first mark = zero).
        g_process_start_ms = (age >= 0.0) ? (t - age) : t;
        g_base_ready = 1;
    }
    g_marks[g_mark_count].phase = phase;
    g_marks[g_mark_count].ms = t;
    g_mark_count++;
}

int startup_prof_count(void) {
    return g_mark_count;
}

const char *startup_prof_name(int i) {
    if (i < 0 || i >= g_mark_count) {
        return "";
    }
    return g_marks[i].phase;
}

double startup_prof_ms(int i) {
    if (i < 0 || i >= g_mark_count || g_mark_count == 0) {
        return 0.0;
    }
    return g_marks[i].ms - g_process_start_ms;
}
