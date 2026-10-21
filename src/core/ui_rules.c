/*
 * npad - Window and dialog rules
 * See ui_rules.h.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_rules.h"
#include "settings.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int clamp_ll(long long value, int lo, int hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return (int) value;
}

int ui_clamp_auto_save_interval(long long seconds) {
    return clamp_ll(seconds, AUTO_SAVE_INTERVAL_MIN, AUTO_SAVE_INTERVAL_MAX);
}

int ui_clamp_session_interval(long long seconds) {
    return clamp_ll(seconds, SESSION_INTERVAL_MIN, SESSION_INTERVAL_MAX);
}

int ui_clamp_large_file_mb(long long mb) {
    return clamp_ll(mb, LARGE_FILE_MB_MIN, LARGE_FILE_MB_MAX);
}

// A stored number as a long long. settings_get_int is atoi, whose result for
// a value beyond int is undefined in C and in practice wraps negative - which
// is how a huge large-file threshold silently turned the warning off. strtoll
// saturates instead, and the callers clamp to their real range.
static long long setting_number(const char *key, long long default_value) {
    char *text = settings_get_string(key, NULL);
    if (!text)
        return default_value;
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    bool parsed = end && end != text;
    free(text);
    return parsed ? value : default_value; // ERANGE leaves LLONG_MIN/MAX: still clamps right
}

int ui_auto_save_interval_setting(void) {
    return ui_clamp_auto_save_interval(
        setting_number(AUTO_SAVE_INTERVAL_KEY, AUTO_SAVE_INTERVAL_DEFAULT));
}

int ui_session_interval_setting(void) {
    return ui_clamp_session_interval(
        setting_number(SESSION_INTERVAL_KEY, SESSION_INTERVAL_DEFAULT));
}

int ui_large_file_mb_setting(int default_mb) {
    return ui_clamp_large_file_mb(setting_number(LARGE_FILE_MB_KEY, default_mb));
}

unsigned int ui_interval_to_timer_ms(int seconds) {
    if (seconds <= 0)
        return 0;
    unsigned long long ms = (unsigned long long) seconds * 1000ull;
    if (ms > UI_TIMER_MAX_MS)
        ms = UI_TIMER_MAX_MS;
    return (unsigned int) ms;
}

// ---------------------------------------------------------------------------
// Font size
// ---------------------------------------------------------------------------

int ui_font_size_clamp_tenths(int tenths) {
    return clamp_ll(tenths, FONT_SIZE_MIN_PT * 10, FONT_SIZE_MAX_PT * 10);
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

int ui_font_size_parse_tenths(const char *text, int default_tenths) {
    if (!text)
        return ui_font_size_clamp_tenths(default_tenths);

    const char *p = text;
    while (is_space(*p))
        p++;
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p++;
    }

    // Whole points; anything past a few digits is far beyond the clamp, so
    // stop accumulating rather than overflow
    long long points = 0;
    bool any_digit = false;
    for (; is_digit(*p); p++) {
        any_digit = true;
        if (points < 1000000)
            points = points * 10 + (*p - '0');
    }
    long long tenths = points * 10;

    if (*p == '.') {
        p++;
        if (is_digit(*p)) {
            any_digit = true;
            tenths += *p - '0';
            p++;
            if (is_digit(*p) && *p >= '5')
                tenths++; // Round to the nearest tenth
            while (is_digit(*p))
                p++;
        }
    }

    if (!any_digit)
        return ui_font_size_clamp_tenths(default_tenths);
    return clamp_ll(negative ? -tenths : tenths, FONT_SIZE_MIN_PT * 10, FONT_SIZE_MAX_PT * 10);
}

void ui_font_size_format_tenths(int tenths, char *buf, size_t size) {
    if (!buf || size == 0)
        return;
    tenths = ui_font_size_clamp_tenths(tenths);
    if (tenths % 10 == 0)
        snprintf(buf, size, "%d", tenths / 10);
    else
        snprintf(buf, size, "%d.%d", tenths / 10, tenths % 10);
}

int ui_font_size_setting_tenths(void) {
    char *text = settings_get_string(FONT_SIZE_KEY, NULL);
    int tenths = ui_font_size_parse_tenths(text, FONT_SIZE_DEFAULT_TENTHS);
    free(text);
    return tenths;
}

void ui_font_size_set_tenths(int tenths) {
    char buf[16];
    ui_font_size_format_tenths(tenths, buf, sizeof(buf));
    settings_set_string(FONT_SIZE_KEY, buf);
}

int ui_font_size_tenths_to_points(int tenths) {
    return (ui_font_size_clamp_tenths(tenths) + 5) / 10;
}

// ---------------------------------------------------------------------------
// Go To Line
// ---------------------------------------------------------------------------

GotoLineResult ui_goto_line_parse(const char *text, long long line_count, int *line_out) {
    if (!text)
        return GOTO_LINE_INVALID;
    if (line_count < 1)
        line_count = 1; // Even an empty document has line 1

    const char *p = text;
    while (is_space(*p))
        p++;
    if (!is_digit(*p))
        return GOTO_LINE_INVALID;

    long long value = 0;
    bool too_big = false;
    for (; is_digit(*p); p++) {
        if (!too_big) {
            value = value * 10 + (*p - '0');
            if (value > INT_MAX)
                too_big = true;
        }
    }
    while (is_space(*p))
        p++;
    if (*p != '\0')
        return GOTO_LINE_INVALID;

    if (too_big || value < 1 || value > line_count)
        return GOTO_LINE_OUT_OF_RANGE;
    if (line_out)
        *line_out = (int) value;
    return GOTO_LINE_OK;
}

// ---------------------------------------------------------------------------
// Window placement
// ---------------------------------------------------------------------------

bool ui_fit_title_bar(int *x, int *y, int width, int caption, int grip, const UiRect *work) {
    if (!x || !y || !work || work->right <= work->left || work->bottom <= work->top)
        return false;

    long long nx = *x, ny = *y;
    long long w = width > 0 ? width : 0;
    long long cap = caption > 0 ? caption : 0;
    long long g = grip > 0 ? grip : 0;
    long long work_w = (long long) work->right - work->left;
    if (g > w)
        g = w;
    if (g > work_w)
        g = work_w;

    long long max_top = (long long) work->bottom - cap;
    if (max_top < work->top)
        max_top = work->top;
    if (ny < work->top)
        ny = work->top;
    else if (ny > max_top)
        ny = max_top;

    if (nx + w < (long long) work->left + g)
        nx = (long long) work->left + g - w;
    else if (nx > (long long) work->right - g)
        nx = (long long) work->right - g;

    bool moved = nx != *x || ny != *y;
    *x = (int) nx;
    *y = (int) ny;
    return moved;
}
