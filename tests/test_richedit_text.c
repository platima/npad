/*
 * npad - RichEdit line break normalisation tests (portable part)
 *
 * richedit_normalize_breaks turns every line break into the single CR the
 * Win32 RichEdit control stores, before the text goes in. It must count breaks
 * exactly as the core does (file_convert_line_endings), or a save after a
 * round trip through the control would write different lines than the core
 * alone - CR CR LF is the case that once lost a line per occurrence. The
 * control itself is exercised on Windows by tests/test_richedit_boundary.c.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/platform/richedit_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ASCII text -> wide, normalised, then read back the way richedit_get_text
// does (each stored CR widened to CR LF) -> ASCII
static char *through_normalise(const char *text) {
    size_t len = strlen(text);
    wchar_t *wide = malloc((len + 1) * sizeof(wchar_t));
    if (!wide)
        return NULL;
    for (size_t i = 0; i < len; i++)
        wide[i] = (wchar_t) (unsigned char) text[i];
    wide[len] = L'\0';
    size_t n = richedit_normalize_breaks(wide, len);

    char *out = malloc(2 * n + 1);
    if (out) {
        char *w = out;
        for (size_t i = 0; i < n; i++) {
            if (wide[i] == L'\r') {
                *w++ = '\r';
                *w++ = '\n';
            } else {
                *w++ = (char) wide[i];
            }
        }
        *w = '\0';
    }
    free(wide);
    return out;
}

TEST_CASE(cr_cr_lf_is_two_breaks) {
    char *got = through_normalise("a\r\r\nb");
    TEST_ASSERT_STR_EQ("a\r\n\r\nb", got, "CR CR LF keeps both of its breaks");
    free(got);
}

TEST_CASE(each_break_style_is_one_break) {
    char *got = through_normalise("a\r\nb\rc\nd\n\re");
    TEST_ASSERT_STR_EQ("a\r\nb\r\nc\r\nd\r\n\r\ne", got, "CRLF, CR, LF, LF CR");
    free(got);
    got = through_normalise("");
    TEST_ASSERT_STR_EQ("", got, "empty");
    free(got);
    got = through_normalise("\r");
    TEST_ASSERT_STR_EQ("\r\n", got, "trailing lone CR");
    free(got);
}

TEST_CASE(normalise_is_in_place_and_terminated) {
    wchar_t text[8] = { L'x', L'\r', L'\n', L'y', L'\n', L'z', L'\0', L'!' };
    size_t n = richedit_normalize_breaks(text, 6);
    TEST_ASSERT_EQ(5, (long) n, "the CRLF pair became one unit");
    TEST_ASSERT(text[1] == L'\r' && text[3] == L'\r' && text[4] == L'z', "breaks are CR");
    TEST_ASSERT(text[5] == L'\0', "terminated at the new length");
}

TEST_CASE(matches_core_conversion_on_random_text) {
    // Seeded, so a failure reproduces
    unsigned long seed = 0x5eed1234UL;
    const char alphabet[] = { 'a', 'b', '\r', '\n', '\r', '\n' };
    char text[64];
    for (int iter = 0; iter < 5000; iter++) {
        seed = seed * 1103515245UL + 12345UL;
        size_t len = (size_t) ((seed >> 16) % (sizeof(text) - 1));
        for (size_t i = 0; i < len; i++) {
            seed = seed * 1103515245UL + 12345UL;
            text[i] = alphabet[(seed >> 16) % sizeof(alphabet)];
        }
        text[len] = '\0';
        char *want = file_convert_line_endings(text, NPAD_EOL_CRLF);
        char *got = through_normalise(text);
        bool same = want && got && strcmp(want, got) == 0;
        free(want);
        free(got);
        if (!same)
            printf("\n  iteration %d differs\n", iter);
        TEST_ASSERT(same, "control round trip equals the core's own CRLF conversion");
    }
}

int main(void) {
    TEST_INIT();

    RUN_TEST(cr_cr_lf_is_two_breaks);
    RUN_TEST(each_break_style_is_one_break);
    RUN_TEST(normalise_is_in_place_and_terminated);
    RUN_TEST(matches_core_conversion_on_random_text);

    TEST_SUMMARY();
    return 0;
}
