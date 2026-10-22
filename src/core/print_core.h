/*
 * npad - Printing logic that does not depend on a platform
 *
 * The parts of Print, Print Preview and Page Setup that are pure arithmetic,
 * kept here so they can be tested without a printer, a window or a font:
 *
 *   - print_paginate(): Notepad-style line wrapping and pagination of UTF-16
 *     text against a caller-supplied measuring function. The Win32 build
 *     measures on the printer's information context with GDI; the tests
 *     measure with a fake font. Linear in the length of the text, however
 *     long a line is and however many tabs it has, and never splits a
 *     surrogate pair.
 *   - print_margin_*: thousandths of an inch (how margins are stored) to and
 *     from hundredths of a millimetre (what a metric Page Setup dialog shows).
 *   - print_wheel_pages(): mouse wheel and touchpad deltas to whole page flips.
 *   - print_switch_matches(): the /p command-line switch.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef PRINT_CORE_H
#define PRINT_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

// Longest run measured at once. The measure is sized from the width that is
// left on the line and grows only while everything still fits, so this cap is
// reached only by a line that really is thousands of characters wide.
#define PRINT_MEASURE_MAX 4096

typedef struct {
    // How many of the len units at s fit within max_extent, and the extent
    // after each unit that fits (dx[0..*fit-1], cumulative). dx has room for
    // len entries. Returns false when the text cannot be measured; *fit is
    // then ignored.
    bool (*fit)(void *ctx, const uint16_t *s, int len, int max_extent, int *fit, int *dx);
    // The total extent of len units. Returns false when it cannot be measured.
    bool (*extent)(void *ctx, const uint16_t *s, int len, int *width);
    // One drawn run: len units at offset, on 1-based page, on 0-based line of
    // that page, starting x units from the left of the text area. Returns
    // false to stop with PRINT_PAGINATE_NO_MEMORY.
    bool (*emit)(void *ctx, int page, int line, int x, size_t offset, int len);
    // Optional. Asked every so often during a long job; returning false stops
    // it with PRINT_PAGINATE_CANCELLED. NULL never cancels.
    bool (*keep_going)(void *ctx);
    void *ctx;
} PrintPaginateOps;

typedef struct {
    int wrap_width;     // Width of the text area, in the measuring unit
    int tab_width;      // Distance between tab stops
    int lines_per_page; // Lines that fit on one page
    int avg_char_width; // A typical character's width, to size the first
                        // measure of a line; 0 or less if unknown
} PrintPaginateGeometry;

typedef enum {
    PRINT_PAGINATE_OK,
    PRINT_PAGINATE_NO_MEMORY, // An allocation failed, or emit returned false
    PRINT_PAGINATE_CANCELLED, // keep_going returned false
} PrintPaginateResult;

// Wrap and paginate total units of text. Lines end at CR, LF or CRLF; a
// trailing line break does not add an empty line. Tabs advance to the next
// stop and draw nothing. A line breaks after the last space that fits, or
// mid-word when there is none; a single character wider than the column is
// emitted on a line of its own. *page_count, when given, receives the last
// page anything was drawn on (at least 1).
PrintPaginateResult print_paginate(const uint16_t *text, size_t total,
                                   const PrintPaginateGeometry *geometry,
                                   const PrintPaginateOps *ops, int *page_count);

// UTF-16 surrogate halves
bool print_is_high_surrogate(uint16_t unit);
bool print_is_low_surrogate(uint16_t unit);

// ---------------------------------------------------------------------------
// Margin units
// ---------------------------------------------------------------------------

// Thousandths of an inch to hundredths of a millimetre, as a metric Page Setup
// dialog shows it. A stored value can only be a whole thousandth (0.0254 mm),
// so a value the user typed in tenths of a millimetre comes back up to 0.01 mm
// off; such a value is shown as the round figure that was typed (20.00, not
// 19.99). The defaults, 0.75 in and 1 in, convert exactly and are left alone.
int print_margin_to_mm100(int thousandths_inch);

// Hundredths of a millimetre to the nearest thousandth of an inch
int print_margin_from_mm100(int mm100);

// ---------------------------------------------------------------------------
// Print Preview wheel
// ---------------------------------------------------------------------------

// One notch of a mouse wheel, as Windows reports it (WHEEL_DELTA)
#define PRINT_WHEEL_NOTCH 120

// Add delta (positive away from the user) to *accumulator and return how many
// pages to move: positive forward (toward the end), negative back. A notched
// wheel moves one page per notch; a touchpad's stream of small deltas moves a
// page only once a notch's worth has built up, instead of one page per event.
// Turning the other way discards what had built up in the old direction.
int print_wheel_pages(int *accumulator, int delta);

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

// Whether arg is the print switch: "-p" or "--print", and with allow_slash
// (Windows) also Notepad's own "/p" or "/P".
bool print_switch_matches(const char *arg, bool allow_slash);

#endif // PRINT_CORE_H
