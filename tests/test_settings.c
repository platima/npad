/*
 * npad - Settings Tests
 * Guards the "Reset All Preferences" contract: reset removes every preference
 * key by default and preserves only recent files, window geometry, and
 * find/replace state. This is the drift guard - if a future preference is not
 * reset (the historical bug), or the preserved set is widened by mistake, a
 * case here fails.
 *
 * Author: Platima
 * https://npad.dev
 */

// setenv/unsetenv/mkdir for the XDG tests (c99 mode hides them otherwise)
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "../src/core/settings.h"
#include "../src/core/file_ops.h"
#include "../src/core/thread_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

// The exact preserve list used by reset_all_preferences (kept in sync here so
// the test pins the real contract).
static const char *const KEEP[] = { "recent_file_", "window_", "find_", "replace_" };
#define KEEP_COUNT ((int) (sizeof(KEEP) / sizeof(KEEP[0])))

TEST_CASE(reset_clears_preferences) {
    settings_clear_all();
    // Preferences from several tabs - every one must be removed by a reset,
    // including ones added in later rounds (the historical drift bug)
    settings_set_bool("list_tools_enabled", true);        // Markdown
    settings_set_int("list_default_indent_format", 6);    // Markdown
    settings_set_string("update_mode", "auto");           // Updates
    settings_set_bool("update_check_on_launch", true);    // Updates
    settings_set_bool("status_show_counts", true);        // Appearance (counts)
    settings_set_string("theme", "solarized-dark");       // Appearance
    settings_set_bool("auto_save_enabled", true);         // General
    settings_set_int("recent_files_max", 5);              // General (NOT a recent_file_ key)

    settings_reset_except_prefixes(KEEP, KEEP_COUNT);

    TEST_ASSERT(!settings_has_key("list_tools_enabled"), "Markdown pref reset");
    TEST_ASSERT(!settings_has_key("list_default_indent_format"), "Markdown format pref reset");
    TEST_ASSERT(!settings_has_key("update_mode"), "Updates mode reset");
    TEST_ASSERT(!settings_has_key("update_check_on_launch"), "Updates launch pref reset");
    TEST_ASSERT(!settings_has_key("status_show_counts"), "counts pref reset");
    TEST_ASSERT(!settings_has_key("theme"), "theme reset");
    TEST_ASSERT(!settings_has_key("auto_save_enabled"), "auto-save reset");
    TEST_ASSERT(!settings_has_key("recent_files_max"),
                "recent_files_max is a preference, not a preserved recent_file_ key");
    settings_clear_all();
}

TEST_CASE(reset_preserves_kept_categories) {
    settings_clear_all();
    settings_set_string("recent_file_0", "C:/a.txt");
    settings_set_string("recent_file_9", "C:/b.txt");
    settings_set_int("window_x", 100);
    settings_set_int("window_width", 800);
    settings_set_bool("window_maximized", true);
    settings_set_bool("find_match_case", true);
    settings_set_bool("find_wrap_around", true);
    settings_set_string("find_hist_0", "needle");
    settings_set_string("replace_hist_0", "thread");

    settings_reset_except_prefixes(KEEP, KEEP_COUNT);

    TEST_ASSERT(settings_has_key("recent_file_0"), "recent file 0 kept");
    TEST_ASSERT(settings_has_key("recent_file_9"), "recent file 9 kept");
    TEST_ASSERT(settings_has_key("window_x"), "window x kept");
    TEST_ASSERT(settings_has_key("window_width"), "window width kept");
    TEST_ASSERT(settings_has_key("window_maximized"), "window maximized kept");
    TEST_ASSERT(settings_has_key("find_match_case"), "find option kept");
    TEST_ASSERT(settings_has_key("find_wrap_around"), "find option kept");
    TEST_ASSERT(settings_has_key("find_hist_0"), "find history kept");
    TEST_ASSERT(settings_has_key("replace_hist_0"), "replace history kept");
    settings_clear_all();
}

TEST_CASE(reset_returns_removed_count) {
    settings_clear_all();
    settings_set_bool("theme_dummy_pref", true);
    settings_set_bool("another_pref", true);
    settings_set_string("window_x", "5"); // preserved
    int removed = settings_reset_except_prefixes(KEEP, KEEP_COUNT);
    TEST_ASSERT_EQ(2, removed, "removed count excludes preserved keys");
    settings_clear_all();
}

// --- Save/load round trip -------------------------------------------------
//
// The escaping bug lived here, unseen, because nothing exercised the round
// trip. serialize_settings escaped backslashes on write and the parser never
// unescaped them on read, so every save/load DOUBLED each backslash in a
// value. A Windows path reached 1.2 GB in the field, exhausted 64 GB of RAM
// across a handful of instances, and took every other setting with it when it
// overflowed the serializer's buffer.
//
// The growth test below is the one that matters: a single round trip looks
// fine to the eye, and only repetition exposes the doubling.

#define ROUNDTRIP_FILE "test_settings_roundtrip.json"

static void roundtrip(void) {
    settings_save();
    settings_clear_all();
    settings_load();
}

TEST_CASE(roundtrip_preserves_windows_path) {
    settings_clear_all();
    settings_set_file_path(ROUNDTRIP_FILE);
    const char *path = "E:\\Projects\\npad\\data.json";
    settings_set_string("recent_file_0", path);

    roundtrip();

    char *got = settings_get_string("recent_file_0", "");
    TEST_ASSERT_STR_EQ(path, got, "a Windows path survives one round trip");
    free(got);
    settings_clear_all();
    remove(ROUNDTRIP_FILE);
}

TEST_CASE(roundtrip_does_not_grow_value) {
    settings_clear_all();
    settings_set_file_path(ROUNDTRIP_FILE);
    const char *path = "E:\\Projects\\npad\\data.json";
    settings_set_string("recent_file_0", path);
    size_t original = strlen(path);

    // Ten cycles. Under the old behaviour this value would be 1024x longer.
    for (int i = 0; i < 10; i++) {
        roundtrip();
    }

    char *got = settings_get_string("recent_file_0", "");
    TEST_ASSERT_EQ((int) original, (int) strlen(got), "value length is stable across 10 cycles");
    TEST_ASSERT_STR_EQ(path, got, "and the content is still exact");
    free(got);
    settings_clear_all();
    remove(ROUNDTRIP_FILE);
}

TEST_CASE(roundtrip_preserves_quotes_and_tabs) {
    settings_clear_all();
    settings_set_file_path(ROUNDTRIP_FILE);
    settings_set_string("delim_from", "a\"b\tc\\d");

    roundtrip();

    char *got = settings_get_string("delim_from", "");
    TEST_ASSERT_STR_EQ("a\"b\tc\\d", got, "quote, tab and backslash all round trip");
    free(got);
    settings_clear_all();
    remove(ROUNDTRIP_FILE);
}

TEST_CASE(oversized_value_is_dropped_on_load) {
    settings_clear_all();
    settings_set_file_path(ROUNDTRIP_FILE);

    // Hand-write a file with one absurd value, as a corrupt install would have.
    // It must be ignored rather than loaded, so npad still starts.
    FILE *f = fopen(ROUNDTRIP_FILE, "wb");
    TEST_ASSERT_NOT_NULL(f, "test file created");
    if (f) {
        fputs("{\n  \"sane_key\": \"ok\",\n  \"runaway\": \"", f);
        for (int i = 0; i < 70000; i++) {
            fputc('x', f);
        }
        fputs("\"\n}\n", f);
        fclose(f);
    }

    settings_clear_all();
    settings_load();

    char *sane = settings_get_string("sane_key", "");
    TEST_ASSERT_STR_EQ("ok", sane, "a sane key beside it still loads");
    free(sane);
    TEST_ASSERT(!settings_has_key("runaway"), "the oversized value is dropped");

    settings_clear_all();
    remove(ROUNDTRIP_FILE);
}

TEST_CASE(oversized_file_is_salvaged_not_discarded) {
    settings_clear_all();
    settings_set_file_path(ROUNDTRIP_FILE);
    remove(ROUNDTRIP_FILE ".corrupt");

    // A file too large to load whole, with real settings written BEFORE the
    // runaway value - which is the shape the escaping bug actually produced.
    // v0.28.7 discarded the lot, and because npad rewrites settings on exit
    // the original was then overwritten: upgrading lost the user's settings.
    FILE *f = fopen(ROUNDTRIP_FILE, "wb");
    TEST_ASSERT_NOT_NULL(f, "oversized test file created");
    if (f) {
        fputs("{\n  \"theme\": \"solarized-dark\",\n", f);
        fputs("  \"window_width\": \"2150\",\n", f);
        fputs("  \"runaway\": \"", f);
        for (long i = 0; i < 9L * 1024 * 1024; i++) {
            fputc('x', f);
        }
        fputs("\"\n}\n", f);
        fclose(f);
    }

    settings_clear_all();
    settings_load();

    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("solarized-dark", theme, "settings before the runaway value survive");
    free(theme);
    TEST_ASSERT_EQ(2150, settings_get_int("window_width", 0), "and so do later ones");
    TEST_ASSERT(!settings_has_key("runaway"), "the runaway value itself is not kept");

    // The original must still exist somewhere: losing it is the regression
    TEST_ASSERT(file_exists(ROUNDTRIP_FILE ".corrupt"), "original preserved as .corrupt");
    TEST_ASSERT(!file_exists(ROUNDTRIP_FILE), "and moved out of the load path");

    settings_clear_all();
    remove(ROUNDTRIP_FILE);
    remove(ROUNDTRIP_FILE ".corrupt");
}

// --- Config directory: $XDG_CONFIG_HOME ------------------------------------
//
// Settings live in $XDG_CONFIG_HOME/npad when that is set and absolute, else
// ~/.config/npad. Core code, so Windows (which uses %APPDATA%) must be
// untouched: the resolvers are pure string functions and Windows never calls
// them.

#ifndef _WIN32 // file_join_paths uses backslashes on Windows
TEST_CASE(config_dir_prefers_absolute_xdg) {
    char *dir = settings_resolve_config_dir("/xdg/conf", "/home/u");
    TEST_ASSERT_STR_EQ("/xdg/conf/npad", dir, "an absolute XDG_CONFIG_HOME wins");
    free(dir);

    dir = settings_resolve_config_dir(NULL, "/home/u");
    TEST_ASSERT_STR_EQ("/home/u/.config/npad", dir, "unset falls back to ~/.config");
    free(dir);

    dir = settings_resolve_config_dir("", "/home/u");
    TEST_ASSERT_STR_EQ("/home/u/.config/npad", dir, "empty falls back to ~/.config");
    free(dir);

    dir = settings_resolve_config_dir("relative/conf", "/home/u");
    TEST_ASSERT_STR_EQ("/home/u/.config/npad", dir, "a relative value is ignored (XDG spec)");
    free(dir);

    dir = settings_resolve_config_dir(NULL, NULL);
    TEST_ASSERT_NULL(dir, "no XDG and no home: no directory");

    dir = settings_resolve_legacy_dir("/home/u");
    TEST_ASSERT_STR_EQ("/home/u/.config/npad", dir, "legacy dir ignores XDG_CONFIG_HOME");
    free(dir);
}
#endif

#ifndef _WIN32
static void write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

TEST_CASE(init_reads_old_settings_when_xdg_location_is_empty) {
    // A throwaway HOME and XDG_CONFIG_HOME under the working directory
    mkdir("test_xdg_home", 0755);
    mkdir("test_xdg_home/.config", 0755);
    mkdir("test_xdg_home/.config/npad", 0755);
    mkdir("test_xdg_conf", 0755);
    char cwd[1024];
    TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof cwd), "cwd");
    char home[1100], xdg[1100];
    snprintf(home, sizeof home, "%s/test_xdg_home", cwd);
    snprintf(xdg, sizeof xdg, "%s/test_xdg_conf", cwd);

    const char *old_json = "test_xdg_home/.config/npad/settings.json";
    write_text_file(old_json, "{\n  \"theme\": \"dark\",\n  \"window_width\": \"1234\"\n}\n");

    setenv("HOME", home, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);

    settings_cleanup();
    TEST_ASSERT(settings_init(), "init succeeds");
    char expect_buf[1200];
    const char *expect = expect_buf;
    snprintf(expect_buf, sizeof expect_buf, "%s/npad/settings.json", xdg);
    char expect_dir[1200];
    snprintf(expect_dir, sizeof expect_dir, "%s/npad", xdg);
    TEST_ASSERT_STR_EQ(expect, settings_get_file_path(), "new settings live under XDG_CONFIG_HOME");
    char *theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("dark", theme, "the old ~/.config/npad settings are read");
    free(theme);
    TEST_ASSERT_EQ(1234, settings_get_int("window_width", 0), "all of them");

    // The new directory exists before any settings save, so recovery
    // snapshots (<dir>/recovery, which creates only its own level) can be
    // written from the first edit
    struct stat st;
    TEST_ASSERT(stat(expect_dir, &st) == 0 && S_ISDIR(st.st_mode),
                "the XDG config directory is created at init");
    TEST_ASSERT(!file_exists(expect), "and no settings file is written until a save");

    // Saving writes the NEW location and leaves the old file exactly as it was
    TEST_ASSERT(settings_save(), "save succeeds");
    TEST_ASSERT(file_exists(expect), "the new file is written");
    TEST_ASSERT(file_exists(old_json), "the old file is neither moved nor deleted");

    // With the new file in place it is the one read, even though the old differs
    write_text_file(old_json, "{\n  \"theme\": \"light\"\n}\n");
    settings_set_string("theme", "solarized-dark");
    settings_save();
    settings_cleanup();
    settings_init();
    theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("solarized-dark", theme, "once it exists, the XDG file wins");
    free(theme);

    // XDG unset: back to ~/.config/npad
    settings_cleanup();
    unsetenv("XDG_CONFIG_HOME");
    settings_init();
    snprintf(expect_buf, sizeof expect_buf, "%s/.config/npad/settings.json", home);
    TEST_ASSERT_STR_EQ(expect, settings_get_file_path(), "unset XDG keeps the classic path");
    theme = settings_get_string("theme", "");
    TEST_ASSERT_STR_EQ("light", theme, "and reads the file there");
    free(theme);

    settings_cleanup();
    remove("test_xdg_conf/npad/settings.json");
    remove("test_xdg_conf/npad");
    remove("test_xdg_conf");
    remove(old_json);
    remove("test_xdg_home/.config/npad");
    remove("test_xdg_home/.config");
    remove("test_xdg_home");
}
#endif

// The Win32 Recent Files menu is rebuilt when it opens; an existence check per
// entry there could block for the SMB timeout on a sleeping NAS, so it reads
// the stored list as it is. The filtered call keeps its old behaviour.
TEST_CASE(recent_files_stored_list_skips_the_existence_check) {
    const char *present = "test_recent_present.txt";
    const char *missing = "test_recent_missing_no_such_file.txt";
    FILE *f = fopen(present, "wb");
    TEST_ASSERT_NOT_NULL(f, "setup: create a file");
    fclose(f);
    remove(missing);

    settings_clear_all();
    settings_set_string("recent_file_0", missing);
    settings_set_string("recent_file_1", present);

    int count = 0;
    char **files = settings_get_recent_files_ex(&count, false);
    TEST_ASSERT_EQ(2, count, "both stored entries, reachable or not");
    TEST_ASSERT_STR_EQ(missing, files[0], "in stored order");
    TEST_ASSERT_STR_EQ(present, files[1], "second entry");
    settings_free_recent_files(files, count);

    files = settings_get_recent_files(&count);
    TEST_ASSERT_EQ(1, count, "the filtered list still drops the missing one");
    TEST_ASSERT_STR_EQ(present, files[0], "and keeps the one that exists");
    settings_free_recent_files(files, count);

    settings_set_int("recent_files_max", 1);
    files = settings_get_recent_files_ex(&count, false);
    TEST_ASSERT_EQ(1, count, "the Recent Files limit still applies");
    TEST_ASSERT_STR_EQ(missing, files[0], "to the stored order");
    settings_free_recent_files(files, count);

    settings_clear_all();
    remove(present);
}

int main(void) {
    thread_safety_init();
    TEST_INIT();

    RUN_TEST(reset_clears_preferences);
    RUN_TEST(reset_preserves_kept_categories);
    RUN_TEST(reset_returns_removed_count);
    RUN_TEST(roundtrip_preserves_windows_path);
    RUN_TEST(roundtrip_does_not_grow_value);
    RUN_TEST(roundtrip_preserves_quotes_and_tabs);
    RUN_TEST(oversized_value_is_dropped_on_load);
    RUN_TEST(oversized_file_is_salvaged_not_discarded);
    RUN_TEST(recent_files_stored_list_skips_the_existence_check);
#ifndef _WIN32
    RUN_TEST(config_dir_prefers_absolute_xdg);
    RUN_TEST(init_reads_old_settings_when_xdg_location_is_empty);
#endif

    TEST_SUMMARY();
    thread_safety_cleanup();
    return 0;
}
