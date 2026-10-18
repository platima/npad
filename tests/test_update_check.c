/*
 * npad - Update Check Tests
 * Unit tests for release-tag extraction, version comparison and sha256
 * digest parsing used by Help > Check for Updates
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/update_check.h"
#include <stdio.h>
#include <stdlib.h>
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
    // Pre-releases sort before their release, and among themselves naturally
    TEST_ASSERT(update_version_compare("v0.36.0-rc2", "v0.36.0") < 0, "rc before its release");
    TEST_ASSERT(update_version_compare("v0.36.0", "v0.36.0-rc2") > 0, "release after its rc");
    TEST_ASSERT(update_version_compare("v0.36.0-rc2", "v0.35.0") > 0, "rc after the last release");
    TEST_ASSERT(update_version_compare("v0.36.0-rc1", "v0.36.0-rc2") < 0, "rc1 before rc2");
    TEST_ASSERT(update_version_compare("v0.36.0-rc2", "v0.36.0-rc10") < 0, "rc2 before rc10");
    TEST_ASSERT(update_version_compare("v0.36.0-dev", "v0.36.0") < 0, "dev build before release");
    TEST_ASSERT(update_version_compare("v0.36.0-rc2", "v0.36.0-rc2") == 0, "same rc");
    TEST_ASSERT(update_version_compare("v0.36.0 (abc1234)", "v0.36.0") == 0, "commit note ignored");
    TEST_ASSERT(update_is_newer_unskipped("v0.36.0-rc2", "v0.36.0", ""), "rc tester offered the final");
    TEST_ASSERT(!update_is_newer_unskipped("v0.36.0-rc2", "v0.35.0", ""), "but not an older release");

    char v[32];
    update_current_version("v0.36.0", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0") == 0, "release build");
    update_current_version("v0.36.0-rc2", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0-rc2") == 0, "pre-release build keeps its tag");
    update_current_version("v0.36.0-dev (abc1234)", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0-dev") == 0, "dev build drops the commit note");
    update_current_version("0.36.0-dev", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0-dev") == 0, "the header default gains its v");
    update_current_version("v0.35.0", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0") == 0, "a string for other numbers falls back to the triple");
    update_current_version("garbage", 0, 36, 0, v, sizeof(v));
    TEST_ASSERT(strcmp(v, "v0.36.0") == 0, "unrecognised falls back to the triple");
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

TEST_CASE(installer_suffix_per_arch) {
    TEST_ASSERT_STR_EQ("-setup-win-x64.exe", update_installer_suffix("x64", false),
                       "x64 installer");
    TEST_ASSERT_STR_EQ("-setup-win-x64.exe.sha256", update_installer_suffix("x64", true),
                       "x64 digest");
    TEST_ASSERT_STR_EQ("-setup-win-arm64.exe", update_installer_suffix("arm64", false),
                       "arm64 installer");
    TEST_ASSERT_STR_EQ("-setup-win-arm64.exe.sha256", update_installer_suffix("arm64", true),
                       "arm64 digest");
    // Anything else gets nothing - never a fallback to the x64 installer
    TEST_ASSERT(update_installer_suffix(NULL, false) == NULL, "NULL arch");
    TEST_ASSERT(update_installer_suffix("x86", false) == NULL, "32-bit x86 is not shipped");
    TEST_ASSERT(update_installer_suffix("ARM64", false) == NULL, "arch names are exact");
}

TEST_CASE(build_arch_matches_compiler_target) {
    const char *arch = update_build_arch();
#if defined(_M_ARM64) || defined(__aarch64__)
    TEST_ASSERT_STR_EQ("arm64", arch, "an arm64 build reports arm64");
#elif defined(_M_X64) || defined(__x86_64__)
    TEST_ASSERT_STR_EQ("x64", arch, "an x64 build reports x64");
#else
    TEST_ASSERT(arch == NULL, "any other target reports no architecture");
#endif
    // Whatever it is, it must pair with an installer or with none at all
    if (arch)
        TEST_ASSERT(update_installer_suffix(arch, false) != NULL, "build arch has an installer");
}

// Both architectures on one release, x64 first and each digest before its
// installer: every suffix must still resolve to its own asset
static const char *MULTIARCH_RELEASE =
    "{\"tag_name\":\"v0.35.0\",\"assets\":["
    "{\"name\":\"npad-v0.35.0-setup-win-x64.exe.sha256\",\"browser_download_url\":"
    "\"https://github.com/platima/npad/releases/download/v0.35.0/"
    "npad-v0.35.0-setup-win-x64.exe.sha256\"},"
    "{\"name\":\"npad-v0.35.0-setup-win-x64.exe\",\"browser_download_url\":"
    "\"https://github.com/platima/npad/releases/download/v0.35.0/"
    "npad-v0.35.0-setup-win-x64.exe\"},"
    "{\"name\":\"npad-v0.35.0-setup-win-arm64.exe.sha256\",\"browser_download_url\":"
    "\"https://github.com/platima/npad/releases/download/v0.35.0/"
    "npad-v0.35.0-setup-win-arm64.exe.sha256\"},"
    "{\"name\":\"npad-v0.35.0-setup-win-arm64.exe\",\"browser_download_url\":"
    "\"https://github.com/platima/npad/releases/download/v0.35.0/"
    "npad-v0.35.0-setup-win-arm64.exe\"}"
    "]}";

TEST_CASE(find_asset_picks_the_builds_architecture) {
    char url[512];
    TEST_ASSERT(update_find_asset_url(MULTIARCH_RELEASE, update_installer_suffix("arm64", false),
                                      url, sizeof(url)),
                "arm64 installer found");
    TEST_ASSERT_STR_EQ("https://github.com/platima/npad/releases/download/v0.35.0/"
                       "npad-v0.35.0-setup-win-arm64.exe",
                       url, "arm64 build gets the arm64 installer, not the x64 one listed first");
    TEST_ASSERT(update_find_asset_url(MULTIARCH_RELEASE, update_installer_suffix("arm64", true),
                                      url, sizeof(url)),
                "arm64 digest found");
    TEST_ASSERT_STR_EQ("https://github.com/platima/npad/releases/download/v0.35.0/"
                       "npad-v0.35.0-setup-win-arm64.exe.sha256",
                       url, "arm64 digest URL");
    TEST_ASSERT(update_find_asset_url(MULTIARCH_RELEASE, update_installer_suffix("x64", false),
                                      url, sizeof(url)),
                "x64 installer found");
    TEST_ASSERT_STR_EQ("https://github.com/platima/npad/releases/download/v0.35.0/"
                       "npad-v0.35.0-setup-win-x64.exe",
                       url, "x64 build still gets the x64 installer");
}

TEST_CASE(find_asset_arm64_never_falls_back_to_x64) {
    // A release from before arm64 builds existed carries only x64 assets: the
    // arm64 updater must find nothing rather than the x64 installer
    char json[4096], url[512];
    build_release(json, sizeof(json));
    TEST_ASSERT(!update_find_asset_url(json, update_installer_suffix("arm64", false), url,
                                       sizeof(url)),
                "no arm64 installer on an x64-only release");
    TEST_ASSERT(!update_find_asset_url(json, update_installer_suffix("arm64", true), url,
                                       sizeof(url)),
                "no arm64 digest on an x64-only release");
}

// --- HTTP responses (the Linux check) ---------------------------------------

static bool parse(const char *raw, int *status, char **body, size_t *len) {
    return update_http_parse_response(raw, strlen(raw), status, body, len);
}

TEST_CASE(http_plain_body) {
    int status = 0;
    char *body = NULL;
    size_t len = 0;
    TEST_ASSERT(parse("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"tag_name\":"
                      "\"v0.36.0\"}",
                      &status, &body, &len),
                "a body read to the close parses");
    TEST_ASSERT_EQ(200, status, "status code");
    TEST_ASSERT_STR_EQ("{\"tag_name\":\"v0.36.0\"}", body, "body is everything after the headers");
    TEST_ASSERT_EQ(22, (int) len, "body length");
    free(body);
}

TEST_CASE(http_content_length) {
    int status = 0;
    char *body = NULL;
    TEST_ASSERT(
        parse("HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\nhello trailing", &status, &body, NULL),
        "Content-Length (any case) is honoured");
    TEST_ASSERT_STR_EQ("hello", body, "body cut to Content-Length");
    free(body);
    TEST_ASSERT(!parse("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort", &status, &body, NULL),
                "a body shorter than Content-Length is a failure");
    TEST_ASSERT(body == NULL, "no body on failure");
}

TEST_CASE(http_chunked) {
    int status = 0;
    char *body = NULL;
    size_t len = 0;
    TEST_ASSERT(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "4\r\n{\"ta\r\n"
                      "f;ext=1\r\ng_name\":\"v1.0.0\r\n"
                      "2\r\n\"}\r\n"
                      "0\r\n\r\n",
                      &status, &body, &len),
                "chunked body parses");
    TEST_ASSERT_STR_EQ("{\"tag_name\":\"v1.0.0\"}", body, "chunks joined, extension ignored");
    TEST_ASSERT_EQ(21, (int) len, "de-chunked length");
    free(body);

    TEST_ASSERT(!parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10\r\nshort", &status,
                       &body, NULL),
                "a chunk cut short fails");
    TEST_ASSERT(
        !parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nabc\r\n0\r\n\r\n",
               &status, &body, NULL),
        "a malformed chunk size fails");
    TEST_ASSERT(!parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                       "FFFFFFFFFFFFFFFFFFFF\r\nabc\r\n0\r\n\r\n",
                       &status, &body, NULL),
                "a huge chunk size fails rather than overflowing");
}

TEST_CASE(http_status_and_garbage) {
    int status = 0;
    char *body = NULL;
    TEST_ASSERT(parse("HTTP/1.1 403 rate limit exceeded\r\n\r\n{}", &status, &body, NULL),
                "an error status still parses");
    TEST_ASSERT_EQ(403, status, "error status reported");
    free(body);
    TEST_ASSERT(!parse("SSH-2.0-OpenSSH\r\n\r\n", &status, &body, NULL), "not HTTP fails");
    TEST_ASSERT(!parse("HTTP/1.1 200 OK\r\nServer: x\r\n", &status, &body, NULL),
                "headers that never end fail");
    TEST_ASSERT(!parse("HTTP/1.1 2x0 OK\r\n\r\n", &status, &body, NULL), "bad status fails");
    TEST_ASSERT(!update_http_parse_response(NULL, 0, &status, &body, NULL), "NULL fails");
}

// --- How this copy was installed --------------------------------------------

static UpdateInstallSource source_of(UpdatePackageOwner owner, const char *package, bool apt,
                                     bool dnf, bool pacman) {
    UpdateInstallFacts facts = { owner, package, apt, dnf, pacman };
    return update_install_source(&facts);
}

TEST_CASE(install_source_decision) {
    TEST_ASSERT_EQ(UPDATE_SOURCE_APT_REPO, source_of(UPDATE_OWNER_DPKG, "npad", true, false, false),
                   "dpkg + apt source = apt repo");
    TEST_ASSERT_EQ(UPDATE_SOURCE_DEB_FILE, source_of(UPDATE_OWNER_DPKG, "npad", false, true, false),
                   "dpkg without the apt source = a downloaded .deb (a dnf file is irrelevant)");
    TEST_ASSERT_EQ(UPDATE_SOURCE_DNF_REPO, source_of(UPDATE_OWNER_RPM, "npad", false, true, false),
                   "rpm + dnf repo = dnf repo");
    TEST_ASSERT_EQ(UPDATE_SOURCE_RPM_FILE, source_of(UPDATE_OWNER_RPM, "npad", true, false, false),
                   "rpm without the dnf repo = a downloaded rpm");
    TEST_ASSERT_EQ(UPDATE_SOURCE_PACMAN_REPO,
                   source_of(UPDATE_OWNER_PACMAN, "npad", false, false, true),
                   "pacman + [npad] = pacman repo");
    TEST_ASSERT_EQ(UPDATE_SOURCE_PACMAN_FILE,
                   source_of(UPDATE_OWNER_PACMAN, "npad", false, false, false),
                   "pacman without [npad] = installed by hand");
    TEST_ASSERT_EQ(UPDATE_SOURCE_AUR,
                   source_of(UPDATE_OWNER_PACMAN, "npad-bin", false, false, true),
                   "npad-bin is the AUR even with [npad] configured");
    TEST_ASSERT_EQ(UPDATE_SOURCE_AUR,
                   source_of(UPDATE_OWNER_PACMAN, "npad-git", false, false, false),
                   "npad-git is the AUR");
    TEST_ASSERT_EQ(UPDATE_SOURCE_PACMAN_FILE,
                   source_of(UPDATE_OWNER_PACMAN, NULL, false, false, false),
                   "unknown pacman package name is not assumed to be the AUR");
    TEST_ASSERT_EQ(UPDATE_SOURCE_PORTABLE, source_of(UPDATE_OWNER_NONE, NULL, true, true, true),
                   "not owned = portable, whatever repos exist");
    TEST_ASSERT_EQ(UPDATE_SOURCE_PORTABLE, update_install_source(NULL), "NULL = portable");
}

TEST_CASE(install_advice_ends_with_the_command) {
    struct {
        UpdateInstallSource source;
        const char *last_line;
    } cases[] = {
        { UPDATE_SOURCE_APT_REPO, "sudo apt update && sudo apt upgrade" },
        { UPDATE_SOURCE_DNF_REPO, "sudo dnf upgrade npad" },
        { UPDATE_SOURCE_PACMAN_REPO, "sudo pacman -Syu" },
        { UPDATE_SOURCE_AUR, "yay -Syu" },
        { UPDATE_SOURCE_DEB_FILE, "curl -fsSL https://pkg.npad.dev/install.sh | sudo sh" },
        { UPDATE_SOURCE_RPM_FILE, "curl -fsSL https://pkg.npad.dev/install.sh | sudo sh" },
        { UPDATE_SOURCE_PACMAN_FILE, "https://pkg.npad.dev" },
        { UPDATE_SOURCE_PORTABLE, "https://github.com/platima/npad/releases/latest" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *advice = update_install_advice(cases[i].source);
        const char *nl = advice ? strrchr(advice, '\n') : NULL;
        TEST_ASSERT(nl != NULL, "advice has a command line");
        TEST_ASSERT_STR_EQ(cases[i].last_line, nl ? nl + 1 : "", "advice's last line");
        TEST_ASSERT(strstr(advice, "Npad") == NULL && strstr(advice, "NPAD") == NULL,
                    "the name is always lowercase");
    }
}

TEST_CASE(exe_may_be_packaged) {
    TEST_ASSERT(update_exe_may_be_packaged("/usr/bin/npad"), "/usr/bin");
    TEST_ASSERT(update_exe_may_be_packaged("/bin/npad"), "/bin (unmerged /usr)");
    TEST_ASSERT(!update_exe_may_be_packaged("/usr/local/bin/npad"), "make install");
    TEST_ASSERT(!update_exe_may_be_packaged("/home/me/.local/bin/npad"), "~/.local/bin");
    TEST_ASSERT(!update_exe_may_be_packaged("/home/me/npad-v0.36.0-portable-linux-x86_64/npad"), "tarball");
    TEST_ASSERT(!update_exe_may_be_packaged("/usr/bin/sub/npad"), "below /usr/bin");
    TEST_ASSERT(!update_exe_may_be_packaged("/usr/bin/"), "no file name");
    TEST_ASSERT(!update_exe_may_be_packaged(NULL), "NULL");
}

TEST_CASE(repo_text_names_npad) {
    TEST_ASSERT(
        update_repo_text_names_npad("Types: deb\nURIs: https://pkg.npad.dev/apt\n"
                                    "Suites: stable\nSigned-By: /etc/apt/keyrings/npad.asc\n"),
        "deb822 .sources");
    TEST_ASSERT(
        update_repo_text_names_npad("deb [signed-by=/k.asc] https://pkg.npad.dev/apt stable "
                                    "main\r\n"),
        "one-line .list, CRLF");
    TEST_ASSERT(update_repo_text_names_npad("[npad]\nname=npad\nbaseurl=https://PKG.npad.dev/rpm/"
                                            "$basearch\n"),
                "dnf .repo, host case-insensitive");
    TEST_ASSERT(!update_repo_text_names_npad("# deb https://pkg.npad.dev/apt stable main\n"),
                "commented out");
    TEST_ASSERT(!update_repo_text_names_npad("  ; baseurl=https://pkg.npad.dev/rpm\n"),
                "ini comment");
    TEST_ASSERT(!update_repo_text_names_npad("deb http://deb.debian.org/debian bookworm main\n"),
                "another repository");
    TEST_ASSERT(!update_repo_text_names_npad(""), "empty");
    TEST_ASSERT(!update_repo_text_names_npad(NULL), "NULL");
}

TEST_CASE(pacman_conf_has_npad) {
    TEST_ASSERT(update_pacman_conf_has_npad("[options]\nArchitecture = auto\n\n[core]\n"
                                            "Include = /etc/pacman.d/mirrorlist\n\n[npad]\n"
                                            "Server = https://pkg.npad.dev/arch/$arch\n"),
                "[npad] section");
    TEST_ASSERT(update_pacman_conf_has_npad("  [npad]   # npad's own repo\r\n"),
                "indented, trailing comment, CRLF");
    TEST_ASSERT(!update_pacman_conf_has_npad("#[npad]\n#Server = https://pkg.npad.dev/arch\n"),
                "commented out");
    TEST_ASSERT(!update_pacman_conf_has_npad("[npad-testing]\n"), "a different section");
    TEST_ASSERT(!update_pacman_conf_has_npad("[core]\n"), "no section");
    TEST_ASSERT(!update_pacman_conf_has_npad(NULL), "NULL");
}

TEST_CASE(owner_output_parsing) {
    char pkg[64];
    TEST_ASSERT(
        update_parse_owner_output(UPDATE_OWNER_DPKG, "npad: /usr/bin/npad\n", pkg, sizeof(pkg)),
        "dpkg -S");
    TEST_ASSERT_STR_EQ("npad", pkg, "dpkg package name");
    TEST_ASSERT(update_parse_owner_output(UPDATE_OWNER_DPKG, "npad:amd64: /usr/bin/npad\n", pkg,
                                          sizeof(pkg)),
                "dpkg -S with an arch qualifier");
    TEST_ASSERT_STR_EQ("npad", pkg, "qualifier dropped");
    TEST_ASSERT(update_parse_owner_output(UPDATE_OWNER_DPKG,
                                          "diversion by foo from: /usr/bin/npad\n"
                                          "diversion by foo to: /usr/bin/npad.real\n"
                                          "npad, foo: /usr/bin/npad\n",
                                          pkg, sizeof(pkg)),
                "dpkg -S with diversions and two owners");
    TEST_ASSERT_STR_EQ("npad", pkg, "first owner");
    TEST_ASSERT(!update_parse_owner_output(
                    UPDATE_OWNER_DPKG, "dpkg-query: no path found matching pattern /usr/bin/npad\n",
                    pkg, sizeof(pkg)),
                "dpkg not owned");

    TEST_ASSERT(update_parse_owner_output(UPDATE_OWNER_RPM, "npad", pkg, sizeof(pkg)),
                "rpm --queryformat, no newline");
    TEST_ASSERT_STR_EQ("npad", pkg, "rpm package name");
    TEST_ASSERT(!update_parse_owner_output(UPDATE_OWNER_RPM,
                                           "file /usr/bin/npad is not owned by any package\n", pkg,
                                           sizeof(pkg)),
                "rpm not owned");

    TEST_ASSERT(update_parse_owner_output(UPDATE_OWNER_PACMAN, "npad-bin\n", pkg, sizeof(pkg)),
                "pacman -Qqo");
    TEST_ASSERT_STR_EQ("npad-bin", pkg, "pacman package name");
    TEST_ASSERT(!update_parse_owner_output(UPDATE_OWNER_PACMAN,
                                           "error: No package owns /usr/bin/npad\n", pkg,
                                           sizeof(pkg)),
                "pacman not owned");

    TEST_ASSERT(!update_parse_owner_output(UPDATE_OWNER_RPM, "", pkg, sizeof(pkg)), "empty");
    TEST_ASSERT(!update_parse_owner_output(UPDATE_OWNER_NONE, "npad", pkg, sizeof(pkg)),
                "no owner kind");
    char tiny[3];
    TEST_ASSERT(!update_parse_owner_output(UPDATE_OWNER_RPM, "npad", tiny, sizeof(tiny)),
                "name longer than the buffer");
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
    RUN_TEST(installer_suffix_per_arch);
    RUN_TEST(build_arch_matches_compiler_target);
    RUN_TEST(find_asset_picks_the_builds_architecture);
    RUN_TEST(find_asset_arm64_never_falls_back_to_x64);
    RUN_TEST(http_plain_body);
    RUN_TEST(http_content_length);
    RUN_TEST(http_chunked);
    RUN_TEST(http_status_and_garbage);
    RUN_TEST(install_source_decision);
    RUN_TEST(install_advice_ends_with_the_command);
    RUN_TEST(exe_may_be_packaged);
    RUN_TEST(repo_text_names_npad);
    RUN_TEST(pacman_conf_has_npad);
    RUN_TEST(owner_output_parsing);

    TEST_SUMMARY();
    return 0;
}
