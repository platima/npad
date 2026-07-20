/*
 * npad - Update Check Tests
 * Unit tests for release-tag extraction, version comparison and sha256
 * digest parsing used by Help > Check for Updates
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/update_check.h"
#include <string.h>

TEST_CASE(extract_tag_basic) {
    char tag[32];
    TEST_ASSERT(update_extract_tag("{\"tag_name\":\"v0.15.0\",\"name\":\"npad v0.15.0\"}", tag,
                                   sizeof(tag)),
                "extraction should succeed");
    TEST_ASSERT_STR_EQ("v0.15.0", tag, "tag value extracted");
}

TEST_CASE(extract_tag_spaced) {
    char tag[32];
    TEST_ASSERT(update_extract_tag("{ \"tag_name\" : \"v1.2.3\" }", tag, sizeof(tag)),
                "extraction tolerates whitespace around the colon");
    TEST_ASSERT_STR_EQ("v1.2.3", tag, "spaced tag value extracted");
}

TEST_CASE(extract_tag_missing_or_bad) {
    char tag[32];
    TEST_ASSERT(!update_extract_tag("{\"name\":\"no tag here\"}", tag, sizeof(tag)),
                "missing key fails");
    TEST_ASSERT(!update_extract_tag("{\"tag_name\":\"\"}", tag, sizeof(tag)), "empty value fails");
    TEST_ASSERT(!update_extract_tag(NULL, tag, sizeof(tag)), "NULL json fails");
    char tiny[4];
    TEST_ASSERT(!update_extract_tag("{\"tag_name\":\"v0.15.0\"}", tiny, sizeof(tiny)),
                "value longer than the buffer fails");
}

TEST_CASE(version_compare_numeric) {
    TEST_ASSERT(update_version_compare("v0.15.0", "v0.15.1") < 0, "patch compares");
    TEST_ASSERT(update_version_compare("v0.9.0", "v0.15.0") < 0, "numeric, not lexicographic");
    TEST_ASSERT(update_version_compare("v1.0.0", "v0.99.99") > 0, "major wins");
    TEST_ASSERT(update_version_compare("v0.15.0", "0.15.0") == 0, "leading v optional");
    TEST_ASSERT(update_version_compare("v0.16.0-dev", "v0.15.0") > 0, "suffix ignored");
    TEST_ASSERT(update_version_compare("v0.15.0", "v0.15.0") == 0, "equal");
}

TEST_CASE(sha256_parse) {
    char hex[65];
    TEST_ASSERT(update_parse_sha256("ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456"
                                    "789  npad-setup-0.15.0.exe\n",
                                    hex),
                "digest with filename parses");
    TEST_ASSERT_EQ((size_t) 64, strlen(hex), "digest is 64 chars");
    TEST_ASSERT(hex[0] == 'a' && hex[5] == 'f', "digest lowercased");
    TEST_ASSERT(!update_parse_sha256("not a digest", hex), "non-hex fails");
    TEST_ASSERT(!update_parse_sha256("abcd", hex), "short digest fails");
    TEST_ASSERT(!update_parse_sha256(NULL, hex), "NULL fails");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(extract_tag_basic);
    RUN_TEST(extract_tag_spaced);
    RUN_TEST(extract_tag_missing_or_bad);
    RUN_TEST(version_compare_numeric);
    RUN_TEST(sha256_parse);

    TEST_SUMMARY();
    return 0;
}
