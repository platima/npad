/*
 * npad - Tab stop tests
 * The column arithmetic behind "Tab inserts spaces" and its two settings
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/tab_stops.h"
#include "../src/core/settings.h"
#include <string.h>

#define COL(s, w) tab_column_after((s), strlen(s), (w))
#define SPACES(s, w) tab_spaces_for_line_prefix((s), strlen(s), (w))

TEST_CASE(stop_is_next_multiple_not_fixed_count) {
    TEST_ASSERT_EQ(4, SPACES("", 4), "column 0 -> 4 spaces");
    TEST_ASSERT_EQ(3, SPACES("a", 4), "column 1 -> 3 spaces");
    TEST_ASSERT_EQ(2, SPACES("ab", 4), "column 2 -> 2 spaces");
    TEST_ASSERT_EQ(1, SPACES("abc", 4), "column 3 -> 1 space");
    TEST_ASSERT_EQ(4, SPACES("abcd", 4), "on a stop -> a whole width, never zero");
    TEST_ASSERT_EQ(3, SPACES("abcde", 4), "column 5 -> 3 spaces");
    TEST_ASSERT_EQ(8, SPACES("", 8), "width 8");
    TEST_ASSERT_EQ(1, SPACES("", 1), "width 1 is always one space");
    TEST_ASSERT_EQ(1, SPACES("xyz", 1), "width 1 mid-line");
    TEST_ASSERT_EQ(16, SPACES("", 16), "width 16");
}

TEST_CASE(existing_tab_advances_to_the_next_stop) {
    TEST_ASSERT_EQ(4, COL("\t", 4), "a leading tab is one stop");
    TEST_ASSERT_EQ(4, COL("a\t", 4), "a tab after one char reaches column 4");
    TEST_ASSERT_EQ(8, COL("abcd\t", 4), "a tab on a stop moves a whole width");
    TEST_ASSERT_EQ(8, COL("\t\t", 4), "two tabs");
    TEST_ASSERT_EQ(8, COL("a\t\t", 4), "tab, tab after a char");
}

TEST_CASE(spaces_after_existing_tab) {
    TEST_ASSERT_EQ(2, SPACES("\tab", 4), "column 6 -> 2 spaces to 8");
    TEST_ASSERT_EQ(4, SPACES("\t", 4), "column 4 -> a whole width");
    TEST_ASSERT_EQ(3, SPACES("a\tb", 4), "a<tab>b is column 5 -> 3 spaces");
}

TEST_CASE(code_points_not_bytes_or_utf16_units) {
    TEST_ASSERT_EQ(1, COL("\xC3\xA9", 4), "e-acute (2 bytes) is one column");
    TEST_ASSERT_EQ(1, COL("\xE2\x82\xAC", 4), "euro (3 bytes) is one column");
    TEST_ASSERT_EQ(1, COL("\xF0\x9F\x98\x80", 4), "emoji (4 bytes, 2 UTF-16 units) is one column");
    TEST_ASSERT_EQ(2, SPACES("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 4), "two emoji -> 2 spaces");
    TEST_ASSERT_EQ(3, SPACES("\xE6\x97\xA5", 4), "a wide CJK glyph still counts as one column");
}

TEST_CASE(invalid_utf8_cannot_inflate_the_column) {
    // A lone continuation byte contributes nothing; a lead byte counts once
    TEST_ASSERT_EQ(0, COL("\x80\x80", 4), "stray continuation bytes");
    TEST_ASSERT_EQ(1, COL("\xE2\x82", 4), "truncated sequence counts its lead byte only");
}

TEST_CASE(line_break_resets_the_column) {
    TEST_ASSERT_EQ(2, COL("abc\nab", 4), "after LF");
    TEST_ASSERT_EQ(1, COL("abc\r\na", 4), "after CRLF");
    TEST_ASSERT_EQ(0, COL("abc\r", 4), "after bare CR (RichEdit's internal break)");
}

TEST_CASE(null_and_empty_prefix) {
    TEST_ASSERT_EQ(0, tab_column_after(NULL, 5, 4), "NULL is column 0");
    TEST_ASSERT_EQ(0, tab_column_after("abc", 0, 4), "zero length is column 0");
    TEST_ASSERT_EQ(2, tab_column_after("abcdef", 2, 4), "only len bytes are read");
}

TEST_CASE(width_is_clamped_in_arithmetic) {
    TEST_ASSERT_EQ(1, tab_spaces_to_stop(7, 0), "0 behaves as 1");
    TEST_ASSERT_EQ(1, tab_spaces_to_stop(7, -3), "negative behaves as 1");
    TEST_ASSERT_EQ(16, tab_spaces_to_stop(0, 99), "huge behaves as 16");
    TEST_ASSERT_EQ(TAB_WIDTH_MIN, tab_clamp_width(-1), "clamp low");
    TEST_ASSERT_EQ(TAB_WIDTH_MAX, tab_clamp_width(17), "clamp high");
    TEST_ASSERT_EQ(8, tab_clamp_width(8), "in range untouched");
}

TEST_CASE(settings_default_off_width_four) {
    settings_clear_all();
    TEST_ASSERT(!tab_inserts_spaces_enabled(), "defaults OFF (Notepad types a tab)");
    TEST_ASSERT_EQ(4, tab_width_setting(), "default width 4");
    settings_set_bool(TAB_INSERTS_SPACES_KEY, true);
    TEST_ASSERT(tab_inserts_spaces_enabled(), "on when set");
    settings_remove_key(TAB_INSERTS_SPACES_KEY);
    TEST_ASSERT(!tab_inserts_spaces_enabled(), "back to off once removed (Reset All)");
    settings_clear_all();
}

TEST_CASE(settings_width_is_clamped) {
    settings_clear_all();
    settings_set_int(TAB_WIDTH_KEY, 2);
    TEST_ASSERT_EQ(2, tab_width_setting(), "2");
    settings_set_int(TAB_WIDTH_KEY, 0);
    TEST_ASSERT_EQ(TAB_WIDTH_MIN, tab_width_setting(), "0 from a hand-edited file");
    settings_set_int(TAB_WIDTH_KEY, -5);
    TEST_ASSERT_EQ(TAB_WIDTH_MIN, tab_width_setting(), "negative");
    settings_set_int(TAB_WIDTH_KEY, 1000);
    TEST_ASSERT_EQ(TAB_WIDTH_MAX, tab_width_setting(), "huge");
    settings_clear_all();
}

TEST_CASE(list_indent_width_follows_tab_width_only_when_on) {
    settings_clear_all();
    settings_set_int(TAB_WIDTH_KEY, 2);
    TEST_ASSERT_EQ(4, tab_list_indent_width(), "off: lists keep their four spaces");
    settings_set_bool(TAB_INSERTS_SPACES_KEY, true);
    TEST_ASSERT_EQ(2, tab_list_indent_width(), "on: lists use the tab width");
    settings_set_int(TAB_WIDTH_KEY, 99);
    TEST_ASSERT_EQ(16, tab_list_indent_width(), "and it is clamped");
    settings_clear_all();
}

// --- Displayed tab stops (the Win32 RichEdit default tab) -------------------

TEST_CASE(edit_average_width_is_gdis) {
    // Consolas 11 pt at 96 DPI draws 8 px per letter: 52 x 8 = 416
    TEST_ASSERT_EQ(8, tab_edit_average_width(416), "monospace 8 px");
    TEST_ASSERT_EQ(9, tab_edit_average_width(52 * 9), "monospace 9 px");
    // (cx / 26 + 1) / 2 rounds a proportional face's average to nearest
    TEST_ASSERT_EQ(7, tab_edit_average_width(350), "350 / 52 = 6.7 -> 7");
    TEST_ASSERT_EQ(0, tab_edit_average_width(0), "nothing measured");
    TEST_ASSERT_EQ(0, tab_edit_average_width(-5), "negative extent");
}

TEST_CASE(font_pixel_height_follows_dpi_and_zoom) {
    TEST_ASSERT_EQ(15, tab_font_pixel_height(220, 96, 100, 100), "11 pt at 96 DPI");
    TEST_ASSERT_EQ(18, tab_font_pixel_height(220, 120, 100, 100), "11 pt at 120 DPI");
    TEST_ASSERT_EQ(16, tab_font_pixel_height(220, 96, 110, 100), "11 pt at 110%");
    TEST_ASSERT_EQ(15, tab_font_pixel_height(220, 96, 0, 0), "0/0 zoom is 100%");
    TEST_ASSERT_EQ(15, tab_font_pixel_height(220, 0, 100, 100), "unknown DPI is 96");
    TEST_ASSERT_EQ(0, tab_font_pixel_height(0, 96, 100, 100), "no height");
}

TEST_CASE(stop_twips_map_back_to_the_pixel_stop) {
    // The review case: 8 x 8 px at 96 DPI is exactly 960 twips, not the 968
    // that measuring at 1440 px/inch gave (a stop half a pixel late)
    TEST_ASSERT_EQ(960, tab_stop_twips_for_pixels(64, 96, 100, 100), "64 px at 96 DPI");
    TEST_ASSERT_EQ(960, tab_stop_twips_for_pixels(80, 120, 100, 100), "80 px at 120 DPI");
    TEST_ASSERT_EQ(960, tab_stop_twips_for_pixels(64, 96, 0, 0), "0/0 zoom is 100%");
    // 110%: 9 px letters, so 72 px; 981.8 twips rounds down to 981
    TEST_ASSERT_EQ(981, tab_stop_twips_for_pixels(72, 96, 110, 100), "72 px at 110%");
    TEST_ASSERT_EQ(0, tab_stop_twips_for_pixels(0, 96, 100, 100), "no stop");

    // Everywhere: the stop drawn is at most one twip's worth of pixels early
    // and never late, so text exactly 8 columns long passes it
    static const long dpis[] = { 96, 120, 144, 168, 192, 288 };
    static const long zooms[][2] = { { 100, 100 }, { 110, 100 }, { 90, 100 }, { 3, 2 },
                                     { 1, 3 },     { 500, 100 }, { 10, 100 } };
    int bad = 0;
    for (size_t d = 0; d < sizeof(dpis) / sizeof(dpis[0]); d++) {
        for (size_t z = 0; z < sizeof(zooms) / sizeof(zooms[0]); z++) {
            for (long px = 8; px <= 400; px++) {
                long t = tab_stop_twips_for_pixels(px, dpis[d], zooms[z][0], zooms[z][1]);
                long long num = (long long) dpis[d] * zooms[z][0];
                long long den = 1440LL * zooms[z][1];
                // t * scale <= px < (t + 1) * scale
                if (!((long long) t * num <= (long long) px * den &&
                      (long long) (t + 1) * num > (long long) px * den))
                    bad++;
            }
        }
    }
    TEST_ASSERT_EQ(0, bad, "the stop is never past the pixel position, and within a twip");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(stop_is_next_multiple_not_fixed_count);
    RUN_TEST(existing_tab_advances_to_the_next_stop);
    RUN_TEST(spaces_after_existing_tab);
    RUN_TEST(code_points_not_bytes_or_utf16_units);
    RUN_TEST(invalid_utf8_cannot_inflate_the_column);
    RUN_TEST(line_break_resets_the_column);
    RUN_TEST(null_and_empty_prefix);
    RUN_TEST(width_is_clamped_in_arithmetic);
    RUN_TEST(settings_default_off_width_four);
    RUN_TEST(settings_width_is_clamped);
    RUN_TEST(list_indent_width_follows_tab_width_only_when_on);
    RUN_TEST(edit_average_width_is_gdis);
    RUN_TEST(font_pixel_height_follows_dpi_and_zoom);
    RUN_TEST(stop_twips_map_back_to_the_pixel_stop);

    TEST_SUMMARY();
    return 0;
}
