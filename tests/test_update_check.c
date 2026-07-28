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
#include <stdio.h>
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

TEST_CASE(newer_unskipped) {
    // Newer and not skipped: surface it
    TEST_ASSERT(update_is_newer_unskipped("v0.16.0", "v0.17.0", ""),
                "a newer release surfaces");
    TEST_ASSERT(update_is_newer_unskipped("v0.16.0", "v0.17.0", NULL),
                "NULL skip surfaces");
    // Equal or older: never
    TEST_ASSERT(!update_is_newer_unskipped("v0.17.0", "v0.17.0", ""), "equal does not surface");
    TEST_ASSERT(!update_is_newer_unskipped("v0.17.0", "v0.16.0", ""), "older does not surface");
    // No successful check yet
    TEST_ASSERT(!update_is_newer_unskipped("v0.16.0", "", ""), "empty latest does not surface");
    TEST_ASSERT(!update_is_newer_unskipped("v0.16.0", NULL, ""), "NULL latest does not surface");
    // Skipped exactly: suppressed
    TEST_ASSERT(!update_is_newer_unskipped("v0.16.0", "v0.17.0", "v0.17.0"),
                "skipping the available version suppresses it");
    // A version newer than the skipped one resurfaces
    TEST_ASSERT(update_is_newer_unskipped("v0.16.0", "v0.18.0", "v0.17.0"),
                "a newer release than the skipped one resurfaces");
}

// A realistic (trimmed) releases API response: several assets, the release's
// own "name" before the array, and an uploader sub-object between an asset's
// name and its URL - all of which the resolver has to navigate.
static const char *RELEASE_JSON =
    "{\"url\":\"https://api.github.com/repos/platima/npad/releases/1\","
    "\"tag_name\":\"v0.23.0\",\"name\":\"npad v0.23.0\",\"draft\":false,"
    "\"assets\":[";
static const char *RELEASE_ASSETS =
    "{\"id\":1,\"name\":\"CHECKSUMS.txt\",\"uploader\":{\"login\":\"platima\"},"
    "\"browser_download_url\":\"https://github.com/platima/npad/releases/download/v0.23.0/CHECKSUMS.txt\"},"
    "{\"id\":2,\"name\":\"npad-v0.23.0-msi-win-x64.msi\",\"uploader\":{\"login\":\"platima\"},"
    "\"browser_download_url\":\"https://github.com/platima/npad/releases/download/v0.23.0/npad-v0.23.0-msi-win-x64.msi\"},"
    "{\"id\":3,\"name\":\"npad-v0.23.0-setup-win-x64.exe.sha256\",\"uploader\":{\"login\":\"platima\"},"
    "\"browser_download_url\":\"https://github.com/platima/npad/releases/download/v0.23.0/npad-v0.23.0-setup-win-x64.exe.sha256\"},"
    "{\"id\":4,\"name\":\"npad-v0.23.0-setup-win-x64.exe\",\"uploader\":{\"login\":\"platima\"},"
    "\"browser_download_url\":\"https://github.com/platima/npad/releases/download/v0.23.0/npad-v0.23.0-setup-win-x64.exe\"}"
    "]}";

static void build_release(char *buf, size_t cap) {
    snprintf(buf, cap, "%s%s", RELEASE_JSON, RELEASE_ASSETS);
}

TEST_CASE(find_asset_installer) {
    char json[4096], url[512];
    build_release(json, sizeof(json));
    TEST_ASSERT(update_find_asset_url(json, "-setup-win-x64.exe", url, sizeof(url)),
                "installer asset found");
    TEST_ASSERT_STR_EQ("https://github.com/platima/npad/releases/download/v0.23.0/"
                       "npad-v0.23.0-setup-win-x64.exe",
                       url, "installer URL");
}

TEST_CASE(find_asset_checksum_not_confused_with_installer) {
    // The installer's name is a strict prefix of the digest's, and the digest
    // appears FIRST in the array - suffix matching must still pick correctly
    char json[4096], url[512];
    build_release(json, sizeof(json));
    TEST_ASSERT(update_find_asset_url(json, "-setup-win-x64.exe.sha256", url, sizeof(url)),
                "checksum asset found");
    TEST_ASSERT_STR_EQ("https://github.com/platima/npad/releases/download/v0.23.0/"
                       "npad-v0.23.0-setup-win-x64.exe.sha256",
                       url, "checksum URL");
}

TEST_CASE(find_asset_survives_renaming) {
    // The whole point: a future rename still resolves as long as the suffix
    // is recognisable, without the updater knowing the version or prefix
    const char *renamed =
        "{\"tag_name\":\"v9.9.9\",\"assets\":[{\"name\":\"npad-Setup-Win-x64.EXE\","
        "\"browser_download_url\":\"https://github.com/x/y/releases/download/v9.9.9/"
        "npad-Setup-Win-x64.EXE\"}]}";
    char url[512];
    TEST_ASSERT(update_find_asset_url(renamed, "-setup-win-x64.exe", url, sizeof(url)),
                "case-insensitive suffix match");
}

TEST_CASE(find_asset_missing) {
    char json[4096], url[512];
    build_release(json, sizeof(json));
    TEST_ASSERT(!update_find_asset_url(json, "-portable-win-arm64.exe", url, sizeof(url)),
                "absent asset reports failure");
    TEST_ASSERT(!update_find_asset_url("{\"tag_name\":\"v1\"}", "-setup-win-x64.exe", url,
                                       sizeof(url)),
                "no assets array at all");
    TEST_ASSERT(!update_find_asset_url(NULL, "-setup-win-x64.exe", url, sizeof(url)),
                "NULL json");
}

TEST_CASE(find_asset_out_too_small) {
    char json[4096], tiny[16];
    build_release(json, sizeof(json));
    TEST_ASSERT(!update_find_asset_url(json, "-setup-win-x64.exe", tiny, sizeof(tiny)),
                "does not overflow a small buffer");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(extract_tag_basic);
    RUN_TEST(extract_tag_spaced);
    RUN_TEST(extract_tag_missing_or_bad);
    RUN_TEST(version_compare_numeric);
    RUN_TEST(sha256_parse);
    RUN_TEST(newer_unskipped);
    RUN_TEST(find_asset_installer);
    RUN_TEST(find_asset_checksum_not_confused_with_installer);
    RUN_TEST(find_asset_survives_renaming);
    RUN_TEST(find_asset_missing);
    RUN_TEST(find_asset_out_too_small);

    TEST_SUMMARY();
    return 0;
}
