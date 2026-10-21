/*
 * npad - Short time and date format tests
 * The F5 stamp and the print header's &t / &d on Linux: the locale's own
 * formats (glibc's T_FMT / D_FMT, quoted below) rewritten to Windows' short
 * forms - no seconds, four-digit years.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/time_format.h"
#include <string.h>
#include <time.h>

static const char *short_time(const char *fmt) {
    static char out[64];
    if (!time_format_short_time(fmt, out, sizeof(out)))
        return "(overflow)";
    return out;
}

static const char *short_date(const char *fmt) {
    static char out[64];
    if (!time_format_short_date(fmt, out, sizeof(out)))
        return "(overflow)";
    return out;
}

TEST_CASE(time_drops_seconds_in_common_locales) {
    TEST_ASSERT_STR_EQ("%H:%M", short_time("%H:%M:%S"), "C / POSIX");
    TEST_ASSERT_STR_EQ("%H:%M", short_time("%T"), "en_GB, de_DE, en_AU: %T");
    TEST_ASSERT_STR_EQ("%I:%M %p", short_time("%r"), "en_US: %r");
    TEST_ASSERT_STR_EQ("%I:%M %p", short_time("%I:%M:%S %p"), "12-hour with seconds");
    TEST_ASSERT_STR_EQ("%l:%M %p", short_time("%l:%M:%S %p"), "space-padded hour kept");
    TEST_ASSERT_STR_EQ("%H.%M", short_time("%H.%M.%S"), "dot separators (fi_FI)");
    TEST_ASSERT_STR_EQ("%H:%M %Z", short_time("%H:%M:%S %Z"), "a zone after the seconds stays");
    TEST_ASSERT_STR_EQ("%H:%M", short_time("%H:%M"), "already short: unchanged");
    TEST_ASSERT_STR_EQ("%R", short_time("%R"), "%R has no seconds");
}

TEST_CASE(time_drops_a_seconds_unit) {
    // ja_JP: "%H<hour>%M<minute>%S<second>", the units as UTF-8
    TEST_ASSERT_STR_EQ("%H\xE6\x99\x82%M\xE5\x88\x86",
                       short_time("%H\xE6\x99\x82%M\xE5\x88\x86%S\xE7\xA7\x92"),
                       "the seconds and their unit go, the minutes' unit stays");
    // ko_KR: "%p %I<hour> %M<minute> %S<second>"
    TEST_ASSERT_STR_EQ("%p %I\xEC\x8B\x9C %M\xEB\xB6\x84",
                       short_time("%p %I\xEC\x8B\x9C %M\xEB\xB6\x84 %S\xEC\xB4\x88"),
                       "no trailing space is left behind");
}

TEST_CASE(time_keeps_modifiers_and_literals) {
    TEST_ASSERT_STR_EQ("%OH:%OM", short_time("%OH:%OM:%OS"), "alternative digits");
    TEST_ASSERT_STR_EQ("%-I:%M %p", short_time("%-I:%M:%S %p"), "GNU flags kept");
    TEST_ASSERT_STR_EQ("%H:%M 100%%", short_time("%H:%M:%S 100%%"), "a literal percent");
}

TEST_CASE(time_falls_back_when_nothing_is_left) {
    TEST_ASSERT_STR_EQ("%H:%M", short_time(NULL), "NULL");
    TEST_ASSERT_STR_EQ("%H:%M", short_time(""), "empty T_FMT");
    TEST_ASSERT_STR_EQ("%H:%M", short_time("%S"), "only seconds");
}

TEST_CASE(date_gets_four_digit_years) {
    TEST_ASSERT_STR_EQ("%m/%d/%Y", short_date("%m/%d/%y"), "C / POSIX");
    TEST_ASSERT_STR_EQ("%d/%m/%Y", short_date("%d/%m/%y"), "en_AU");
    TEST_ASSERT_STR_EQ("%m/%d/%Y", short_date("%m/%d/%Y"), "en_US: already four digits");
    TEST_ASSERT_STR_EQ("%d.%m.%Y", short_date("%d.%m.%Y"), "de_DE unchanged");
    TEST_ASSERT_STR_EQ("%m/%d/%Y", short_date("%D"), "%D expanded");
    TEST_ASSERT_STR_EQ("%-d/%-m/%-Y", short_date("%-d/%-m/%-y"), "flags kept on the year");
    TEST_ASSERT_STR_EQ("%Ey", short_date("%Ey"), "an era year is not a two-digit year");
    TEST_ASSERT_STR_EQ("%Y\xE5\xB9\xB4%m\xE6\x9C\x88%d\xE6\x97\xA5",
                       short_date("%Y\xE5\xB9\xB4%m\xE6\x9C\x88%d\xE6\x97\xA5"), "ja_JP unchanged");
    TEST_ASSERT_STR_EQ("%m/%d/%Y", short_date(""), "empty D_FMT");
    TEST_ASSERT_STR_EQ("%m/%d/%Y", short_date(NULL), "NULL");
}

TEST_CASE(overflow_is_reported) {
    char tiny[4];
    TEST_ASSERT(!time_format_short_time("%I:%M:%S %p", tiny, sizeof(tiny)), "too small");
    TEST_ASSERT_STR_EQ("", tiny, "left empty, never truncated mid-conversion");
    TEST_ASSERT(!time_format_short_date("%d/%m/%y", tiny, sizeof(tiny)), "too small");
    TEST_ASSERT(!time_format_short_time("%H", NULL, 8), "no buffer");
    char exact[6];
    TEST_ASSERT(time_format_short_time("%T", exact, sizeof(exact)), "%H:%M fits in 6");
    TEST_ASSERT_STR_EQ("%H:%M", exact, "written");
}

// The result really is a strftime format: no seconds field in the output
TEST_CASE(result_formats_without_seconds) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 126; // 2026
    tm.tm_mon = 9;
    tm.tm_mday = 21;
    tm.tm_hour = 9;
    tm.tm_min = 15;
    tm.tm_sec = 42;
    char fmt[64], text[64];
    TEST_ASSERT(time_format_short_time("%H:%M:%S", fmt, sizeof(fmt)), "time format");
    strftime(text, sizeof(text), fmt, &tm);
    TEST_ASSERT_STR_EQ("09:15", text, "09:15, not 09:15:42");
    TEST_ASSERT(time_format_short_date("%m/%d/%y", fmt, sizeof(fmt)), "date format");
    strftime(text, sizeof(text), fmt, &tm);
    TEST_ASSERT_STR_EQ("10/21/2026", text, "four-digit year");
}

int main(void) {
    TEST_INIT();
    RUN_TEST(time_drops_seconds_in_common_locales);
    RUN_TEST(time_drops_a_seconds_unit);
    RUN_TEST(time_keeps_modifiers_and_literals);
    RUN_TEST(time_falls_back_when_nothing_is_left);
    RUN_TEST(date_gets_four_digit_years);
    RUN_TEST(overflow_is_reported);
    RUN_TEST(result_formats_without_seconds);
    TEST_SUMMARY();
}
