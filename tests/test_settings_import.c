/*
 * npad - Settings Import Tests
 * Preferences > Backup > Import Settings used to copy whatever file was chosen
 * over settings.json, unchecked and non-atomically: picking another program's
 * settings.json, a .txt or a UTF-16 file wiped every preference, recent file
 * and history entry, and a full disk could leave settings.json half written.
 * These cases pin the replacement: only an npad settings file is accepted,
 * the previous settings are kept as settings.json.bak, and a refused or failed
 * import changes nothing.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/core/settings.h"
#include "../src/core/settings_import.h"
#include "../src/core/thread_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVE "test_import_live.json"
#define LIVE_BAK "test_import_live.json.bak"
#define SRC "test_import_src.json"
#define BLOCKER "test_import_blocker"

static void write_bytes(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

static void write_text(const char *path, const char *text) {
    write_bytes(path, text, strlen(text));
}

// The same text as UTF-16 LE with a BOM, as another editor might re-save it
static void write_utf16le(const char *path, const char *ascii) {
    size_t n = strlen(ascii);
    unsigned char *buf = malloc(2 + n * 2);
    if (!buf)
        return;
    buf[0] = 0xFF;
    buf[1] = 0xFE;
    for (size_t i = 0; i < n; i++) {
        buf[2 + i * 2] = (unsigned char) ascii[i];
        buf[3 + i * 2] = 0;
    }
    write_bytes(path, buf, 2 + n * 2);
    free(buf);
}

static bool file_contains(const char *path, const char *needle) {
    char *text = file_read_text(path);
    bool found = text && strstr(text, needle) != NULL;
    free(text);
    return found;
}

// The user's current settings before each import: a preference, a recent file
// and window geometry, saved to the live file
static void seed_live(void) {
    remove(LIVE);
    remove(LIVE_BAK);
    settings_clear_all();
    settings_set_file_path(LIVE);
    settings_set_string("theme", "dark");
    settings_set_string("recent_file_0", "C:\\Notes\\secret.txt");
    settings_set_int("window_x", 10);
    settings_save();
}

static void cleanup_files(void) {
    settings_clear_all();
    remove(LIVE);
    remove(LIVE_BAK);
    remove(SRC);
}

// A file Export Settings writes (the serializer's own layout)
static const char *const EXPORTED = "{\n"
                                    "  \"theme\": \"solarized-light\",\n"
                                    "  \"word_wrap\": \"true\",\n"
                                    "  \"window_width\": \"900\",\n"
                                    "  \"find_hist_0\": \"needle\"\n"
                                    "}\n";

// --- Recognising an npad settings file ---------------------------------------

TEST_CASE(npad_keys_are_recognised) {
    TEST_ASSERT(settings_import_is_npad_key("theme"), "an exact npad key");
    TEST_ASSERT(settings_import_is_npad_key("recent_file_3"), "a recent file");
    TEST_ASSERT(settings_import_is_npad_key("window_maximized"), "window geometry");
    TEST_ASSERT(settings_import_is_npad_key("find_match_case"), "a Find option");
    TEST_ASSERT(settings_import_is_npad_key("print_margin_left"), "print setup");
    TEST_ASSERT(settings_import_is_npad_key("tab_width"), "the Editor page");
    TEST_ASSERT(!settings_import_is_npad_key("window_"), "a bare prefix is not a key");
    TEST_ASSERT(!settings_import_is_npad_key("editor.fontSize"), "VS Code's keys are not");
    TEST_ASSERT(!settings_import_is_npad_key(""), "empty");
    TEST_ASSERT(!settings_import_is_npad_key(NULL), "NULL");
}

TEST_CASE(exported_file_is_accepted) {
    TEST_ASSERT(settings_import_looks_like_settings(EXPORTED), "an export");
    TEST_ASSERT(settings_import_looks_like_settings("\xEF\xBB\xBF{\"theme\": \"dark\"}"),
                "with a UTF-8 BOM");
    TEST_ASSERT(settings_import_looks_like_settings("{\"window_x\": 5, \"window_y\": -3,}"),
                "bare numbers and a trailing comma");
    TEST_ASSERT(settings_import_looks_like_settings(
                    "{\"theme\": \"dark\", \"newer_pref\": \"1\", \"window_x\": \"1\"}"),
                "a key from a newer npad is outvoted by npad's own");
    TEST_ASSERT(settings_import_looks_like_settings(
                    "{\"recent_file_0\": \"C:\\\\a \\\"b\\\".txt\", \"theme\": \"x\"}"),
                "escaped quotes and backslashes inside a value");
}

TEST_CASE(other_files_are_refused) {
    TEST_ASSERT(!settings_import_looks_like_settings(""), "empty file");
    TEST_ASSERT(!settings_import_looks_like_settings("Shopping list\nmilk\n"), "plain text");
    TEST_ASSERT(!settings_import_looks_like_settings("{}"), "an empty object has nothing");
    TEST_ASSERT(!settings_import_looks_like_settings("[\"theme\"]"), "an array");
    TEST_ASSERT(!settings_import_looks_like_settings(
                    "{\n  \"editor.fontSize\": 14,\n  \"workbench.colorTheme\": \"Dark+\",\n"
                    "  \"files.autoSave\": \"afterDelay\"\n}\n"),
                "VS Code's settings.json");
    TEST_ASSERT(!settings_import_looks_like_settings(
                    "{\"theme\": \"Default.sublime-theme\", \"font_size\": 12,"
                    " \"color_scheme\": \"Mariana\", \"ignored_packages\": [\"Vintage\"],"
                    " \"translate_tabs_to_spaces\": true}"),
                "an editor's file sharing two generic names is still mostly foreign");
    TEST_ASSERT(!settings_import_looks_like_settings("{\"theme\": \"dark\", \"window_x\": \"1\""),
                "a file cut off before its closing brace");
    TEST_ASSERT(!settings_import_looks_like_settings("{\"theme\": \"dar"), "cut off mid-value");
    TEST_ASSERT(!settings_import_looks_like_settings("{\"theme\" \"dark\"}"), "missing colon");
    TEST_ASSERT(!settings_import_looks_like_settings("{\"theme\": \"dark\" \"word_wrap\": 1}"),
                "missing comma");
    TEST_ASSERT(!settings_import_looks_like_settings(NULL), "NULL");

    // A key longer than the scanner keeps must not match a prefix by truncation
    char long_key[400];
    strcpy(long_key, "{\"window_");
    size_t len = strlen(long_key);
    memset(long_key + len, 'x', 300);
    strcpy(long_key + len + 300, "\": 1}");
    TEST_ASSERT(!settings_import_looks_like_settings(long_key), "an over-long key is foreign");
}

// --- Importing -----------------------------------------------------------------

TEST_CASE(import_replaces_settings_and_keeps_a_backup) {
    seed_live();
    write_text(SRC, EXPORTED);

    TEST_ASSERT_EQ(SETTINGS_IMPORT_OK, settings_import_file(SRC), "the export is imported");

    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("solarized-light", theme, "the imported values are live in memory");
    free(theme);
    TEST_ASSERT_EQ(900, settings_get_int("window_width", 0), "every one of them");
    TEST_ASSERT(!settings_has_key("recent_file_0"),
                "import REPLACES: a key the file does not have is gone");
    TEST_ASSERT(file_contains(LIVE, "solarized-light"), "the live file holds the import");
    TEST_ASSERT(!file_contains(LIVE, "secret.txt"), "and not the old settings");

    TEST_ASSERT(file_exists(LIVE_BAK), "the previous settings are kept");
    TEST_ASSERT(file_contains(LIVE_BAK, "secret.txt"), "the backup holds the old recent file");
    TEST_ASSERT(file_contains(LIVE_BAK, "\"dark\""), "and the old theme");

    // The live file reloads to the same thing (what every other window reads)
    settings_clear_all();
    settings_load();
    char *again = settings_get_string("find_hist_0", "");
    TEST_ASSERT_STR_EQ("needle", again, "the written file loads back");
    free(again);
    cleanup_files();
}

TEST_CASE(backup_includes_unsaved_changes) {
    seed_live();
    // Changed in memory only, as Recent Files and Find history are until exit
    settings_set_string("recent_file_1", "C:\\Notes\\unsaved.txt");
    write_text(SRC, EXPORTED);

    TEST_ASSERT_EQ(SETTINGS_IMPORT_OK, settings_import_file(SRC), "imported");
    TEST_ASSERT(file_contains(LIVE_BAK, "unsaved.txt"),
                "the backup is what the user had, not only what was on disk");
    cleanup_files();
}

TEST_CASE(utf16_file_is_converted) {
    seed_live();
    write_utf16le(SRC, EXPORTED);

    TEST_ASSERT_EQ(SETTINGS_IMPORT_OK, settings_import_file(SRC), "a UTF-16 export imports");
    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("solarized-light", theme, "with its values intact");
    free(theme);
    TEST_ASSERT(file_contains(LIVE, "\"window_width\": \"900\""),
                "and the live file is written as UTF-8");
    cleanup_files();
}

// After a refused import the store and the live file are exactly as before
static bool unchanged_after_refusal(void) {
    char *theme = settings_get_string("theme", "");
    bool same = strcmp(theme, "dark") == 0 && settings_has_key("recent_file_0") &&
                settings_get_int("window_x", 0) == 10 && file_contains(LIVE, "secret.txt") &&
                !file_exists(LIVE_BAK);
    free(theme);
    return same;
}

TEST_CASE(wrong_files_change_nothing) {
    seed_live();
    write_text(SRC, "Shopping list\nmilk\n");
    TEST_ASSERT_EQ(SETTINGS_IMPORT_NOT_SETTINGS, settings_import_file(SRC), "a .txt is refused");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");

    write_text(SRC, "{\n  \"editor.fontSize\": 14,\n  \"files.autoSave\": \"off\"\n}\n");
    TEST_ASSERT_EQ(SETTINGS_IMPORT_NOT_SETTINGS, settings_import_file(SRC),
                   "another program's settings.json is refused");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");

    static const char binary[] = "{\"theme\": \"x\"}\0\x01\x02 more";
    write_bytes(SRC, binary, sizeof(binary));
    TEST_ASSERT_EQ(SETTINGS_IMPORT_NOT_SETTINGS, settings_import_file(SRC),
                   "a file with a NUL inside is refused");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");

    write_text(SRC, "{\"theme\": \"caf\xE9\", \"window_x\": \"1\"}");
    TEST_ASSERT_EQ(SETTINGS_IMPORT_NOT_SETTINGS, settings_import_file(SRC),
                   "bytes that are not UTF-8 are refused");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");

    remove(SRC);
    TEST_ASSERT_EQ(SETTINGS_IMPORT_UNREADABLE, settings_import_file(SRC),
                   "a file that is gone is reported as unreadable");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");
    cleanup_files();
}

TEST_CASE(oversized_file_is_refused) {
    seed_live();
    FILE *f = fopen(SRC, "wb");
    TEST_ASSERT_NOT_NULL(f, "source created");
    fputs("{\"theme\": \"", f);
    for (long i = 0; i < SETTINGS_IMPORT_MAX_BYTES; i++)
        fputc('x', f);
    fputs("\"}", f);
    fclose(f);

    TEST_ASSERT_EQ(SETTINGS_IMPORT_NOT_SETTINGS, settings_import_file(SRC),
                   "a file larger than any settings file is refused");
    TEST_ASSERT(unchanged_after_refusal(), "and changes nothing");
    cleanup_files();
}

TEST_CASE(failed_write_changes_nothing) {
    // The settings folder cannot be created: a FILE sits where it should be,
    // so every save fails (the disk-full shape, without filling a disk)
    remove(BLOCKER);
    write_text(BLOCKER, "not a folder");
    settings_clear_all();
    settings_set_file_path(BLOCKER "/settings.json");
    settings_set_string("theme", "dark");
    settings_set_string("recent_file_0", "C:\\Notes\\secret.txt");
    write_text(SRC, EXPORTED);

    TEST_ASSERT_EQ(SETTINGS_IMPORT_SAVE_FAILED, settings_import_file(SRC),
                   "an import that cannot be written is reported");
    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("dark", theme, "the in-memory settings are untouched");
    free(theme);
    TEST_ASSERT(settings_has_key("recent_file_0"), "including the recent file");

    settings_clear_all();
    remove(BLOCKER);
    remove(SRC);
}

TEST_CASE(importing_the_live_file_onto_itself) {
    seed_live();
    TEST_ASSERT_EQ(SETTINGS_IMPORT_OK, settings_import_file(LIVE), "self-import succeeds");
    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("dark", theme, "and keeps the settings");
    free(theme);
    TEST_ASSERT(settings_has_key("recent_file_0"), "all of them");
    TEST_ASSERT(file_contains(LIVE, "secret.txt"), "on disk too");
    cleanup_files();
}

TEST_CASE(every_result_has_a_message) {
    TEST_ASSERT(strstr(settings_import_message(SETTINGS_IMPORT_OK), "settings.json.bak") != NULL,
                "success names the backup");
    TEST_ASSERT(strstr(settings_import_message(SETTINGS_IMPORT_NOT_SETTINGS), "npad") != NULL,
                "a refusal says what was wrong");
    TEST_ASSERT(settings_import_message(SETTINGS_IMPORT_UNREADABLE)[0] != '\0', "unreadable");
    TEST_ASSERT(settings_import_message(SETTINGS_IMPORT_SAVE_FAILED)[0] != '\0', "save failed");
}

int main(void) {
    thread_safety_init();
    TEST_INIT();

    RUN_TEST(npad_keys_are_recognised);
    RUN_TEST(exported_file_is_accepted);
    RUN_TEST(other_files_are_refused);
    RUN_TEST(import_replaces_settings_and_keeps_a_backup);
    RUN_TEST(backup_includes_unsaved_changes);
    RUN_TEST(utf16_file_is_converted);
    RUN_TEST(wrong_files_change_nothing);
    RUN_TEST(oversized_file_is_refused);
    RUN_TEST(failed_write_changes_nothing);
    RUN_TEST(importing_the_live_file_onto_itself);
    RUN_TEST(every_result_has_a_message);

    TEST_SUMMARY();
    thread_safety_cleanup();
    return g_test_stats.tests_failed ? 1 : 0;
}
