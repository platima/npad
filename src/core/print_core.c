/*
 * npad - Printing logic that does not depend on a platform
 * See print_core.h.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "print_core.h"

#include <stdlib.h>
#include <string.h>

// How many output runs and logical lines pass between keep_going calls
#define KEEP_GOING_EVERY 256

bool print_is_high_surrogate(uint16_t unit) {
    return unit >= 0xD800 && unit <= 0xDBFF;
}

bool print_is_low_surrogate(uint16_t unit) {
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

// Whether the units [start, start+len) end between the two halves of a pair
static bool splits_pair(const uint16_t *text, size_t start, int len, size_t limit) {
    size_t last = start + (size_t) len - 1;
    return len > 0 && last + 1 < limit && print_is_high_surrogate(text[last]) &&
           print_is_low_surrogate(text[last + 1]);
}

// Where the next output line goes, and how the page fills up
typedef struct {
    int page;
    int line;
} Cursor;

static void cursor_break(const PrintPaginateGeometry *g, Cursor *c) {
    c->line++;
    if (c->line >= g->lines_per_page) {
        c->page++;
        c->line = 0;
    }
}

// The first measure for a run: what is likely to fit in the width left, with
// headroom, so a line costs a little more than its own length to measure
// rather than PRINT_MEASURE_MAX units every time. It grows if it all fits.
static int first_chunk(int avail, int avg_char_width, size_t remaining) {
    int chunk = 256;
    if (avg_char_width > 0) {
        int likely = avail / avg_char_width;
        chunk = likely + likely / 2 + 16;
    }
    if (chunk > PRINT_MEASURE_MAX)
        chunk = PRINT_MEASURE_MAX;
    if (chunk < 1)
        chunk = 1;
    if ((size_t) chunk > remaining)
        chunk = (int) remaining;
    return chunk;
}

PrintPaginateResult print_paginate(const uint16_t *text, size_t total,
                                   const PrintPaginateGeometry *geometry,
                                   const PrintPaginateOps *ops, int *page_count) {
    if (page_count)
        *page_count = 1;
    if (!geometry || !ops || !ops->fit || !ops->emit)
        return PRINT_PAGINATE_NO_MEMORY;
    if (!text)
        total = 0;

    PrintPaginateGeometry g = *geometry;
    if (g.wrap_width < 1)
        g.wrap_width = 1;
    if (g.tab_width < 1)
        g.tab_width = 1;
    if (g.lines_per_page < 1)
        g.lines_per_page = 1;

    Cursor c = { 1, 0 };
    int last_page = 1;
    int *dx = NULL;
    int dx_cap = 0;
    PrintPaginateResult result = PRINT_PAGINATE_OK;
    unsigned since_check = 0;

    size_t pos = 0;
    while (total > 0) {
        size_t eol = pos;
        while (eol < total && text[eol] != '\r' && text[eol] != '\n')
            eol++;

        size_t i = pos;
        size_t seg_end = pos; // End of the tab-free run i is in; found once per run
        int x = 0;
        while (i < eol) {
            if (text[i] == '\t') {
                // Tabs advance to the next stop rather than drawing a glyph
                int next = ((x / g.tab_width) + 1) * g.tab_width;
                if (next < g.wrap_width) {
                    x = next;
                } else if (x > 0) {
                    cursor_break(&g, &c); // The stop is past the column: wrap
                    x = 0;
                }
                // Already at the margin with no stop that fits: stay put, or
                // every tab would burn a blank line of its own
                i++;
                continue;
            }

            // Scanning for the next tab from every output line made a long
            // tab-free line quadratic; the end is found once and reused for
            // every line it wraps onto
            if (seg_end <= i) {
                seg_end = i;
                while (seg_end < eol && text[seg_end] != '\t')
                    seg_end++;
            }
            size_t remaining = seg_end - i;

            int avail = g.wrap_width - x;
            if (avail <= 0) {
                cursor_break(&g, &c);
                x = 0;
                avail = g.wrap_width;
            }

            // Measure what is likely to fit, and more only while all of it
            // does, so the break is found within the measured span
            int chunk = first_chunk(avail, g.avg_char_width, remaining);
            int fit;       // Both set by the first pass of the loop, which
            bool measured; // always runs
            for (;;) {
                // A measure that stops between the halves of a pair would
                // weigh half a character. Take the whole pair: growing, never
                // shrinking, so this loop always moves forward.
                if (splits_pair(text, i, chunk, seg_end))
                    chunk++;
                if (chunk > dx_cap) {
                    int *grown = realloc(dx, (size_t) chunk * sizeof(int));
                    if (!grown) {
                        result = PRINT_PAGINATE_NO_MEMORY;
                        goto done;
                    }
                    dx = grown;
                    dx_cap = chunk;
                }
                fit = chunk;
                measured = ops->fit(ops->ctx, text + i, chunk, avail, &fit, dx);
                if (!measured) {
                    fit = chunk; // dx is then undefined; the width is re-measured below
                    break;
                }
                if (fit < 0)
                    fit = 0;
                if (fit > chunk)
                    fit = chunk;
                if (fit < chunk || (size_t) chunk >= remaining || chunk >= PRINT_MEASURE_MAX)
                    break;
                size_t grow = (size_t) chunk * 2;
                if (grow > remaining)
                    grow = remaining;
                if (grow > PRINT_MEASURE_MAX)
                    grow = PRINT_MEASURE_MAX;
                chunk = (int) grow;
            }

            bool wrapped = (fit < chunk);
            if (fit < 1) {
                if (x > 0) {
                    // Nothing fits in what is left of the line: start a new one
                    // and measure again from the margin
                    cursor_break(&g, &c);
                    x = 0;
                    continue;
                }
                // A single character wider than the column: emit it, both
                // halves of it if it is a pair
                fit = splits_pair(text, i, 1, seg_end) ? 2 : 1;
                wrapped = true;
            } else if (wrapped) {
                // Break after a space rather than mid-word, when there is one.
                // text[i + fit] is inside the run: fit < chunk <= remaining.
                int brk = fit;
                while (brk > 0 && text[i + (size_t) brk] != ' ')
                    brk--;
                if (brk > 0) {
                    fit = brk + 1; // The space stays at the end of the line
                } else if (fit > 1 && splits_pair(text, i, fit, seg_end)) {
                    fit--; // Mid-word: but never between the halves of a pair
                } else if (splits_pair(text, i, fit, seg_end)) {
                    // Only the high half fits in what is left of the line
                    if (x > 0) {
                        // Mid-line (after a tab, or a run cut at the measure
                        // cap): the pair starts the next line instead of
                        // running past the margin
                        cursor_break(&g, &c);
                        x = 0;
                        continue;
                    }
                    fit = 2; // The pair alone is wider than the line
                }
            }

            if (!ops->emit(ops->ctx, c.page, c.line, x, i, fit)) {
                result = PRINT_PAGINATE_NO_MEMORY;
                goto done;
            }
            // One enormous line is as long a job as many short ones
            if (ops->keep_going && ++since_check >= KEEP_GOING_EVERY) {
                since_check = 0;
                if (!ops->keep_going(ops->ctx)) {
                    result = PRINT_PAGINATE_CANCELLED;
                    goto done;
                }
            }
            if (c.page > last_page)
                last_page = c.page;
            i += (size_t) fit;

            if (wrapped) {
                // The rest of this run goes on a new line, so the pen position
                // is about to be reset - there is no need to measure it. (dx
                // is only guaranteed for the units that FIT, and the space
                // after them can make fit one more than that.)
                if (i < eol) {
                    cursor_break(&g, &c);
                    x = 0;
                }
                // If nothing is left, the end-of-line break below covers it.
                // Breaking here as well would insert a blank line.
            } else if (measured) {
                x += dx[fit - 1];
            } else {
                int width = 0;
                if (ops->extent && ops->extent(ops->ctx, text + i - fit, fit, &width))
                    x += width;
                else
                    x = g.wrap_width; // Unmeasurable: force a break rather than overlap
            }
        }

        cursor_break(&g, &c); // The logical line ends here

        if (ops->keep_going && ++since_check >= KEEP_GOING_EVERY) {
            since_check = 0;
            if (!ops->keep_going(ops->ctx)) {
                result = PRINT_PAGINATE_CANCELLED;
                goto done;
            }
        }

        if (eol >= total)
            break;
        pos = eol;
        if (text[pos] == '\r')
            pos++;
        if (pos < total && text[pos] == '\n')
            pos++;
        if (pos >= total)
            break; // A trailing break leaves one empty line and nothing after
    }

done:
    free(dx);
    if (page_count)
        *page_count = last_page;
    return result;
}

// ---------------------------------------------------------------------------
// Margin units
// ---------------------------------------------------------------------------

// round(v * num / den) for the non-negative and negative values a hand-edited
// settings file might hold, without floating point
static int scale_round(int v, int num, int den) {
    long long p = (long long) v * num;
    long long q = (p >= 0) ? (p + den / 2) / den : -((-p + den / 2) / den);
    if (q > 2147483647LL)
        return 2147483647;
    if (q < -2147483647LL)
        return -2147483647;
    return (int) q;
}

int print_margin_from_mm100(int mm100) {
    // 1 in = 2540 hundredths of a millimetre
    return scale_round(mm100, 1000, 2540);
}

int print_margin_to_mm100(int thousandths_inch) {
    int shown = scale_round(thousandths_inch, 254, 100);
    // The whole tenth of a millimetre that is stored as this value, if there
    // is one: the figure the user typed. Never more than 0.01 mm from the
    // exact conversion, and only one such figure can be that close.
    if (shown <= -2147483000 || shown >= 2147483000)
        return shown;
    int base = (shown / 10) * 10;
    for (int candidate = base - 10; candidate <= base + 10; candidate += 10) {
        if (print_margin_from_mm100(candidate) == thousandths_inch) {
            int d = candidate - shown;
            if (d >= -1 && d <= 1)
                return candidate;
        }
    }
    return shown;
}

// ---------------------------------------------------------------------------
// Print Preview wheel
// ---------------------------------------------------------------------------

int print_wheel_pages(int *accumulator, int delta) {
    if (!accumulator)
        return 0;
    if ((delta > 0 && *accumulator < 0) || (delta < 0 && *accumulator > 0))
        *accumulator = 0; // Turned round: start again in the new direction
    *accumulator += delta;
    int notches = *accumulator / PRINT_WHEEL_NOTCH; // Toward zero: the remainder waits
    *accumulator -= notches * PRINT_WHEEL_NOTCH;
    // The wheel turned away from the user scrolls up, toward page 1
    return -notches;
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

bool print_switch_matches(const char *arg, bool allow_slash) {
    if (!arg)
        return false;
    if (strcmp(arg, "-p") == 0 || strcmp(arg, "--print") == 0)
        return true;
    return allow_slash && (strcmp(arg, "/p") == 0 || strcmp(arg, "/P") == 0);
}
