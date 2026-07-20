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

int main(void) {
    thread_safety_init();
    TEST_INIT();

    RUN_TEST(reset_clears_preferences);
    RUN_TEST(reset_preserves_kept_categories);
    RUN_TEST(reset_returns_removed_count);

    TEST_SUMMARY();
    thread_safety_cleanup();
    return 0;
}
