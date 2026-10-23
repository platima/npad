/*
 * npad - Chunked text scan tests
 * The line counting behind the Win32 status bar's Ln/Col with Word Wrap on,
 * and the surrogate check that decides whether a font change must rebind
 * emoji faces. Both are fed the document a chunk at a time, so every case is
 * also run with the text split at every possible point.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/text_scan.h"
#include <string.h>

// Widen an ASCII string to UTF-16 units (enough for these cases)
static size_t widen(const char *s, uint16_t *out, size_t cap) {
    size_t n = 0;
    while (s[n] && n < cap) {
        out[n] = (uint16_t) (unsigned char) s[n];
        n++;
    }
    return n;
}

// Scan units[0..end) in two feeds split at `split`
static LineScan scan_split(const uint16_t *units, size_t end, size_t split) {
    LineScan s;
    line_scan_init(&s);
    if (split > end)
        split = end;
    line_scan_feed(&s, units, split);
    line_scan_feed(&s, units + split, end - split);
    return s;
}

TEST_CASE(column_is_measured_from_the_line_start) {
    // NP-058: "first line" + Enter + "sec" showed Col 15, the caret's offset
    // in the whole document, instead of Col 4
    uint16_t u[64];
    size_t n = widen("first line\rsec", u, 64);
    LineScan s;
    line_scan_init(&s);
    line_scan_feed(&s, u, n);
    TEST_ASSERT_EQ(1, s.breaks, "one break before the caret");
    TEST_ASSERT_EQ(11, s.line_start, "line 2 starts after the CR");
    TEST_ASSERT_EQ(14, s.consumed, "caret offset");
    TEST_ASSERT_EQ(4, s.consumed - s.line_start + 1, "Col 4, not Col 15");
}

TEST_CASE(first_line_starts_at_zero) {
    uint16_t u[16];
    size_t n = widen("abc", u, 16);
    LineScan s;
    line_scan_init(&s);
    line_scan_feed(&s, u, n);
    TEST_ASSERT_EQ(0, s.breaks, "no break");
    TEST_ASSERT_EQ(0, s.line_start, "line 1 starts at 0");
    TEST_ASSERT_EQ(4, s.consumed - s.line_start + 1, "end of line 1 is Col 4");
}

TEST_CASE(caret_right_after_a_break) {
    uint16_t u[16];
    size_t n = widen("ab\r\r", u, 16);
    LineScan s;
    line_scan_init(&s);
    line_scan_feed(&s, u, n);
    TEST_ASSERT_EQ(2, s.breaks, "two breaks: the caret is on line 3");
    TEST_ASSERT_EQ(4, s.line_start, "an empty line starts where the caret is");
    TEST_ASSERT_EQ(1, s.consumed - s.line_start + 1, "Col 1");
}

TEST_CASE(only_cr_is_a_break) {
    // RichEdit stores breaks as bare CR; a stray LF is just a character
    uint16_t u[16];
    size_t n = widen("a\nb", u, 16);
    LineScan s;
    line_scan_init(&s);
    line_scan_feed(&s, u, n);
    TEST_ASSERT_EQ(0, s.breaks, "LF does not count");
    TEST_ASSERT_EQ(0, s.line_start, "still line 1");
}

TEST_CASE(any_chunking_gives_the_same_answer) {
    uint16_t u[64];
    size_t n = widen("one\rtwo two\r\rfour\rfive five", u, 64);
    LineScan whole;
    line_scan_init(&whole);
    line_scan_feed(&whole, u, n);
    for (size_t split = 0; split <= n; split++) {
        LineScan s = scan_split(u, n, split);
        TEST_ASSERT_EQ(whole.breaks, s.breaks, "breaks independent of the split");
        TEST_ASSERT_EQ(whole.line_start, s.line_start, "line start independent of the split");
        TEST_ASSERT_EQ(whole.consumed, s.consumed, "offset independent of the split");
    }
    TEST_ASSERT_EQ(4, whole.breaks, "four breaks");
    TEST_ASSERT_EQ(18, whole.line_start, "line 5 starts after the fourth CR");
}

TEST_CASE(null_and_empty_feeds_are_harmless) {
    const uint16_t empty[1] = { 0x0D };
    LineScan s;
    line_scan_init(&s);
    line_scan_feed(&s, NULL, 5);
    line_scan_feed(&s, empty, 0);
    line_scan_feed(NULL, empty, 1);
    TEST_ASSERT_EQ(0, s.breaks, "nothing counted");
    TEST_ASSERT_EQ(0, s.consumed, "nothing consumed");
}

TEST_CASE(astral_pair_in_one_chunk) {
    // "a" U+1F600 "b": D83D DE00
    const uint16_t u[] = { 0x61, 0xD83D, 0xDE00, 0x62 };
    AstralScan a;
    astral_scan_init(&a);
    astral_scan_feed(&a, u, 4);
    TEST_ASSERT(a.found, "an emoji is found");
}

TEST_CASE(astral_pair_split_across_chunks) {
    const uint16_t u[] = { 0x61, 0xD83D, 0xDE00, 0x62 };
    for (size_t split = 0; split <= 4; split++) {
        AstralScan a;
        astral_scan_init(&a);
        astral_scan_feed(&a, u, split);
        astral_scan_feed(&a, u + split, 4 - split);
        TEST_ASSERT(a.found, "found wherever the chunk boundary falls");
    }
}

TEST_CASE(bmp_text_and_lone_surrogates_are_not_astral) {
    const uint16_t bmp[] = { 0x61, 0x00E9, 0x65E5, 0x20AC };
    const uint16_t lone_high[] = { 0x61, 0xD83D, 0x62 };
    const uint16_t lone_low[] = { 0xDE00, 0x61 };
    const uint16_t reversed[] = { 0xDE00, 0xD83D };
    AstralScan a;
    astral_scan_init(&a);
    astral_scan_feed(&a, bmp, 4);
    TEST_ASSERT(!a.found, "accented, CJK and euro are all in the BMP");
    astral_scan_init(&a);
    astral_scan_feed(&a, lone_high, 3);
    TEST_ASSERT(!a.found, "a high surrogate followed by a letter");
    astral_scan_init(&a);
    astral_scan_feed(&a, lone_low, 2);
    TEST_ASSERT(!a.found, "a low surrogate with nothing before it");
    astral_scan_init(&a);
    astral_scan_feed(&a, reversed, 2);
    TEST_ASSERT(!a.found, "low then high is not a pair");
    TEST_ASSERT(a.pending_high, "but the trailing high may still pair with the next chunk");
    const uint16_t next[] = { 0xDC00 };
    astral_scan_feed(&a, next, 1);
    TEST_ASSERT(a.found, "and does");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(column_is_measured_from_the_line_start);
    RUN_TEST(first_line_starts_at_zero);
    RUN_TEST(caret_right_after_a_break);
    RUN_TEST(only_cr_is_a_break);
    RUN_TEST(any_chunking_gives_the_same_answer);
    RUN_TEST(null_and_empty_feeds_are_harmless);
    RUN_TEST(astral_pair_in_one_chunk);
    RUN_TEST(astral_pair_split_across_chunks);
    RUN_TEST(bmp_text_and_lone_surrogates_are_not_astral);

    TEST_SUMMARY();
    return 0;
}
