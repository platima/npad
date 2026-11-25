/*
 * npad - Tab stops
 * The platform-independent half of "Tab inserts spaces". Notepad's Tab key
 * types a tab character; with the preference on, npad types the spaces that
 * reach the next tab stop instead. Every UI layer (Win32, GTK, later ncurses
 * and Cocoa) asks the same questions here, so the rule, the width and the
 * preference are identical on every platform:
 *
 *   - tab_inserts_spaces_enabled(): is the preference on? (key
 *     tab_inserts_spaces, default OFF: classic Notepad types a tab)
 *   - tab_width_setting(): the configured stop spacing, clamped to
 *     TAB_WIDTH_MIN..TAB_WIDTH_MAX (key tab_width, default 4)
 *   - tab_column_after() / tab_spaces_to_stop(): the arithmetic
 *
 * A column is a code point counted from the start of the line, so a
 * character outside the BMP is one column (as on GTK) rather than two UTF-16
 * units, and a literal tab already on the line advances to the next stop like
 * any other. Wide (East Asian) glyphs are still one column each: the rule is
 * about characters, not about how wide a font draws them.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef TAB_STOPS_H
#define TAB_STOPS_H

#include <stdbool.h>
#include <stddef.h>

// Settings keys, shared by every platform and its Preferences UI
#define TAB_INSERTS_SPACES_KEY "tab_inserts_spaces"
#define TAB_WIDTH_KEY "tab_width"

#define TAB_WIDTH_MIN 1
#define TAB_WIDTH_MAX 16
#define TAB_WIDTH_DEFAULT 4

// Is "Tab inserts spaces" on? Default false (Notepad types a tab character).
bool tab_inserts_spaces_enabled(void);

// The configured tab stop spacing in columns, always within
// TAB_WIDTH_MIN..TAB_WIDTH_MAX (a missing, zero, negative or huge value from a
// hand-edited settings file falls back to / is clamped to a usable width).
int tab_width_setting(void);

// width forced into TAB_WIDTH_MIN..TAB_WIDTH_MAX.
int tab_clamp_width(int width);

// The column reached after the len bytes of UTF-8 text at line_prefix, which
// must start at the beginning of a line (anything before a '\n' or '\r' inside
// it is ignored). Each code point is one column; a '\t' advances to the next
// multiple of width. Continuation bytes never count, so invalid UTF-8 cannot
// inflate the column. width is clamped.
size_t tab_column_after(const char *line_prefix, size_t len, int width);

// The width of one step of the Markdown list tools' Spaces indent format:
// the configured tab width while "Tab inserts spaces" is on, so Tab and the
// list indent agree, otherwise the historic four (list_ops.h).
int tab_list_indent_width(void);

// How many spaces a Tab pressed at column reaches the next stop with: always
// 1..width (a caret already on a stop moves a whole width on, never zero).
int tab_spaces_to_stop(size_t column, int width);

// Convenience: tab_spaces_to_stop(tab_column_after(line_prefix, len, width),
// width).
int tab_spaces_for_line_prefix(const char *line_prefix, size_t len, int width);

// --- Displayed tab stops (Notepad's Edit control rule) ----------------------
// A literal tab is drawn to the next of the stops an Edit control uses: one
// every TAB_EDIT_COLUMNS average character widths, measured in device pixels
// at the size the text is drawn. The arithmetic is here so it can be tested;
// the measuring is the platform layer's.

#define TAB_EDIT_COLUMNS 8

// GdiGetCharDimensions' average character width, which an Edit control spaces
// its default tab stops by, from the device-pixel extent of the 52 letters
// A-Z a-z. 0 for a non-positive extent.
long tab_edit_average_width(long letters_extent);

// The em height in device pixels of a font height_twips tall drawn at dpi and
// zoom zoom_num/zoom_den, rounded to nearest as MulDiv does. A dpi of 0 counts
// as 96 and a non-positive zoom term as 100%.
long tab_font_pixel_height(long height_twips, long dpi, long zoom_num, long zoom_den);

// The tab stop spacing in twips that a control scaling twips by dpi and zoom
// draws at stop_px pixels. Rounded DOWN when the ratio is not exact, so the
// stop lands at most a fraction of a pixel early, never late: a stop even a
// fraction past 8 columns would catch "12345678<TAB>" on that same stop
// instead of the next one. 0 for a non-positive stop_px.
long tab_stop_twips_for_pixels(long stop_px, long dpi, long zoom_num, long zoom_den);

#endif // TAB_STOPS_H
