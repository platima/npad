/*
 * npad - Print core tests
 * Pagination (wrapping, tabs, pages, surrogate pairs, linear cost), margin
 * unit conversion, the preview's wheel accumulator and the /p switch
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/print_core.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// A fake font and a run recorder
// ---------------------------------------------------------------------------

typedef struct {
    int page, line, x;
    size_t offset;
    int len;
} Run;

typedef struct {
    Run *runs;
    int count;
    int cap;
    long long measured_units; // Every unit handed to fit()
    long fit_calls;
    bool unmeasurable;       // fit() fails, extent() works
    int fail_emit_after;     // emit() fails once this many runs exist (0: never)
    int keep_going_calls;
    int cancel_after_checks; // keep_going() says stop on this call (0: never)
} Rec;

// 'i' is narrow, 'W' is wide, everything else (each surrogate half too) is 10
static int unit_width(uint16_t u) {
    if (u == 'i')
        return 3;
    if (u == 'W')
        return 20;
    return 10;
}

static bool fake_fit(void *ctx, const uint16_t *s, int len, int max_extent, int *fit, int *dx) {
    Rec *r = ctx;
    r->fit_calls++;
    r->measured_units += len;
    if (r->unmeasurable)
        return false;
    int total = 0, n = 0;
    while (n < len) {
        int w = total + unit_width(s[n]);
        if (w > max_extent)
            break;
        total = w;
        dx[n] = total;
        n++;
    }
    *fit = n;
    return true;
}

static bool fake_extent(void *ctx, const uint16_t *s, int len, int *width) {
    (void) ctx;
    int total = 0;
    for (int i = 0; i < len; i++)
        total += unit_width(s[i]);
    *width = total;
    return true;
}

static bool rec_emit(void *ctx, int page, int line, int x, size_t offset, int len) {
    Rec *r = ctx;
    if (r->fail_emit_after > 0 && r->count >= r->fail_emit_after)
        return false;
    if (r->count == r->cap) {
        int cap = r->cap ? r->cap * 2 : 64;
        Run *grown = realloc(r->runs, (size_t) cap * sizeof(Run));
        if (!grown)
            return false;
        r->runs = grown;
        r->cap = cap;
    }
    Run run = { page, line, x, offset, len };
    r->runs[r->count++] = run;
    return true;
}

static bool rec_keep_going(void *ctx) {
    Rec *r = ctx;
    r->keep_going_calls++;
    return !(r->cancel_after_checks > 0 && r->keep_going_calls >= r->cancel_after_checks);
}

static void rec_free(Rec *r) {
    free(r->runs);
    memset(r, 0, sizeof(*r));
}

// ASCII to UTF-16 units; caller frees
static uint16_t *u16(const char *s, size_t *len) {
    size_t n = strlen(s);
    uint16_t *out = malloc((n + 1) * sizeof(uint16_t));
    for (size_t i = 0; i < n; i++)
        out[i] = (uint16_t) (unsigned char) s[i];
    out[n] = 0;
    *len = n;
    return out;
}

static PrintPaginateResult paginate(Rec *r, const uint16_t *text, size_t len, int wrap, int tab,
                                    int lines, int avg, int *pages) {
    PrintPaginateOps ops = { fake_fit, fake_extent, rec_emit, rec_keep_going, r };
    PrintPaginateGeometry g = { wrap, tab, lines, avg };
    return print_paginate(text, len, &g, &ops, pages);
}

static PrintPaginateResult paginate_str(Rec *r, const char *s, int wrap, int lines, int *pages) {
    size_t len;
    uint16_t *text = u16(s, &len);
    PrintPaginateResult result = paginate(r, text, len, wrap, 80, lines, 10, pages);
    free(text);
    return result;
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

TEST_CASE(empty_text_is_one_blank_page) {
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "", 1000, 50, &pages), "ok");
    TEST_ASSERT_EQ(0, r.count, "no runs");
    TEST_ASSERT_EQ(1, pages, "one page");
    rec_free(&r);
}

TEST_CASE(wraps_after_the_last_space_that_fits) {
    Rec r = { 0 };
    int pages = 0;
    // 11 units of width 10 fit in 110: "hello world" fits, the space after it
    // hangs at the end of the line, "foo" goes on the next
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "hello world foo", 110, 50, &pages), "ok");
    TEST_ASSERT_EQ(2, r.count, "two lines");
    TEST_ASSERT_EQ(0, (int) r.runs[0].offset, "first run starts the text");
    TEST_ASSERT_EQ(12, r.runs[0].len, "the space stays on the first line");
    TEST_ASSERT_EQ(12, (int) r.runs[1].offset, "second run starts at foo");
    TEST_ASSERT_EQ(3, r.runs[1].len, "foo");
    TEST_ASSERT_EQ(1, r.runs[1].line, "on the next line");
    TEST_ASSERT_EQ(0, r.runs[1].x, "from the margin");
    rec_free(&r);
}

TEST_CASE(breaks_mid_word_when_there_is_no_space) {
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "abcdefghijklmnop", 50, 50, &pages),
                   "ok");
    TEST_ASSERT_EQ(4, r.count, "16 units at 5 per line");
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQ(5, r.runs[i].len, "full lines of 5");
    TEST_ASSERT_EQ(1, r.runs[3].len, "the remainder");
    rec_free(&r);
}

TEST_CASE(line_breaks_blank_lines_and_trailing_break) {
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "a\r\n\r\nb\rc\n", 1000, 50, &pages),
                   "ok");
    TEST_ASSERT_EQ(3, r.count, "a, b, c");
    TEST_ASSERT_EQ(0, r.runs[0].line, "a on line 0");
    TEST_ASSERT_EQ(2, r.runs[1].line, "a blank line, then b");
    TEST_ASSERT_EQ(3, r.runs[2].line, "a bare CR is a line break too");
    TEST_ASSERT_EQ(1, pages, "one page");
    rec_free(&r);
}

TEST_CASE(pages_fill_in_order) {
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "1\n2\n3\n4\n5", 1000, 2, &pages), "ok");
    TEST_ASSERT_EQ(5, r.count, "five lines");
    TEST_ASSERT_EQ(3, pages, "two lines a page");
    int expect_page[5] = { 1, 1, 2, 2, 3 };
    int expect_line[5] = { 0, 1, 0, 1, 0 };
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQ(expect_page[i], r.runs[i].page, "page");
        TEST_ASSERT_EQ(expect_line[i], r.runs[i].line, "line on page");
    }
    rec_free(&r);
}

TEST_CASE(tabs_advance_to_stops_and_draw_nothing) {
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "a\tbc\td", 1000, 50, &pages), "ok");
    TEST_ASSERT_EQ(3, r.count, "three runs, no tab glyphs");
    TEST_ASSERT_EQ(0, r.runs[0].x, "a at the margin");
    TEST_ASSERT_EQ(80, r.runs[1].x, "bc at the first stop");
    TEST_ASSERT_EQ(160, r.runs[2].x, "d at the second stop");
    TEST_ASSERT_EQ(0, r.runs[2].line, "all on one line");
    rec_free(&r);

    // A stop past the column wraps instead
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "abcdefghij\tk", 100, 50, &pages), "ok");
    TEST_ASSERT_EQ(2, r.count, "two runs");
    TEST_ASSERT_EQ(1, r.runs[1].line, "k wraps to the next line");
    TEST_ASSERT_EQ(0, r.runs[1].x, "at the margin");
    rec_free(&r);
}

// The review measured a simulation of the old loop at 5.95G scan steps and
// 48.7M measured characters for a 1M-character line
TEST_CASE(a_long_line_costs_linear_time) {
    const size_t n = 1000000;
    uint16_t *text = malloc(n * sizeof(uint16_t));
    for (size_t i = 0; i < n; i++)
        text[i] = 'x';
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, n, 1000, 80, 60, 10, &pages), "ok");
    TEST_ASSERT_EQ(10000, r.count, "100 units a line");
    TEST_ASSERT(r.measured_units < (long long) n * 3, "measured at most ~2x the text, not 40x");
    TEST_ASSERT_EQ((10000 + 59) / 60, pages, "60 lines a page");
    for (int i = 0; i < r.count; i++) {
        if (r.runs[i].len != 100) {
            TEST_ASSERT(false, "every line is full");
        }
    }
    rec_free(&r);

    // The same with a tab every 50 units: the run end is found once per run
    for (size_t i = 50; i < n; i += 51)
        text[i] = '\t';
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, n, 1000, 80, 60, 10, &pages), "ok");
    TEST_ASSERT(r.measured_units < (long long) n * 3, "tabs do not make it quadratic");
    rec_free(&r);
    free(text);
}

TEST_CASE(narrow_glyphs_grow_the_measure) {
    // avg 10 but every glyph 3 wide: far more fit than the first measure
    // guessed, and the line must still be as full as it can be
    const size_t n = 2000;
    uint16_t *text = malloc(n * sizeof(uint16_t));
    for (size_t i = 0; i < n; i++)
        text[i] = 'i';
    Rec r = { 0 };
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, n, 1000, 80, 60, 10, &pages), "ok");
    TEST_ASSERT_EQ(333, r.runs[0].len, "333 narrow glyphs fit in 1000");
    TEST_ASSERT_EQ(333, r.runs[1].len, "and on the next line");
    rec_free(&r);

    // A space well past the first measure is still the break point
    text[300] = ' ';
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, n, 1000, 80, 60, 10, &pages), "ok");
    TEST_ASSERT_EQ(301, r.runs[0].len, "broken after the space at 300");
    rec_free(&r);
    free(text);
}

// Text of `pairs` copies of U+1F600 (D83D DE00)
static uint16_t *emoji_line(int pairs, size_t *len) {
    uint16_t *text = malloc((size_t) pairs * 2 * sizeof(uint16_t));
    for (int i = 0; i < pairs; i++) {
        text[2 * i] = 0xD83D;
        text[2 * i + 1] = 0xDE00;
    }
    *len = (size_t) pairs * 2;
    return text;
}

static bool runs_keep_pairs_whole(const Rec *r, const uint16_t *text) {
    for (int i = 0; i < r->count; i++) {
        const Run *run = &r->runs[i];
        if (run->len <= 0)
            return false;
        if (print_is_low_surrogate(text[run->offset]))
            return false;
        if (print_is_high_surrogate(text[run->offset + (size_t) run->len - 1]))
            return false;
    }
    return true;
}

TEST_CASE(wrapping_never_splits_a_surrogate_pair) {
    size_t len;
    uint16_t *text = emoji_line(101, &len);
    Rec r = { 0 };
    int pages = 0;
    // 21 units of width 10 fit in 210: the 21st is a high surrogate
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, len, 210, 80, 60, 10, &pages), "ok");
    TEST_ASSERT(runs_keep_pairs_whole(&r, text), "no run ends inside a pair");
    TEST_ASSERT_EQ(20, r.runs[0].len, "ten whole pairs on the first line");
    rec_free(&r);

    // Odd widths of every size, and both first-measure guesses
    for (int wrap = 15; wrap < 400; wrap += 7) {
        for (int avg = 0; avg <= 10; avg += 10) {
            TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, len, wrap, 80, 60, avg, &pages),
                           "ok");
            TEST_ASSERT(runs_keep_pairs_whole(&r, text), "no run ends inside a pair");
            size_t covered = 0;
            for (int i = 0; i < r.count; i++)
                covered += (size_t) r.runs[i].len;
            TEST_ASSERT_EQ((long) len, (long) covered, "every unit is drawn exactly once");
            rec_free(&r);
        }
    }
    free(text);
}

TEST_CASE(a_pair_wider_than_the_line_is_emitted_whole) {
    size_t len;
    uint16_t *text = emoji_line(3, &len);
    Rec r = { 0 };
    int pages = 0;
    // One half (10) fits in 15, the pair (20) does not
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, len, 15, 80, 60, 10, &pages), "ok");
    TEST_ASSERT_EQ(3, r.count, "one pair a line");
    TEST_ASSERT(runs_keep_pairs_whole(&r, text), "pairs kept whole");
    rec_free(&r);
    // Nothing fits at all
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate(&r, text, len, 5, 80, 60, 10, &pages), "ok");
    TEST_ASSERT_EQ(3, r.count, "still one pair a line");
    TEST_ASSERT(runs_keep_pairs_whole(&r, text), "pairs kept whole");
    rec_free(&r);
    free(text);
}

TEST_CASE(keep_going_can_cancel_a_long_job) {
    const size_t n = 200000;
    uint16_t *text = malloc(n * sizeof(uint16_t));
    for (size_t i = 0; i < n; i++)
        text[i] = 'x';
    Rec r = { 0 };
    r.cancel_after_checks = 3;
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_CANCELLED, paginate(&r, text, n, 1000, 80, 60, 10, &pages),
                   "one enormous line is still cancellable");
    TEST_ASSERT_EQ(3, r.keep_going_calls, "stopped at the third check");
    TEST_ASSERT(r.count < 2000, "well before the end");
    rec_free(&r);

    // Without keep_going it runs to the end
    PrintPaginateOps ops = { fake_fit, fake_extent, rec_emit, NULL, &r };
    PrintPaginateGeometry g = { 1000, 80, 60, 10 };
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, print_paginate(text, n, &g, &ops, &pages), "no callback");
    TEST_ASSERT_EQ(2000, r.count, "all of it");
    rec_free(&r);
    free(text);
}

TEST_CASE(emit_failure_stops_with_no_memory) {
    Rec r = { 0 };
    r.fail_emit_after = 2;
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_NO_MEMORY, paginate_str(&r, "a\nb\nc\nd", 1000, 50, &pages),
                   "reported");
    TEST_ASSERT_EQ(2, r.count, "stopped there");
    rec_free(&r);
}

TEST_CASE(unmeasurable_text_falls_back_to_extent) {
    Rec r = { 0 };
    r.unmeasurable = true;
    int pages = 0;
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, paginate_str(&r, "ab\tc", 1000, 50, &pages), "ok");
    TEST_ASSERT_EQ(2, r.count, "two runs");
    TEST_ASSERT_EQ(80, r.runs[1].x, "the tab stop came from the extent");
    rec_free(&r);
}

TEST_CASE(bad_arguments_do_not_crash) {
    int pages = 0;
    PrintPaginateGeometry g = { 0, 0, 0, 0 };
    TEST_ASSERT_EQ(PRINT_PAGINATE_NO_MEMORY, print_paginate(NULL, 0, &g, NULL, &pages), "no ops");
    Rec r = { 0 };
    PrintPaginateOps ops = { fake_fit, fake_extent, rec_emit, NULL, &r };
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, print_paginate(NULL, 5, &g, &ops, &pages), "no text");
    size_t len;
    uint16_t *text = u16("abc def", &len);
    // Zero width, tab width and page height are clamped to 1
    TEST_ASSERT_EQ(PRINT_PAGINATE_OK, print_paginate(text, len, &g, &ops, &pages), "zeros");
    TEST_ASSERT(r.count > 0, "still drew something");
    free(text);
    rec_free(&r);
}

// ---------------------------------------------------------------------------
// Margin units
// ---------------------------------------------------------------------------

TEST_CASE(margins_convert_between_inches_and_millimetres) {
    TEST_ASSERT_EQ(1905, print_margin_to_mm100(750), "0.75 in is 19.05 mm exactly");
    TEST_ASSERT_EQ(2540, print_margin_to_mm100(1000), "1 in is 25.4 mm");
    TEST_ASSERT_EQ(750, print_margin_from_mm100(1905), "and back");
    TEST_ASSERT_EQ(1000, print_margin_from_mm100(2540), "and back");
    TEST_ASSERT_EQ(787, print_margin_from_mm100(2000), "20 mm is stored as 0.787 in");
    TEST_ASSERT_EQ(2000, print_margin_to_mm100(787), "and shown as 20.00, not 19.99");
    TEST_ASSERT_EQ(0, print_margin_to_mm100(0), "zero");
    TEST_ASSERT_EQ(0, print_margin_from_mm100(0), "zero");
}

TEST_CASE(a_typed_metric_margin_comes_back_as_typed) {
    // Every margin a user can type in tenths of a millimetre, up to 50 cm
    for (int mm100 = 0; mm100 <= 50000; mm100 += 10) {
        int stored = print_margin_from_mm100(mm100);
        if (print_margin_to_mm100(stored) != mm100) {
            printf("\n  %d hundredths of a mm came back as %d\n", mm100,
                   print_margin_to_mm100(stored));
            TEST_ASSERT(false, "round trip");
        }
    }
    // And a value from the inch side is within a hundredth of a millimetre
    for (int thou = 0; thou <= 20000; thou++) {
        long exact100 = (long) thou * 254; // hundredths of a mm, times 100
        long shown100 = (long) print_margin_to_mm100(thou) * 100;
        long diff = shown100 - exact100;
        if (diff < -150 || diff > 150)
            TEST_ASSERT(false, "close to the exact conversion");
    }
    // A hand-edited settings file can hold anything
    (void) print_margin_to_mm100(-5);
    (void) print_margin_to_mm100(2147483647);
    (void) print_margin_from_mm100(-2147483647);
}

// ---------------------------------------------------------------------------
// Print Preview wheel
// ---------------------------------------------------------------------------

TEST_CASE(wheel_flips_one_page_per_notch) {
    int acc = 0;
    TEST_ASSERT_EQ(1, print_wheel_pages(&acc, -120), "a notch toward the user: next page");
    TEST_ASSERT_EQ(-1, print_wheel_pages(&acc, 120), "away: previous page");
    TEST_ASSERT_EQ(3, print_wheel_pages(&acc, -360), "three notches at once");
    TEST_ASSERT_EQ(0, acc, "nothing left over");
}

TEST_CASE(touchpad_deltas_build_up_to_a_page) {
    int acc = 0;
    int pages = 0;
    // A swipe: 30 events of 10
    for (int i = 0; i < 30; i++)
        pages += print_wheel_pages(&acc, -10);
    TEST_ASSERT_EQ(2, pages, "300 of delta is two pages, not thirty");
    TEST_ASSERT_EQ(-60, acc, "the rest waits");
    // Turning round discards it
    TEST_ASSERT_EQ(0, print_wheel_pages(&acc, 100), "not yet a notch the other way");
    TEST_ASSERT_EQ(100, acc, "only the new direction counts");
    TEST_ASSERT_EQ(-1, print_wheel_pages(&acc, 20), "now it is");
    TEST_ASSERT_EQ(0, print_wheel_pages(NULL, 120), "no accumulator");
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

TEST_CASE(print_switch_spellings) {
    TEST_ASSERT(print_switch_matches("-p", false), "-p");
    TEST_ASSERT(print_switch_matches("--print", false), "--print");
    TEST_ASSERT(print_switch_matches("/p", true), "/p on Windows");
    TEST_ASSERT(print_switch_matches("/P", true), "/P on Windows");
    TEST_ASSERT(!print_switch_matches("/p", false), "/p is a path on Linux");
    TEST_ASSERT(!print_switch_matches("/pt", true), "/pt is Notepad's print-to, not /p");
    TEST_ASSERT(!print_switch_matches("-print", true), "not a near miss");
    TEST_ASSERT(!print_switch_matches("p", true), "not a bare letter");
    TEST_ASSERT(!print_switch_matches(NULL, true), "NULL");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(empty_text_is_one_blank_page);
    RUN_TEST(wraps_after_the_last_space_that_fits);
    RUN_TEST(breaks_mid_word_when_there_is_no_space);
    RUN_TEST(line_breaks_blank_lines_and_trailing_break);
    RUN_TEST(pages_fill_in_order);
    RUN_TEST(tabs_advance_to_stops_and_draw_nothing);
    RUN_TEST(a_long_line_costs_linear_time);
    RUN_TEST(narrow_glyphs_grow_the_measure);
    RUN_TEST(wrapping_never_splits_a_surrogate_pair);
    RUN_TEST(a_pair_wider_than_the_line_is_emitted_whole);
    RUN_TEST(keep_going_can_cancel_a_long_job);
    RUN_TEST(emit_failure_stops_with_no_memory);
    RUN_TEST(unmeasurable_text_falls_back_to_extent);
    RUN_TEST(bad_arguments_do_not_crash);
    RUN_TEST(margins_convert_between_inches_and_millimetres);
    RUN_TEST(a_typed_metric_margin_comes_back_as_typed);
    RUN_TEST(wheel_flips_one_page_per_notch);
    RUN_TEST(touchpad_deltas_build_up_to_a_page);
    RUN_TEST(print_switch_spellings);

    TEST_SUMMARY();
    return 0;
}
