/*
 * npad - Find seeding tests
 * Which selections may pre-fill Find, and the preference that gates it
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/find_seed.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
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

int main(void) {
    // The settings store is guarded by g_settings_mutex, which must be
    // initialised first: on Windows an uninitialised CRITICAL_SECTION crashes
    thread_safety_init();
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

    settings_cleanup();
    thread_safety_cleanup();
    TEST_SUMMARY();
    return 0;
}
