/*
 * npad - Find seeding tests
 * Which selections may pre-fill Find, and the preference that gates it; the
 * find_ preferences Reset All clears; Replace All's one-pass result
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/find_seed.h"
#include "../src/core/settings.h"
#include <stdlib.h>
#include <string.h>

// Assert the selection seeds as `expected`, freeing the result
#define ASSERT_SEED(sel, escape, expected, msg)                                                    \
    do {                                                                                           \
        char *_r = find_seed_text((sel), (escape));                                                \
        TEST_ASSERT_NOT_NULL(_r, msg " (returned NULL)");                                          \
        if (_r) {                                                                                  \
            TEST_ASSERT_STR_EQ((expected), _r, msg);                                               \
            free(_r);                                                                              \
        }                                                                                          \
    } while (0)

// Assert the selection must NOT seed (the caller keeps the last term)
#define ASSERT_NO_SEED(sel, escape, msg) TEST_ASSERT_NULL(find_seed_text((sel), (escape)), msg)

TEST_CASE(seeds_plain_word) {
    ASSERT_SEED("needle", false, "needle", "single word");
}

TEST_CASE(seeds_text_with_spaces_and_tabs) {
    ASSERT_SEED("two words\there", false, "two words\there", "spaces and tab are not line breaks");
}

TEST_CASE(seeds_non_ascii) {
    ASSERT_SEED("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", false,
                "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", "multi-byte characters survive");
}

TEST_CASE(empty_or_null_does_not_seed) {
    ASSERT_NO_SEED(NULL, false, "NULL selection");
    ASSERT_NO_SEED("", false, "empty selection");
}

TEST_CASE(any_line_break_does_not_seed) {
    ASSERT_NO_SEED("a\nb", false, "LF");
    ASSERT_NO_SEED("a\rb", false, "CR (RichEdit's internal break)");
    ASSERT_NO_SEED("a\r\nb", false, "CRLF");
    ASSERT_NO_SEED("a\vb", false, "VT (RichEdit soft break)");
    ASSERT_NO_SEED("a\fb", false, "FF");
    ASSERT_NO_SEED("a\xC2\x85" "b", false, "NEL");
    ASSERT_NO_SEED("a\xE2\x80\xA8" "b", false, "LINE SEPARATOR");
    ASSERT_NO_SEED("a\xE2\x80\xA9" "b", false, "PARAGRAPH SEPARATOR");
    ASSERT_NO_SEED("trailing\n", false, "a trailing break is still a break");
    ASSERT_NO_SEED("\n", false, "a lone break");
}

TEST_CASE(invalid_utf8_does_not_seed) {
    ASSERT_NO_SEED("a\xFF" "b", false, "invalid lead byte");
    ASSERT_NO_SEED("a\xC3", false, "truncated sequence");
    ASSERT_NO_SEED("\xC0\x80", false, "overlong encoding");
    ASSERT_NO_SEED("\xED\xA0\x80", false, "encoded surrogate");
}

TEST_CASE(length_cap_counts_utf16_units) {
    char buf[FIND_SEED_MAX_UNITS + 8];
    memset(buf, 'x', sizeof(buf));

    buf[FIND_SEED_MAX_UNITS] = '\0';
    char *ok = find_seed_text(buf, false);
    TEST_ASSERT_NOT_NULL(ok, "exactly the cap seeds");
    free(ok);

    buf[FIND_SEED_MAX_UNITS + 1] = '\0';
    buf[FIND_SEED_MAX_UNITS] = 'x';
    ASSERT_NO_SEED(buf, false, "one over the cap does not seed");

    // Astral characters are two UTF-16 units each: 128 of them is 256 units
    char astral[128 * 4 + 1];
    for (int i = 0; i < 128; i++)
        memcpy(astral + i * 4, "\xF0\x9F\x98\x80", 4);
    astral[sizeof(astral) - 1] = '\0';
    ASSERT_NO_SEED(astral, false, "128 astral characters is 256 units");
    astral[127 * 4] = '\0';
    char *fits = find_seed_text(astral, false);
    TEST_ASSERT_NOT_NULL(fits, "127 astral characters is 254 units");
    free(fits);
}

TEST_CASE(escape_mode_doubles_backslashes) {
    ASSERT_SEED("C:\\new\\table", true, "C:\\\\new\\\\table", "backslashes doubled");
    ASSERT_SEED("C:\\new\\table", false, "C:\\new\\table", "left alone when not escaping");
    ASSERT_SEED("no backslash", true, "no backslash", "nothing to double");
}

TEST_CASE(escape_mode_cap_applies_to_escaped_length) {
    char buf[FIND_SEED_MAX_UNITS + 1];
    memset(buf, '\\', sizeof(buf));
    buf[FIND_SEED_MAX_UNITS / 2 + 1] = '\0'; // 128 backslashes -> 256 once doubled
    ASSERT_NO_SEED(buf, true, "doubled length is over the cap");
    char *plain = find_seed_text(buf, false);
    TEST_ASSERT_NOT_NULL(plain, "same selection seeds when not escaping");
    free(plain);
}

TEST_CASE(preference_defaults_on_and_follows_setting) {
    settings_clear_all();
    TEST_ASSERT(find_seed_enabled(), "defaults ON (notepad.exe does it)");
    settings_set_bool(FIND_SEED_KEY, false);
    TEST_ASSERT(!find_seed_enabled(), "off when the preference is cleared");
    settings_set_bool(FIND_SEED_KEY, true);
    TEST_ASSERT(find_seed_enabled(), "on when set");
    settings_remove_key(FIND_SEED_KEY);
    TEST_ASSERT(find_seed_enabled(), "back to the default once removed (Reset All)");
    settings_clear_all();
}

// Reset All Preferences keeps the find_ prefix (search state) and then
// removes the find_ keys a Preferences page shows - both platforms' resets
// run exactly this sequence
TEST_CASE(reset_clears_find_preferences_but_keeps_search_state) {
    static const char *const keep[] = { "recent_file_", "window_", "find_", "replace_" };
    settings_clear_all();
    settings_set_bool(FIND_SEED_KEY, false);
    settings_set_bool(FIND_WRAP_KEY, true);
    settings_set_bool("find_match_case", true);
    settings_set_bool("find_search_down", false);
    settings_set_string("find_hist_0", "needle");
    settings_set_string("replace_hist_0", "thread");
    settings_set_bool("word_wrap", true);

    settings_reset_except_prefixes(keep, (int) (sizeof(keep) / sizeof(keep[0])));
    find_reset_preference_keys();

    TEST_ASSERT(find_seed_enabled(), "Fill Find from the selection is back to its default (on)");
    TEST_ASSERT(!settings_has_key(FIND_WRAP_KEY), "wrap-around preference removed");
    TEST_ASSERT(!settings_get_bool(FIND_WRAP_KEY, false), "wrap-around back to off (notepad.exe)");
    TEST_ASSERT(settings_get_bool("find_match_case", false), "dialog-only option kept");
    TEST_ASSERT(!settings_get_bool("find_search_down", true), "direction kept");
    TEST_ASSERT(!settings_has_key("word_wrap"), "an ordinary preference is reset");
    char *hist = settings_get_string("find_hist_0", NULL);
    TEST_ASSERT_STR_EQ("needle", hist ? hist : "(missing)", "find history kept");
    free(hist);
    hist = settings_get_string("replace_hist_0", NULL);
    TEST_ASSERT_STR_EQ("thread", hist ? hist : "(missing)", "replace history kept");
    free(hist);
    settings_clear_all();
}

// Compare a wide result against the expected text, freeing the result
static bool wide_result_is(wchar_t *got, size_t got_len, const wchar_t *expected) {
    bool same = got && got_len == wcslen(expected) && wcscmp(got, expected) == 0;
    free(got);
    return same;
}

TEST_CASE(replace_ranges_builds_the_whole_result) {
    const wchar_t *text = L"cat sat on the cat mat";
    FindRange two[] = { { 0, 3 }, { 15, 18 } };
    size_t n = 99;
    wchar_t *r = find_replace_ranges(text, wcslen(text), two, 2, L"dog", &n);
    TEST_ASSERT(wide_result_is(r, n, L"dog sat on the dog mat"), "same-length replacement");

    r = find_replace_ranges(text, wcslen(text), two, 2, L"tiger", &n);
    TEST_ASSERT(wide_result_is(r, n, L"tiger sat on the tiger mat"), "longer replacement");

    r = find_replace_ranges(text, wcslen(text), two, 2, L"", &n);
    TEST_ASSERT(wide_result_is(r, n, L" sat on the  mat"), "empty replacement deletes");

    FindRange whole[] = { { 0, 22 } };
    r = find_replace_ranges(text, wcslen(text), whole, 1, L"x", &n);
    TEST_ASSERT(wide_result_is(r, n, L"x"), "a match covering the whole text");

    FindRange adjacent[] = { { 0, 1 }, { 1, 2 }, { 2, 3 } };
    r = find_replace_ranges(L"aaa", 3, adjacent, 3, L"bb", &n);
    TEST_ASSERT(wide_result_is(r, n, L"bbbbbb"), "back-to-back matches");

    FindRange at_end[] = { { 4, 7 } };
    r = find_replace_ranges(L"the end", 7, at_end, 1, L"start", &n);
    TEST_ASSERT(wide_result_is(r, n, L"the start"), "a match at the very end");

    r = find_replace_ranges(text, wcslen(text), NULL, 0, L"dog", &n);
    TEST_ASSERT(wide_result_is(r, n, text), "no matches copies the text");

    r = find_replace_ranges(L"", 0, NULL, 0, L"dog", &n);
    TEST_ASSERT(wide_result_is(r, n, L""), "empty text");
}

TEST_CASE(replace_ranges_keeps_line_breaks) {
    // RichEdit's internal break is a bare CR, one position like any character
    const wchar_t *text = L"a\rb\rc";
    FindRange breaks[] = { { 1, 2 }, { 3, 4 } };
    size_t n = 0;
    wchar_t *r = find_replace_ranges(text, 5, breaks, 2, L"\r\r", &n);
    TEST_ASSERT(wide_result_is(r, n, L"a\r\rb\r\rc"), "breaks in and out");
}

TEST_CASE(replace_ranges_rejects_bad_ranges) {
    const wchar_t *text = L"abcdef";
    FindRange overlap[] = { { 0, 3 }, { 2, 4 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, overlap, 2, L"x", NULL), "overlapping");
    FindRange backwards[] = { { 3, 4 }, { 0, 1 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, backwards, 2, L"x", NULL), "out of order");
    FindRange inverted[] = { { 3, 2 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, inverted, 1, L"x", NULL), "end before start");
    FindRange negative[] = { { -1, 2 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, negative, 1, L"x", NULL), "negative start");
    FindRange past[] = { { 4, 7 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, past, 1, L"x", NULL), "past the end");
    FindRange ok[] = { { 0, 1 } };
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, ok, 1, NULL, NULL), "no replacement");
    TEST_ASSERT_NULL(find_replace_ranges(NULL, 6, ok, 1, L"x", NULL), "no text");
    TEST_ASSERT_NULL(find_replace_ranges(text, 6, NULL, 1, L"x", NULL), "no ranges");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(seeds_plain_word);
    RUN_TEST(seeds_text_with_spaces_and_tabs);
    RUN_TEST(seeds_non_ascii);
    RUN_TEST(empty_or_null_does_not_seed);
    RUN_TEST(any_line_break_does_not_seed);
    RUN_TEST(invalid_utf8_does_not_seed);
    RUN_TEST(length_cap_counts_utf16_units);
    RUN_TEST(escape_mode_doubles_backslashes);
    RUN_TEST(escape_mode_cap_applies_to_escaped_length);
    RUN_TEST(preference_defaults_on_and_follows_setting);
    RUN_TEST(reset_clears_find_preferences_but_keeps_search_state);
    RUN_TEST(replace_ranges_builds_the_whole_result);
    RUN_TEST(replace_ranges_keeps_line_breaks);
    RUN_TEST(replace_ranges_rejects_bad_ranges);

    TEST_SUMMARY();
    return 0;
}
