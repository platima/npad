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
    double ms; // Monotonic milliseconds (absolute; deltas taken vs mark 0)
} ProfMark;

static ProfMark g_marks[STARTUP_PROF_MAX];
static int g_mark_count = 0;

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

void startup_prof_mark(const char *phase) {
    if (!phase || g_mark_count >= STARTUP_PROF_MAX) {
        return;
    }
    g_marks[g_mark_count].phase = phase;
    g_marks[g_mark_count].ms = now_ms();
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
    return g_marks[i].ms - g_marks[0].ms;
}
