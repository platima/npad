/*
 * npad - Windows Unicode tests (run on Windows; not part of `make test`)
 *
 * Two launch-path bugs on any system whose ANSI code page cannot spell a name:
 * - argv arrived in the ANSI code page but was read as UTF-8, so a non-ASCII
 *   file name from Explorer, Open with or a console could not be opened
 *   (args_win32_utf8_argv now rebuilds it from the wide command line);
 * - the settings directory came from the ANSI SHGetFolderPathA, so a profile
 *   such as C:\Users\Jos<e-acute> lost settings and crash recovery. That path
 *   is now wide end to end; with -DNPAD_TEST_HOOKS it is taken from
 *   $NPAD_TEST_CONFIG_DIR, which this test points at a non-ASCII folder under
 *   %TEMP% - the real %APPDATA% is never touched.
 *
 * Build: make test-win32-unicode (cross-compiles tests/test_win32_unicode.exe)
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include "../src/platform/args_win32.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

static wchar_t g_root[MAX_PATH]; // %TEMP%\npad-test-Jos<e-acute>-<pid>
static wchar_t g_conf[MAX_PATH]; // ...\conf

static char *to_utf8(const wchar_t *wide) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *out = n > 0 ? malloc((size_t) n) : NULL;
    if (out)
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, n, NULL, NULL);
    return out;
}

TEST_CASE(argv_is_utf8_from_the_wide_command_line) {
    const wchar_t *cmdline = L"\"C:\\Program Files\\npad\\npad.exe\" "
                             L"\"C:\\Users\\Jos\u00e9\\R\u00e9sum\u00e9.txt\" "
                             L"\u65e5\u672c.txt --cascade 2";
    int argc = 0;
    char **argv = args_win32_utf8_argv(cmdline, &argc);
    TEST_ASSERT_NOT_NULL(argv, "converted");
    TEST_ASSERT_EQ(5, argc, "five arguments");
    TEST_ASSERT_STR_EQ("C:\\Program Files\\npad\\npad.exe", argv[0], "quoted exe path");
    TEST_ASSERT_STR_EQ("C:\\Users\\Jos\xc3\xa9\\R\xc3\xa9sum\xc3\xa9.txt", argv[1],
                       "a code-page name arrives as UTF-8, not ANSI bytes");
    TEST_ASSERT_STR_EQ("\xe6\x97\xa5\xe6\x9c\xac.txt", argv[2],
                       "a name the code page cannot spell arrives intact, not as ??.txt");
    TEST_ASSERT_STR_EQ("--cascade", argv[3], "options pass through");
    TEST_ASSERT_STR_EQ("2", argv[4], "and their values");
    TEST_ASSERT_NULL(argv[5], "NULL-terminated like argv");
    args_win32_free_argv(argv, argc);

    TEST_ASSERT_NULL(args_win32_utf8_argv(NULL, &argc), "no command line");
}

TEST_CASE(settings_without_the_test_dir_get_no_directory) {
    // A test build must never fall back to the real %APPDATA%
    _wputenv(L"NPAD_TEST_CONFIG_DIR=");
    settings_cleanup();
    TEST_ASSERT(!settings_init(), "no NPAD_TEST_CONFIG_DIR: no settings directory at all");
    TEST_ASSERT_NULL(settings_get_file_path(), "and no settings path");
}

TEST_CASE(settings_live_under_a_non_ascii_directory) {
    wchar_t assign[MAX_PATH + 32];
    _snwprintf(assign, MAX_PATH + 31, L"NPAD_TEST_CONFIG_DIR=%ls", g_conf);
    assign[MAX_PATH + 31] = L'\0';
    _wputenv(assign);

    settings_cleanup();
    TEST_ASSERT(settings_init(), "init succeeds");
    char *conf8 = to_utf8(g_conf);
    TEST_ASSERT_NOT_NULL(conf8, "utf-8 of the folder");
    char expect[MAX_PATH * 3 + 32];
    snprintf(expect, sizeof expect, "%s\\settings.json", conf8);
    TEST_ASSERT_STR_EQ(expect, settings_get_file_path(), "the path is the folder's UTF-8");
    free(conf8);

    TEST_ASSERT(GetFileAttributesW(g_conf) != INVALID_FILE_ATTRIBUTES,
                "init created the folder by its real (wide) name");
    settings_set_string("theme", "dark");
    TEST_ASSERT(settings_save(), "save succeeds");

    wchar_t wfile[MAX_PATH + 16];
    _snwprintf(wfile, MAX_PATH + 15, L"%ls\\settings.json", g_conf);
    wfile[MAX_PATH + 15] = L'\0';
    TEST_ASSERT(GetFileAttributesW(wfile) != INVALID_FILE_ATTRIBUTES,
                "settings.json is written under that name");

    settings_cleanup();
    TEST_ASSERT(settings_init(), "init again");
    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("dark", theme, "and read back");
    free(theme);
    settings_cleanup();
    DeleteFileW(wfile);
    _wputenv(L"NPAD_TEST_CONFIG_DIR=");
}

TEST_CASE(file_paths_must_be_utf8) {
    // A UTF-8 name opens...
    wchar_t wfile[MAX_PATH + 16];
    _snwprintf(wfile, MAX_PATH + 15, L"%ls\\caf\u00e9.txt", g_root);
    wfile[MAX_PATH + 15] = L'\0';
    char *file8 = to_utf8(wfile);
    TEST_ASSERT_NOT_NULL(file8, "utf-8 name");
    TEST_ASSERT(file_write_text(file8, "hello"), "written by its UTF-8 name");
    char *back = file_read_text(file8);
    TEST_ASSERT_STR_EQ("hello", back, "and read back");
    free(back);

    // ...while the same name in ANSI bytes (0xE9 for e-acute) is refused, not
    // decoded to U+FFFD or handed to the narrow fopen as some other name
    char ansi[MAX_PATH * 3 + 16];
    snprintf(ansi, sizeof ansi, "%s", file8);
    char *e_acute = strstr(ansi, "caf\xc3\xa9");
    TEST_ASSERT_NOT_NULL(e_acute, "found the name");
    e_acute[3] = (char) 0xE9;
    memmove(e_acute + 4, e_acute + 5, strlen(e_acute + 5) + 1);
    errno = 0;
    TEST_ASSERT_NULL(file_read_text(ansi), "an ANSI-encoded path does not open");
    TEST_ASSERT_EQ(EILSEQ, errno, "and says why");
    DeleteFileW(wfile);
    free(file8);
}

int main(void) {
    thread_safety_init();
    TEST_INIT();

    wchar_t temp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        printf("No temporary directory\n");
        return 1;
    }
    _snwprintf(g_root, MAX_PATH - 1, L"%lsnpad-test-Jos\u00e9-%lu", temp,
               (unsigned long) GetCurrentProcessId());
    g_root[MAX_PATH - 1] = L'\0';
    _snwprintf(g_conf, MAX_PATH - 1, L"%ls\\conf", g_root);
    g_conf[MAX_PATH - 1] = L'\0';
    CreateDirectoryW(g_root, NULL);

    RUN_TEST(argv_is_utf8_from_the_wide_command_line);
    RUN_TEST(settings_without_the_test_dir_get_no_directory);
    RUN_TEST(settings_live_under_a_non_ascii_directory);
    RUN_TEST(file_paths_must_be_utf8);

    RemoveDirectoryW(g_conf);
    RemoveDirectoryW(g_root);
    TEST_SUMMARY();
    thread_safety_cleanup();
    return 0;
}
