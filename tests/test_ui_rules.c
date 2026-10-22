/*
 * npad - Window and dialog rule tests
 * Interval clamps and timer arithmetic, the font size round trip, the Go To
 * Line check and keeping a restored window's title bar reachable
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/ui_rules.h"
#include "../src/core/settings.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(intervals_clamp_to_the_gtk_ranges) {
    TEST_ASSERT_EQ(10, ui_clamp_auto_save_interval(0), "auto-save floor");
    TEST_ASSERT_EQ(10, ui_clamp_auto_save_interval(-5), "negative auto-save");
    TEST_ASSERT_EQ(300, ui_clamp_auto_save_interval(300), "in range untouched");
    TEST_ASSERT_EQ(86400, ui_clamp_auto_save_interval(4294968LL), "the wrapping value");
    TEST_ASSERT_EQ(86400, ui_clamp_auto_save_interval(LLONG_MAX), "huge");
    TEST_ASSERT_EQ(5, ui_clamp_session_interval(1), "session floor");
    TEST_ASSERT_EQ(86400, ui_clamp_session_interval(1LL << 40), "session ceiling");
    TEST_ASSERT_EQ(0, ui_clamp_large_file_mb(0), "0 keeps the warning off");
    TEST_ASSERT_EQ(0, ui_clamp_large_file_mb(-1), "negative never wraps into a value");
    TEST_ASSERT_EQ(1048576, ui_clamp_large_file_mb(3000000000LL), "above INT_MAX clamps");
}

TEST_CASE(timer_ms_never_wraps) {
    TEST_ASSERT_EQ(0, (long) ui_interval_to_timer_ms(0), "no timer");
    TEST_ASSERT_EQ(0, (long) ui_interval_to_timer_ms(-1), "no timer for negative");
    TEST_ASSERT_EQ(10000, (long) ui_interval_to_timer_ms(10), "10 s");
    TEST_ASSERT_EQ(86400000L, (long) ui_interval_to_timer_ms(86400), "a day");
    // The value from the review: (UINT) 4294968 * 1000 wrapped to 704 ms
    TEST_ASSERT(ui_interval_to_timer_ms(4294968) == UI_TIMER_MAX_MS, "saturates, never wraps");
    TEST_ASSERT(ui_interval_to_timer_ms(INT_MAX) == UI_TIMER_MAX_MS, "INT_MAX saturates");
}

TEST_CASE(interval_settings_read_without_overflow) {
    settings_clear_all();
    TEST_ASSERT_EQ(AUTO_SAVE_INTERVAL_DEFAULT, ui_auto_save_interval_setting(), "default");
    TEST_ASSERT_EQ(SESSION_INTERVAL_DEFAULT, ui_session_interval_setting(), "default");
    TEST_ASSERT_EQ(77, ui_large_file_mb_setting(77), "machine default");

    settings_set_string(AUTO_SAVE_INTERVAL_KEY, "99999999999");
    TEST_ASSERT_EQ(86400, ui_auto_save_interval_setting(), "beyond int clamps to max");
    settings_set_string(AUTO_SAVE_INTERVAL_KEY, "3");
    TEST_ASSERT_EQ(10, ui_auto_save_interval_setting(), "below floor");
    settings_set_string(AUTO_SAVE_INTERVAL_KEY, "junk");
    TEST_ASSERT_EQ(AUTO_SAVE_INTERVAL_DEFAULT, ui_auto_save_interval_setting(), "non-numeric");

    settings_set_string(SESSION_INTERVAL_KEY, "45");
    TEST_ASSERT_EQ(45, ui_session_interval_setting(), "in range");

    // atoi of this wraps negative, which used to switch the warning off
    settings_set_string(LARGE_FILE_MB_KEY, "4294967295");
    TEST_ASSERT_EQ(1048576, ui_large_file_mb_setting(77), "huge threshold stays on");
    settings_set_string(LARGE_FILE_MB_KEY, "0");
    TEST_ASSERT_EQ(0, ui_large_file_mb_setting(77), "0 is the off switch");
    settings_set_string(LARGE_FILE_MB_KEY, "200");
    TEST_ASSERT_EQ(200, ui_large_file_mb_setting(77), "stored value");
    settings_clear_all();
}

TEST_CASE(font_size_parse) {
    TEST_ASSERT_EQ(110, ui_font_size_parse_tenths("11", 100), "whole points");
    TEST_ASSERT_EQ(105, ui_font_size_parse_tenths("10.5", 100), "half point");
    TEST_ASSERT_EQ(93, ui_font_size_parse_tenths(" 9.25 ", 100), "rounds to nearest tenth");
    TEST_ASSERT_EQ(92, ui_font_size_parse_tenths("9.24", 100), "rounds down");
    TEST_ASSERT_EQ(110, ui_font_size_parse_tenths("10.96", 100), "rounds across the point");
    TEST_ASSERT_EQ(100, ui_font_size_parse_tenths("10,5", 90), "a comma is not a decimal point");
    TEST_ASSERT_EQ(90, ui_font_size_parse_tenths("", 90), "empty gives the default");
    TEST_ASSERT_EQ(90, ui_font_size_parse_tenths(NULL, 90), "missing gives the default");
    TEST_ASSERT_EQ(90, ui_font_size_parse_tenths("abc", 90), "junk gives the default");
    TEST_ASSERT_EQ(60, ui_font_size_parse_tenths("2", 90), "clamped to 6 pt");
    TEST_ASSERT_EQ(60, ui_font_size_parse_tenths("-12", 90), "negative clamps");
    TEST_ASSERT_EQ(720, ui_font_size_parse_tenths("96", 90), "clamped to 72 pt");
    TEST_ASSERT_EQ(720, ui_font_size_parse_tenths("99999999999999", 90), "no overflow");
    TEST_ASSERT_EQ(60, ui_font_size_parse_tenths(".5", 90), "a bare fraction parses, then clamps");
    TEST_ASSERT_EQ(720, ui_font_size_parse_tenths("72.5", 90), "72.5 clamps to 72");
}

TEST_CASE(font_size_format_and_round_trip) {
    char buf[16];
    ui_font_size_format_tenths(110, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ("11", buf, "whole points stay an integer for older readers");
    ui_font_size_format_tenths(105, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ("10.5", buf, "half point");
    ui_font_size_format_tenths(1000, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ("72", buf, "clamped on the way out");

    settings_clear_all();
    TEST_ASSERT_EQ(FONT_SIZE_DEFAULT_TENTHS, ui_font_size_setting_tenths(), "default 11 pt");
    for (int t = 60; t <= 720; t++) {
        ui_font_size_set_tenths(t);
        if (ui_font_size_setting_tenths() != t) {
            TEST_ASSERT_EQ(t, ui_font_size_setting_tenths(), "every tenth round-trips");
        }
    }
    ui_font_size_set_tenths(105);
    char *stored = settings_get_string(FONT_SIZE_KEY, NULL);
    TEST_ASSERT_STR_EQ("10.5", stored, "stored as decimal points");
    free(stored);
    // A whole size is still readable by the old integer reader
    ui_font_size_set_tenths(120);
    TEST_ASSERT_EQ(12, settings_get_int(FONT_SIZE_KEY, 0), "atoi-compatible");
    ui_font_size_set_tenths(800);
    TEST_ASSERT_EQ(720, ui_font_size_setting_tenths(), "store clamps above 72");
    settings_clear_all();

    TEST_ASSERT_EQ(11, ui_font_size_tenths_to_points(105), "half rounds up for printing");
    TEST_ASSERT_EQ(10, ui_font_size_tenths_to_points(104), "below half rounds down");
}

TEST_CASE(goto_line_parse) {
    int line = -1;
    TEST_ASSERT_EQ(GOTO_LINE_OK, ui_goto_line_parse("1", 10, &line), "first line");
    TEST_ASSERT_EQ(1, line, "line 1");
    TEST_ASSERT_EQ(GOTO_LINE_OK, ui_goto_line_parse(" 10 ", 10, &line), "last line, spaces");
    TEST_ASSERT_EQ(10, line, "line 10");
    TEST_ASSERT_EQ(GOTO_LINE_OK, ui_goto_line_parse("007", 10, &line), "leading zeros");
    TEST_ASSERT_EQ(7, line, "line 7");

    line = -1;
    TEST_ASSERT_EQ(GOTO_LINE_OUT_OF_RANGE, ui_goto_line_parse("11", 10, &line), "past the end");
    TEST_ASSERT_EQ(-1, line, "untouched when not OK");
    TEST_ASSERT_EQ(GOTO_LINE_OUT_OF_RANGE, ui_goto_line_parse("0", 10, &line), "zero");
    // Above INT_MAX used to wrap negative and jump to the top
    TEST_ASSERT_EQ(GOTO_LINE_OUT_OF_RANGE, ui_goto_line_parse("2147483648", LLONG_MAX, &line),
                   "INT_MAX + 1");
    TEST_ASSERT_EQ(GOTO_LINE_OUT_OF_RANGE, ui_goto_line_parse("4294967297", LLONG_MAX, &line),
                   "2^32 + 1");
    TEST_ASSERT_EQ(GOTO_LINE_OUT_OF_RANGE,
                   ui_goto_line_parse("99999999999999999999999999", LLONG_MAX, &line),
                   "no overflow on many digits");
    TEST_ASSERT_EQ(GOTO_LINE_OK, ui_goto_line_parse("2147483647", LLONG_MAX, &line), "INT_MAX");
    TEST_ASSERT_EQ(INT_MAX, line, "INT_MAX itself is fine");

    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse("", 10, &line), "empty");
    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse("   ", 10, &line), "blank");
    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse("-3", 10, &line), "sign");
    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse("3x", 10, &line), "trailing junk");
    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse("1 2", 10, &line), "two numbers");
    TEST_ASSERT_EQ(GOTO_LINE_INVALID, ui_goto_line_parse(NULL, 10, &line), "NULL");
    TEST_ASSERT_EQ(GOTO_LINE_OK, ui_goto_line_parse("1", 0, &line), "empty document has line 1");
}

static const UiRect k_work = { 0, 40, 1920, 1080 }; // A 40 px taskbar at the top

TEST_CASE(fit_title_bar_leaves_reachable_windows_alone) {
    int x = 100, y = 100;
    TEST_ASSERT(!ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work), "inside: not moved");
    TEST_ASSERT_EQ(100, x, "x");
    TEST_ASSERT_EQ(100, y, "y");
    x = -500; // Mostly off the left edge, but 300 px still grabbable
    y = 40;
    TEST_ASSERT(!ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work), "partly off-screen survives");
    TEST_ASSERT_EQ(-500, x, "x kept");
}

TEST_CASE(fit_title_bar_pulls_the_caption_back) {
    // The creep the review describes: the window ended up under the taskbar
    int x = 100, y = 0;
    TEST_ASSERT(ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work), "under a top taskbar");
    TEST_ASSERT_EQ(40, y, "top of the work area");
    TEST_ASSERT_EQ(100, x, "x untouched");

    y = -2000;
    ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work);
    TEST_ASSERT_EQ(40, y, "far above");

    y = 1075;
    ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work);
    TEST_ASSERT_EQ(1050, y, "below the bottom: caption kept on screen");

    x = -790;
    y = 100;
    ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work);
    TEST_ASSERT_EQ(-680, x, "off the left: grip pixels brought back");

    x = 1900;
    ui_fit_title_bar(&x, &y, 800, 30, 120, &k_work);
    TEST_ASSERT_EQ(1800, x, "off the right: grip pixels brought back");
}

TEST_CASE(fit_title_bar_caption_on_one_monitor_body_on_another) {
    // Monitor A at y=0 on the left, monitor B at y=200 on the right. The
    // window's caption lies wholly on A while most of its width is over B.
    // The caller fits against the caption's monitor (A), where it is left
    // alone; fitting against B (the largest-overlap monitor) would push it
    // down, which is the regression the caption-strip choice avoids.
    const UiRect work_a = { 0, 0, 1920, 1040 };
    const UiRect work_b = { 1920, 200, 3840, 1240 };
    int x = 1500, y = 50;
    TEST_ASSERT(!ui_fit_title_bar(&x, &y, 1000, 32, 120, &work_a), "caption's monitor: not moved");
    TEST_ASSERT_EQ(1500, x, "x kept");
    TEST_ASSERT_EQ(50, y, "y kept");
    TEST_ASSERT(ui_fit_title_bar(&x, &y, 1000, 32, 120, &work_b), "body's monitor would move it");
    TEST_ASSERT_EQ(200, y, "pushed down to B's top");

    // Stacked monitors: caption on the upper one, body mostly on the lower
    const UiRect upper = { 0, 0, 1920, 1080 };
    x = 100;
    y = 1000;
    TEST_ASSERT(!ui_fit_title_bar(&x, &y, 800, 32, 120, &upper), "stacked: caption on upper kept");
    TEST_ASSERT_EQ(1000, y, "y kept");
}

TEST_CASE(fit_title_bar_degenerate_inputs) {
    int x = 5, y = 5;
    const UiRect empty = { 0, 0, 0, 0 };
    TEST_ASSERT(!ui_fit_title_bar(&x, &y, 800, 30, 120, &empty), "no work area: no change");
    TEST_ASSERT(!ui_fit_title_bar(NULL, &y, 800, 30, 120, &k_work), "NULL x");
    const UiRect tiny = { 0, 0, 50, 20 }; // Smaller than the caption and grip
    x = 1000;
    y = 1000;
    ui_fit_title_bar(&x, &y, 800, 30, 120, &tiny);
    TEST_ASSERT_EQ(0, y, "caption taller than the work area: pinned to its top");
    TEST_ASSERT_EQ(0, x, "grip wider than the work area: pinned to its left");
    x = INT_MAX;
    y = INT_MIN;
    ui_fit_title_bar(&x, &y, INT_MAX, 30, 120, &k_work);
    TEST_ASSERT_EQ(40, y, "no overflow on extreme values");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(intervals_clamp_to_the_gtk_ranges);
    RUN_TEST(timer_ms_never_wraps);
    RUN_TEST(interval_settings_read_without_overflow);
    RUN_TEST(font_size_parse);
    RUN_TEST(font_size_format_and_round_trip);
    RUN_TEST(goto_line_parse);
    RUN_TEST(fit_title_bar_leaves_reachable_windows_alone);
    RUN_TEST(fit_title_bar_pulls_the_caption_back);
    RUN_TEST(fit_title_bar_caption_on_one_monitor_body_on_another);
    RUN_TEST(fit_title_bar_degenerate_inputs);

    TEST_SUMMARY();
    return 0;
}
