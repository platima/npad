/*
 * npad - Settings Tests
 * Guards the "Reset All Preferences" contract: reset removes every preference
 * key by default and preserves only recent files, window geometry, and
 * find/replace state. This is the drift guard - if a future preference is not
 * reset (the historical bug), or the preserved set is widened by mistake, a
 * case here fails.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    TEST_SUMMARY();
    thread_safety_cleanup();
    return 0;
}
