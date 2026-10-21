/*
 * npad - Window and dialog rules
 * The platform-independent arithmetic behind npad's window and dialog chrome,
 * kept here so every UI layer applies the same limits and so it can be unit
 * tested without a desktop:
 *
 *   - the bounded interval / threshold preferences (auto-save interval,
 *     recovery snapshot interval, large-file warning) and the millisecond
 *     timer value an interval becomes, which must never wrap
 *   - the font size preference, held in tenths of a point so a half-point
 *     size such as 10.5 survives a round trip
 *   - the Go To Line number check
 *   - keeping a restored window's title bar on a work area
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef UI_RULES_H
#define UI_RULES_H

#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Interval and threshold preferences
//
// These match the ranges the GTK Preferences spin buttons already enforce. A
// value outside them - typed into the Win32 edit box, or hand-edited into
// settings.json - is clamped, never wrapped: a 4294968-second interval used to
// become a 704 ms timer once multiplied into a 32-bit millisecond count.
// ---------------------------------------------------------------------------

#define AUTO_SAVE_INTERVAL_KEY "auto_save_interval"
#define AUTO_SAVE_INTERVAL_MIN 10
#define AUTO_SAVE_INTERVAL_MAX 86400
#define AUTO_SAVE_INTERVAL_DEFAULT 300

#define SESSION_INTERVAL_KEY "session_interval"
#define SESSION_INTERVAL_MIN 5
#define SESSION_INTERVAL_MAX 86400
#define SESSION_INTERVAL_DEFAULT 30

#define LARGE_FILE_MB_KEY "large_file_warning_mb"
#define LARGE_FILE_MB_MIN 0 // 0 turns the warning off
#define LARGE_FILE_MB_MAX 1048576

// value forced into each preference's range
int ui_clamp_auto_save_interval(long long seconds);
int ui_clamp_session_interval(long long seconds);
int ui_clamp_large_file_mb(long long mb);

// The stored preference, read without int overflow (a value too large for an
// int clamps to the maximum instead of turning negative) and clamped. A missing
// or non-numeric value gives the default (default_mb for the threshold, whose
// default depends on the machine's memory).
int ui_auto_save_interval_setting(void);
int ui_session_interval_setting(void);
int ui_large_file_mb_setting(int default_mb);

// The largest timer period worth asking a platform for, in milliseconds
// (Win32's USER_TIMER_MAXIMUM, and well inside a guint for GLib)
#define UI_TIMER_MAX_MS 0x7FFFFFFFu

// seconds as a timer period in milliseconds, computed in 64 bits and capped at
// UI_TIMER_MAX_MS; 0 for a non-positive interval (no timer).
unsigned int ui_interval_to_timer_ms(int seconds);

// ---------------------------------------------------------------------------
// Font size
//
// "font_size" is stored as points in decimal ("11", "10.5"), so a whole-point
// size reads back identically in older versions (which parse it as an
// integer). Internally sizes are tenths of a point - what the Win32 font
// chooser reports - so a half point is never lost.
// ---------------------------------------------------------------------------

#define FONT_SIZE_KEY "font_size"
#define FONT_SIZE_MIN_PT 6
#define FONT_SIZE_MAX_PT 72
#define FONT_SIZE_DEFAULT_TENTHS 110

// tenths forced into FONT_SIZE_MIN_PT..FONT_SIZE_MAX_PT (in tenths)
int ui_font_size_clamp_tenths(int tenths);

// Parse a stored size ("11", "10.5", " 9.25 ") into tenths of a point,
// locale-independently (a decimal point, never a comma), rounding to the
// nearest tenth. NULL, empty or non-numeric text gives default_tenths. The
// result is clamped.
int ui_font_size_parse_tenths(const char *text, int default_tenths);

// Format tenths of a point the way it is stored: "11" or "10.5". The value is
// clamped first. buf should hold at least 8 bytes.
void ui_font_size_format_tenths(int tenths, char *buf, size_t size);

// The "font_size" preference in tenths of a point, clamped; and the setter,
// which clamps and stores it in the format above.
int ui_font_size_setting_tenths(void);
void ui_font_size_set_tenths(int tenths);

// Whole points for an API that only takes points (printing), rounding a half
// point up.
int ui_font_size_tenths_to_points(int tenths);

// ---------------------------------------------------------------------------
// Go To Line
// ---------------------------------------------------------------------------

typedef enum {
    GOTO_LINE_OK = 0,
    GOTO_LINE_OUT_OF_RANGE, // A number, but 0 or past the last line (or past INT_MAX)
    GOTO_LINE_INVALID,      // Empty, or not a plain decimal number
} GotoLineResult;

// Check the text typed into Go To Line against a document of line_count lines
// (1-based; a document always has at least one). Surrounding spaces are
// ignored; anything else must be decimal digits. Numbers too large for an int
// are out of range rather than wrapping (they used to jump to the top of a
// word-wrapped document). *line_out is set only for GOTO_LINE_OK.
GotoLineResult ui_goto_line_parse(const char *text, long long line_count, int *line_out);

// ---------------------------------------------------------------------------
// Window placement
// ---------------------------------------------------------------------------

typedef struct {
    int left, top, right, bottom;
} UiRect;

// Move a restored window at (*x, *y) of the given width so its title bar can
// still be reached on work (the work area of the monitor it is on or nearest):
// the top edge is kept between the work area's top and caption pixels above its
// bottom, and at least grip pixels of the window's width stay inside it
// horizontally. A window that already satisfies this is not moved, so a
// deliberately partly off-screen position survives as long as it can be
// grabbed. Returns true if it moved the window.
bool ui_fit_title_bar(int *x, int *y, int width, int caption, int grip, const UiRect *work);

#endif // UI_RULES_H
