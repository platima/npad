/*
 * npad - Settings Tests
 * Covers the store end to end: the save/load round trip, the merge that lets
 * several windows share one file, the JSON reader against malformed input,
 * the writer's escaping and limits, the typed getters and Recent Files.
 *
 * Also guards the "Reset All Preferences" contract: reset removes every preference
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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

// These call settings_reset_all_preferences, the function both platforms'
// Reset All Preferences buttons call, so the contract tested here is the one
// the app applies - not a copy of its keep-list that could drift from it.

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
    settings_set_bool("find_seed_selection", false);      // General, under the kept find_ prefix

    settings_reset_all_preferences();

    TEST_ASSERT(!settings_has_key("list_tools_enabled"), "Markdown pref reset");
    TEST_ASSERT(!settings_has_key("list_default_indent_format"), "Markdown format pref reset");
    TEST_ASSERT(!settings_has_key("update_mode"), "Updates mode reset");
    TEST_ASSERT(!settings_has_key("update_check_on_launch"), "Updates launch pref reset");
    TEST_ASSERT(!settings_has_key("status_show_counts"), "counts pref reset");
    TEST_ASSERT(!settings_has_key("theme"), "theme reset");
    TEST_ASSERT(!settings_has_key("auto_save_enabled"), "auto-save reset");
    TEST_ASSERT(!settings_has_key("recent_files_max"),
                "recent_files_max is a preference, not a preserved recent_file_ key");
    TEST_ASSERT(!settings_has_key("find_seed_selection"),
                "the find-seeding preference is reset although it starts with find_");
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

    settings_reset_all_preferences();

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
    int removed = settings_reset_all_preferences();
    TEST_ASSERT_EQ(2, removed, "removed count excludes preserved keys");

    settings_set_bool("find_seed_selection", true);
    settings_set_bool("find_match_case", true); // preserved
    TEST_ASSERT_EQ(1, settings_reset_all_preferences(),
                   "an explicitly removed preference is counted too");
    TEST_ASSERT_EQ(0, settings_reset_all_preferences(), "a second reset removes nothing");
    settings_clear_all();
}

TEST_CASE(reset_except_prefixes_primitive) {
    // The primitive underneath: keeps exactly the keys under the given prefixes
    static const char *const keep[] = { "keep_" };
    settings_clear_all();
    settings_set_bool("keep_me", true);
    settings_set_bool("drop_me", true);
    settings_set_bool("kee", true); // shorter than the prefix: not kept
    TEST_ASSERT_EQ(2, settings_reset_except_prefixes(keep, 1), "two removed");
    TEST_ASSERT(settings_has_key("keep_me"), "prefixed key kept");
    TEST_ASSERT_EQ(1, settings_count(), "nothing else left");
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
    // A save writes only when something changed, so change another key each
    // cycle to make every one re-read, re-escape and rewrite the file.
    for (int i = 0; i < 10; i++) {
        settings_set_int("cycle", i);
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

// --- Several windows, one file ---------------------------------------------
//
// npad is one process per window and every window holds the whole store, so
// "another window saved" is simulated here by rewriting the file between this
// instance's steps. A save must apply only what THIS instance changed onto
// what the file holds now, and a reload must keep what it has not saved yet.

#define SHARED_FILE "test_settings_shared.json"

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

// The whole file as a string (malloc'd), or NULL
static char *get_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n > 0 ? (size_t) n + 1 : 1);
    size_t got = buf && n > 0 ? fread(buf, 1, (size_t) n, f) : 0;
    if (buf)
        buf[got] = '\0';
    fclose(f);
    return buf;
}

static void remove_shared_files(void) {
    remove(SHARED_FILE);
    remove(SHARED_FILE ".corrupt");
    remove(SHARED_FILE ".corrupt.1");
    remove(SHARED_FILE ".corrupt.2");
    remove(SHARED_FILE ".lock");
}

// Start this "instance" from a file with the given contents
static void start_instance(const char *contents) {
    settings_clear_all();
    remove_shared_files();
    settings_set_file_path(SHARED_FILE);
    if (contents)
        put_file(SHARED_FILE, contents);
    settings_load();
}

static bool value_is(const char *key, const char *expected) {
    char *got = settings_get_string(key, NULL);
    bool same = got && strcmp(got, expected) == 0;
    free(got);
    return same;
}

TEST_CASE(reload_keeps_unsaved_changes) {
    start_instance("{\"theme\": \"dark\", \"word_wrap\": \"false\"}");
    TEST_ASSERT(!settings_has_unsaved_changes(), "nothing is unsaved straight after a load");

    // This window opens a file and searches with Match case
    settings_set_string("recent_file_0", "/tmp/report.txt");
    settings_set_bool("find_match_case", true);
    settings_set_string("find_hist_0", "TODO");

    // Another window turns Word Wrap on and saves; its broadcast reloads here
    put_file(SHARED_FILE, "{\"theme\": \"dark\", \"word_wrap\": \"true\"}");
    TEST_ASSERT(settings_load(), "the reload works");
    TEST_ASSERT(settings_get_bool("word_wrap", false), "the other window's change arrives");
    TEST_ASSERT(value_is("recent_file_0", "/tmp/report.txt"), "this window's Recent Files survive");
    TEST_ASSERT(settings_get_bool("find_match_case", false), "and its find option");
    TEST_ASSERT(value_is("find_hist_0", "TODO"), "and its find history");
    TEST_ASSERT(settings_has_unsaved_changes(), "still unsaved, so the next save writes them");

    // The exit save keeps both windows' changes
    TEST_ASSERT(settings_save(), "save works");
    TEST_ASSERT(!settings_has_unsaved_changes(), "nothing unsaved after the save");
    settings_clear_all();
    settings_load();
    TEST_ASSERT(settings_get_bool("word_wrap", false), "the file has the other window's change");
    TEST_ASSERT(value_is("recent_file_0", "/tmp/report.txt"), "and this window's");
    TEST_ASSERT(value_is("find_hist_0", "TODO"), "and this window's history");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(exit_save_does_not_overwrite_newer_values) {
    start_instance("{\"a\": \"1\", \"b\": \"1\"}");
    settings_set_string("b", "3"); // This window's only change

    // Meanwhile another window saved a=2 and a new key c
    put_file(SHARED_FILE, "{\"a\": \"2\", \"b\": \"1\", \"c\": \"9\"}");
    TEST_ASSERT(settings_save(), "save works");
    TEST_ASSERT(value_is("a", "2"), "this window adopts what the other saved");

    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("a", "2"), "a value this window only loaded is not written back");
    TEST_ASSERT(value_is("b", "3"), "this window's change is written");
    TEST_ASSERT(value_is("c", "9"), "the other window's new key survives");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(removals_merge_both_ways) {
    start_instance("{\"x\": \"1\", \"y\": \"1\", \"z\": \"1\"}");
    TEST_ASSERT(settings_remove_key("x"), "x removed here");
    TEST_ASSERT(settings_has_unsaved_changes(), "a removal is an unsaved change");

    // Another window removed z and changed y
    put_file(SHARED_FILE, "{\"x\": \"1\", \"y\": \"2\"}");
    TEST_ASSERT(settings_load(), "reload");
    TEST_ASSERT(!settings_has_key("x"), "this window's removal survives the reload");
    TEST_ASSERT(!settings_has_key("z"), "the other window's removal arrives");
    TEST_ASSERT(settings_save(), "save works");

    settings_clear_all();
    settings_load();
    TEST_ASSERT(!settings_has_key("x"), "the removal reaches the file");
    TEST_ASSERT(value_is("y", "2"), "the other window's value stays");
    TEST_ASSERT(!settings_has_key("z"), "and its removal");

    // Reset All Preferences is a removal of everything not kept
    settings_set_string("theme", "dark");
    settings_save();
    settings_reset_all_preferences();
    put_file(SHARED_FILE, "{\"theme\": \"dark\", \"window_x\": \"7\"}");
    settings_save();
    settings_clear_all();
    settings_load();
    TEST_ASSERT(!settings_has_key("theme"), "a reset removes the preference from the file");
    TEST_ASSERT(value_is("window_x", "7"), "and keeps what it preserves");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(setting_the_held_value_is_not_a_change) {
    start_instance("{\"find_match_case\": \"false\"}");
    // Every search re-asserts every option; that must not count as choosing it
    settings_set_bool("find_match_case", false);
    TEST_ASSERT(!settings_has_unsaved_changes(), "setting the value held is not a change");

    put_file(SHARED_FILE, "{\"find_match_case\": \"true\"}");
    TEST_ASSERT(settings_save(), "a save with nothing to add succeeds");
    settings_clear_all();
    settings_load();
    TEST_ASSERT(settings_get_bool("find_match_case", false), "the other window's choice stays");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(save_with_nothing_changed_leaves_file_alone) {
    const char *text = "{\"a\":\"1\"}";
    start_instance(text);
    TEST_ASSERT(settings_save(), "save succeeds");
    char *after = get_file(SHARED_FILE);
    TEST_ASSERT_STR_EQ(text, after, "the file is not rewritten");
    free(after);
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(window_state_is_always_this_windows) {
    start_instance("{\"window_x\": \"5\", \"window_maximized\": \"false\"}");
    // Another window closed elsewhere first
    put_file(SHARED_FILE, "{\"window_x\": \"900\", \"window_maximized\": \"true\"}");
    // This window closes last, where it opened: it decides, as in Notepad
    settings_save_window_state(5, 6, 640, 480, false);
    TEST_ASSERT(settings_save(), "save works");
    settings_clear_all();
    settings_load();
    TEST_ASSERT_EQ(5, settings_get_int("window_x", 0), "the last window closed wins the geometry");
    TEST_ASSERT(!settings_get_bool("window_maximized", true), "including maximized");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(discard_unsaved_changes_adopts_the_file) {
    start_instance("{\"a\": \"1\"}");
    settings_set_string("a", "2");
    settings_remove_key("missing"); // Not held: nothing to record
    settings_discard_unsaved_changes();
    TEST_ASSERT(!settings_has_unsaved_changes(), "nothing unsaved after a discard");
    settings_load();
    TEST_ASSERT(value_is("a", "1"), "the next load takes the file's value");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(reload_of_unreadable_file_keeps_settings) {
    start_instance("{\"theme\": \"dark\", \"word_wrap\": \"true\"}");
    int before = settings_count();

    // Not settings at all (a half-finished hand edit, another program's file)
    put_file(SHARED_FILE, "this is not json");
    TEST_ASSERT(!settings_load(), "load reports the failure");
    TEST_ASSERT_EQ(before, settings_count(), "and keeps every setting held");
    TEST_ASSERT(value_is("theme", "dark"), "values intact");

    // A save then keeps the unreadable original before replacing it
    settings_set_string("theme", "light");
    TEST_ASSERT(settings_save(), "save works");
    char *aside = get_file(SHARED_FILE ".corrupt");
    TEST_ASSERT_STR_EQ("this is not json", aside, "the original is kept as .corrupt");
    free(aside);
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("theme", "light"), "the new file holds the settings");
    TEST_ASSERT(settings_get_bool("word_wrap", false), "all of them");

#ifndef _WIN32
    // A file that exists but cannot be read at that moment (EACCES here; a
    // sharing violation or offline AppData on Windows). Root reads anything,
    // so this half only runs unprivileged.
    if (geteuid() != 0) {
        put_file(SHARED_FILE, "{\"theme\": \"solarized\"}");
        chmod(SHARED_FILE, 0);
        TEST_ASSERT(!settings_load(), "an unreadable file fails the load");
        TEST_ASSERT(value_is("theme", "light"), "and nothing held is lost");
        settings_set_string("theme", "blue");
        TEST_ASSERT(!settings_save(), "a save refuses rather than write blind");
        TEST_ASSERT(settings_has_unsaved_changes(), "and keeps the change for later");
        chmod(SHARED_FILE, 0644);
        char *text = get_file(SHARED_FILE);
        TEST_ASSERT_STR_EQ("{\"theme\": \"solarized\"}", text, "the file is untouched");
        free(text);
        TEST_ASSERT(settings_save(), "once readable, the save goes through");
        settings_clear_all();
        settings_load();
        TEST_ASSERT(value_is("theme", "blue"), "with the kept change");
    }
#endif
    settings_clear_all();
    remove_shared_files();
}

// --- Reader ----------------------------------------------------------------

TEST_CASE(reader_skips_nested_values_and_keeps_them) {
    start_instance("{\n"
                   "  \"future\": {\"x\": {\"deep\": [1, \"}\", \"\\\"]\"]}},\n"
                   "  \"list\": [1, 2, {\"a\": \"]\"}],\n"
                   "  \"theme\": \"dark\",\n"
                   "  \"n\": 42,\n"
                   "  \"flag\": true,\n"
                   "  \"nothing\": null\n"
                   "}\n");
    TEST_ASSERT(value_is("theme", "dark"), "a key after a nested object still loads");
    TEST_ASSERT_EQ(42, settings_get_int("n", 0), "a bare number");
    TEST_ASSERT(settings_get_bool("flag", false), "a bare true");
    TEST_ASSERT(!settings_has_key("nothing"), "null means not set");
    TEST_ASSERT_EQ(5, settings_count(), "no bogus keys from inside the nested values");
    settings_set_int("n", 42);
    settings_set_bool("flag", true);
    TEST_ASSERT(!settings_has_unsaved_changes(), "re-asserting a bare 42 or true is no change");

    // A save writes the nested values back as they were, and the bare
    // scalars keep their JSON types
    settings_set_string("theme", "light");
    TEST_ASSERT(settings_save(), "save works");
    char *text = get_file(SHARED_FILE);
    TEST_ASSERT(text && strstr(text, "\"future\": {\"x\": {\"deep\": [1, \"}\", \"\\\"]\"]}}"),
                "the nested object is kept verbatim");
    TEST_ASSERT(text && strstr(text, "\"list\": [1, 2, {\"a\": \"]\"}]"), "and the array");
    TEST_ASSERT(text && strstr(text, "\"n\": 42"), "a bare number stays a number");
    TEST_ASSERT(text && strstr(text, "\"flag\": true"), "and true stays true");
    free(text);
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("theme", "light"), "and the file still reads");
    TEST_ASSERT_EQ(5, settings_count(), "with the same keys");

    // A value npad sets itself is a string, as always
    settings_set_int("n", 7);
    TEST_ASSERT(settings_save(), "save works");
    text = get_file(SHARED_FILE);
    TEST_ASSERT(text && strstr(text, "\"n\": \"7\""), "a changed number is written as npad writes it");
    free(text);

    // Bare text that is not valid JSON is not written back bare
    start_instance("{\"a\": tru, \"b\": 01, \"c\": -1.5e3, \"d\": 1.}");
    settings_set_string("probe", "1");
    TEST_ASSERT(settings_save(), "save works");
    text = get_file(SHARED_FILE);
    TEST_ASSERT(text && strstr(text, "\"a\": \"tru\""), "a typo is quoted, keeping the file JSON");
    TEST_ASSERT(text && strstr(text, "\"b\": \"01\""), "so is a number with a leading zero");
    TEST_ASSERT(text && strstr(text, "\"c\": -1.5e3"), "a valid exponent form stays bare");
    TEST_ASSERT(text && strstr(text, "\"d\": \"1.\""), "a number missing its fraction is quoted");
    free(text);
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(reader_decodes_every_json_escape) {
    // As Python's json.dump writes it (non-ASCII escaped)
    start_instance("{\"font_face\": \"Caf\\u00e9 Sans\", \"emoji\": \"\\ud83d\\ude00\","
                   " \"ctl\": \"a\\bb\\fc\\/d\", \"lone\": \"\\ud800x\", \"nul\": \"a\\u0000b\","
                   " \"k\\u00e9y\": \"v\"}");
    TEST_ASSERT(value_is("font_face", "Caf\xC3\xA9 Sans"), "\\u00e9 decodes to UTF-8");
    TEST_ASSERT(value_is("emoji", "\xF0\x9F\x98\x80"), "a surrogate pair decodes to one character");
    TEST_ASSERT(value_is("ctl", "a\bb\fc/d"), "\\b, \\f and \\/");
    TEST_ASSERT(value_is("lone", "\xEF\xBF\xBD"
                                 "x"),
                "a lone surrogate becomes U+FFFD");
    TEST_ASSERT(value_is("nul", "a\xEF\xBF\xBD"
                                "b"),
                "so does a NUL, which a C string cannot hold");
    TEST_ASSERT(value_is("k\xC3\xA9y", "v"), "keys are decoded too");

    // And they stay that way: the written file is read back to the same text
    settings_set_string("cycle", "1");
    settings_save();
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("font_face", "Caf\xC3\xA9 Sans"), "the decoded value survives a save");
    TEST_ASSERT(value_is("emoji", "\xF0\x9F\x98\x80"), "and so does the emoji");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(key_with_backslash_and_quote_does_not_grow) {
    start_instance(NULL);
    settings_set_string("font\\face", "v");
    settings_set_string("q\"k", "w");
    for (int i = 0; i < 20; i++) {
        settings_set_int("cycle", i); // Force a rewrite every cycle
        roundtrip();
    }
    TEST_ASSERT(value_is("font\\face", "v"), "a backslash in a key round trips");
    TEST_ASSERT(value_is("q\"k", "w"), "so does a quote");
    TEST_ASSERT_EQ(3, settings_count(), "and no doubled variant appears beside it");
    char *text = get_file(SHARED_FILE);
    TEST_ASSERT(text && strlen(text) < 200, "the file stays small after 20 saves");
    free(text);
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(reader_survives_malformed_input) {
    static char deep[200016];
    memcpy(deep, "{\"a\": ", 6);
    memset(deep + 6, '[', 200000);
    deep[200006] = '\0';

    const char *inputs[] = {
        "",
        "   \n",
        "{",
        "}",
        "[1, 2]",
        "{\"a\"",
        "{\"a\":",
        "{\"a\": \"x",
        "{\"a\": \"x\\",
        "{\"a\": tru",
        "{\"a\": {\"b\": [1, 2}",
        "{\"a\": \"\\u12\"}",
        "{\"a\": \"\\ud83d\"}",
        "{\"a\": \"\\ud83d\\u0041\"}",
        "{,,,\"a\": \"1\",,}",
        "{\"a\" \"1\"}",
        "{\"a\": \"1\" \"b\": \"2\"}",
        "{\"a\": ]}",
        "{\"a\": }",
        "{1: 2}",
        "\xEF\xBB\xBF{\"a\": \"1\"}",
        "{\"a\": \"1\"} trailing text",
        "{\"\": \"empty key\"}",
        "{\"a\": \"raw\ttab and\nnewline\"}",
        deep,
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        start_instance(inputs[i]);
        settings_set_string("probe", "1");
        settings_save(); // Must neither crash nor lose the probe
        settings_clear_all();
        settings_load();
        TEST_ASSERT(value_is("probe", "1"), "a save after malformed input still writes");
    }

    // Salvage: what came before the damage is kept
    start_instance("{\"a\": \"1\", \"b\": \"2\", \"c\": ");
    TEST_ASSERT(value_is("a", "1") && value_is("b", "2"), "keys before the damage load");
    start_instance("\xEF\xBB\xBF{\"a\": \"1\"}");
    TEST_ASSERT(value_is("a", "1"), "a BOM is skipped");
    start_instance("{\"a\": \"1\" \"b\": \"2\"}");
    TEST_ASSERT(value_is("b", "2"), "a missing comma is forgiven");
    start_instance("{\"a\": \"raw\ttab\"}");
    TEST_ASSERT(value_is("a", "raw\ttab"), "a raw control character an older npad wrote reads");

    // Random bytes from JSON's alphabet: nothing may crash or hang
    unsigned long seed = 12345;
    const char alphabet[] = "{}[]\":,\\ un0aZ\n\t9-.e";
    char buf[96];
    for (int iter = 0; iter < 3000; iter++) {
        size_t len = 0;
        if (iter % 2 == 0)
            buf[len++] = '{';
        size_t target = 1 + (size_t) (iter % 90);
        while (len < target) {
            seed = seed * 1103515245UL + 12345UL;
            buf[len++] = alphabet[(seed >> 16) % (sizeof(alphabet) - 1)];
        }
        buf[len] = '\0';
        start_instance(buf);
        settings_count();
    }
    TEST_ASSERT(true, "3000 random inputs parsed without a crash");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(many_keys_are_capped) {
    // A corrupt or crafted file of 20,000 keys: loads in linear time (the old
    // list made every insert a full scan) and stops at the cap
    size_t cap = 20000 * 32 + 16;
    char *text = malloc(cap);
    TEST_ASSERT_NOT_NULL(text, "buffer");
    if (!text)
        return;
    size_t n = 0;
    n += (size_t) snprintf(text + n, cap - n, "{");
    for (int i = 0; i < 20000; i++)
        n += (size_t) snprintf(text + n, cap - n, "%s\"k%d\": \"v\"", i ? ", " : "", i);
    snprintf(text + n, cap - n, "}");
    start_instance(text);
    free(text);
    TEST_ASSERT_EQ(10000, settings_count(), "loading stops at 10,000 keys");
    TEST_ASSERT(value_is("k9999", "v"), "keys up to the cap are there");
    TEST_ASSERT(!settings_has_key("k10000"), "later ones are not");
    TEST_ASSERT(!settings_set_string("one_more", "x"), "and no new key is added past the cap");
    settings_set_string("k0", "changed");
    TEST_ASSERT(settings_save(), "a save still works");
    TEST_ASSERT(file_exists(SHARED_FILE ".corrupt"), "keeping the full original aside");
    settings_clear_all();
    remove_shared_files();
}

// --- Writer ----------------------------------------------------------------

TEST_CASE(writer_escapes_control_characters) {
    start_instance(NULL);
    const char *term = "a\tb\nc\x01"
                       "d\x1f"
                       "e\r";
    settings_set_string("find_hist_0", term);
    TEST_ASSERT(settings_save(), "save works");
    char *text = get_file(SHARED_FILE);
    bool clean = text != NULL;
    for (const char *p = text; p && *p; p++) {
        if ((unsigned char) *p < 0x20 && *p != '\n')
            clean = false;
    }
    TEST_ASSERT(clean, "no raw control character in the file (valid JSON for jq and Python)");
    TEST_ASSERT(text && strstr(text, "a\\tb\\nc\\u0001d\\u001fe\\r"), "each one is escaped");
    free(text);
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("find_hist_0", term), "and reads back exactly");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(writer_refuses_what_the_reader_would_drop) {
    start_instance(NULL);
    char *big = malloc(65538);
    TEST_ASSERT_NOT_NULL(big, "buffer");
    if (!big)
        return;
    memset(big, 'x', 65537);
    big[65537] = '\0';
    TEST_ASSERT(!settings_set_string("big", big), "a value over 64 KB is refused");
    TEST_ASSERT(!settings_has_key("big"), "and not stored");
    big[65536] = '\0';
    TEST_ASSERT(settings_set_string("big", big), "64 KB exactly is fine");
    roundtrip();
    TEST_ASSERT(settings_has_key("big"), "and survives a round trip");
    free(big);

    char key[300];
    memset(key, 'k', 257);
    key[257] = '\0';
    TEST_ASSERT(!settings_set_string(key, "v"), "a key over 256 bytes is refused");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(entry_order_is_stable) {
    start_instance(NULL);
    settings_set_string("order_a", "1");
    settings_set_string("order_b", "2");
    settings_set_string("order_c", "3");
    for (int i = 0; i < 3; i++) {
        settings_set_int("cycle", i);
        roundtrip();
    }
    char *text = get_file(SHARED_FILE);
    const char *a = text ? strstr(text, "order_a") : NULL;
    const char *b = text ? strstr(text, "order_b") : NULL;
    const char *c = text ? strstr(text, "order_c") : NULL;
    TEST_ASSERT(a && b && c && a < b && b < c, "entries keep their order across saves");
    free(text);
    settings_clear_all();
    remove_shared_files();
}

// --- Typed getters ---------------------------------------------------------

TEST_CASE(typed_getters_fall_back_on_malformed_text) {
    settings_clear_all();
    settings_set_string("recent_files_max", "ten");
    TEST_ASSERT_EQ(10, settings_get_int("recent_files_max", 10), "\"ten\" reads as the default");
    settings_set_string("large_file_warning_mb", "");
    TEST_ASSERT_EQ(50, settings_get_int("large_file_warning_mb", 50), "\"\" reads as the default");
    settings_set_string("i", " 42 ");
    TEST_ASSERT_EQ(42, settings_get_int("i", 0), "spaces around a number are fine");
    settings_set_string("i", "-5");
    TEST_ASSERT_EQ(-5, settings_get_int("i", 0), "negative numbers read");
    settings_set_string("i", "12abc");
    TEST_ASSERT_EQ(7, settings_get_int("i", 7), "trailing junk reads as the default");
    settings_set_string("i", "99999999999");
    TEST_ASSERT_EQ(7, settings_get_int("i", 7), "out of int range reads as the default");

    settings_set_string("b", "yes");
    TEST_ASSERT(settings_get_bool("b", true), "an unknown bool keeps a true default");
    TEST_ASSERT(!settings_get_bool("b", false), "and a false one");
    settings_set_string("b", "TRUE");
    TEST_ASSERT(settings_get_bool("b", false), "TRUE in any case is true");
    settings_set_string("b", "False");
    TEST_ASSERT(!settings_get_bool("b", true), "False in any case is false");
    settings_set_string("b", "0");
    TEST_ASSERT(!settings_get_bool("b", true), "0 is false");

    settings_set_string("d", "1.5");
    TEST_ASSERT(settings_get_double("d", 0.0) == 1.5, "a double reads");
    settings_set_string("d", "abc");
    TEST_ASSERT(settings_get_double("d", 2.0) == 2.0, "junk reads as the default");
    settings_set_string("d", "nan");
    TEST_ASSERT(settings_get_double("d", 2.0) == 2.0, "so does a non-number");

    // GTK runs in the user's locale; a German one prints 1,5. The stored text
    // must still be 1.5, and still read back.
    if (setlocale(LC_NUMERIC, "de_DE.UTF-8") || setlocale(LC_NUMERIC, "de_DE")) {
        settings_set_double("d", 1.5);
        TEST_ASSERT(value_is("d", "1.500000"), "stored with a '.' in a ',' locale");
        TEST_ASSERT(settings_get_double("d", 0.0) == 1.5, "and read back there");
        setlocale(LC_NUMERIC, "C");
    }
    settings_set_double("d", 2.25);
    TEST_ASSERT(settings_get_double("d", 0.0) == 2.25, "set/get double round trip");
    settings_clear_all();
}

// --- Recent files ----------------------------------------------------------

TEST_CASE(recent_files_keep_unreachable_entries) {
    start_instance(NULL);
    put_file("test_recent_exists.txt", "x");
    const char *gone = "/nonexistent-npad-test-dir/on-a-sleeping-nas.txt";
    settings_set_string("recent_file_0", gone);

    TEST_ASSERT(settings_add_recent_file("test_recent_exists.txt"), "add works");
    TEST_ASSERT(value_is("recent_file_0", "test_recent_exists.txt"), "the new file is first");
    TEST_ASSERT(value_is("recent_file_1", gone), "the unreachable entry is kept, not dropped");

    int count = 0;
    char **files = settings_get_recent_files(&count);
    TEST_ASSERT_EQ(1, count, "the menu list leaves the unreachable one out");
    TEST_ASSERT(files && strcmp(files[0], "test_recent_exists.txt") == 0, "and shows the other");
    settings_free_recent_files(files, count);

    remove("test_recent_exists.txt");
    settings_clear_all();
    remove_shared_files();
}

// The keys of a "<prefix>_N" list, in order, joined with spaces; positions
// that are missing show as "-" up to the last one present
static void list_text(const char *prefix, char *out, size_t cap) {
    out[0] = '\0';
    size_t len = 0;
    int last = -1;
    for (int i = 0; i < 12; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        if (settings_has_key(key))
            last = i;
    }
    for (int i = 0; i <= last; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        char *v = settings_get_string(key, "-");
        len += (size_t) snprintf(out + len, cap - len, "%s%s", i ? " " : "", v);
        free(v);
        if (len >= cap)
            break;
    }
}

TEST_CASE(recent_files_are_saved_as_a_whole_list) {
    char got[256];

    // Window A has [a,b,c] and reopens b: [b,a,c], position 2 unchanged
    start_instance("{\"recent_file_0\": \"a\", \"recent_file_1\": \"b\", \"recent_file_2\": \"c\"}");
    TEST_ASSERT(settings_add_recent_file("b"), "add works");
    // Meanwhile window B saved [y,a,b,c]
    put_file(SHARED_FILE, "{\"recent_file_0\": \"y\", \"recent_file_1\": \"a\","
                          " \"recent_file_2\": \"b\", \"recent_file_3\": \"c\"}");
    TEST_ASSERT(settings_save(), "A's save works");
    settings_clear_all();
    settings_load();
    list_text("recent_file", got, sizeof(got));
    TEST_ASSERT_STR_EQ("b a c", got, "A's list replaces B's whole, with no file listed twice");

    // Clear Recent Files in A, holding [a], while B has saved [y,a]
    start_instance("{\"recent_file_0\": \"a\"}");
    put_file(SHARED_FILE, "{\"recent_file_0\": \"y\", \"recent_file_1\": \"a\", \"theme\": \"x\"}");
    TEST_ASSERT(settings_clear_recent_files(), "clear works");
    TEST_ASSERT(settings_save(), "save works");
    settings_clear_all();
    settings_load();
    list_text("recent_file", got, sizeof(got));
    TEST_ASSERT_STR_EQ("", got, "every entry is cleared, also those only B had");
    TEST_ASSERT(value_is("theme", "x"), "and nothing else is touched");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(history_list_has_no_gap_after_a_merge) {
    char got[256];
    char *mine[] = { "t0", "t1" };

    // A held [t1], B saved [u0,u1,u2]; A searches t0, writing [t0,t1]
    start_instance("{\"find_hist_0\": \"t1\"}");
    put_file(SHARED_FILE, "{\"find_hist_0\": \"u0\", \"find_hist_1\": \"u1\","
                          " \"find_hist_2\": \"u2\"}");
    TEST_ASSERT(settings_set_list("find_hist", mine, 2, 10), "set_list works");
    TEST_ASSERT(settings_save(), "save works");
    settings_clear_all();
    settings_load();
    list_text("find_hist", got, sizeof(got));
    TEST_ASSERT_STR_EQ("t0 t1", got, "the history is A's, whole, with no tail of B's");

    // Rewriting the list as it is still claims every position
    start_instance("{\"find_hist_0\": \"t0\", \"find_hist_1\": \"t1\"}");
    TEST_ASSERT(settings_set_list("find_hist", mine, 2, 10), "set_list works");
    TEST_ASSERT(settings_has_unsaved_changes(), "an unchanged list written whole is a change");

    // Bad arguments change nothing
    settings_discard_unsaved_changes();
    TEST_ASSERT(!settings_set_list(NULL, mine, 2, 10), "no prefix is refused");
    TEST_ASSERT(!settings_set_list("find_hist", mine, 3, 2), "count past capacity is refused");
    TEST_ASSERT(!settings_has_unsaved_changes(), "and leaves nothing unsaved");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(discard_one_unsaved_change_keeps_the_rest) {
    // A failed Preferences Apply undoes only what it wrote
    start_instance("{\"theme\": \"dark\"}");
    settings_set_string("theme", "light");      // The Apply that failed to save
    settings_set_string("added_by_apply", "1"); // A key the file does not have
    settings_add_recent_file("/tmp/opened.txt"); // Unrelated: must survive
    settings_discard_unsaved_change("theme");
    settings_discard_unsaved_change("added_by_apply");
    TEST_ASSERT(settings_has_unsaved_changes(), "the Recent Files change is still unsaved");
    TEST_ASSERT(settings_load(), "reload");
    TEST_ASSERT(value_is("theme", "dark"), "the reverted key takes the file's value");
    TEST_ASSERT(!settings_has_key("added_by_apply"), "a key the file lacks is dropped");
    TEST_ASSERT(value_is("recent_file_0", "/tmp/opened.txt"), "the other change is kept");
    TEST_ASSERT(settings_save(), "save works");
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("recent_file_0", "/tmp/opened.txt"), "and reaches the file");
    TEST_ASSERT(value_is("theme", "dark"), "without the reverted value");
    settings_clear_all();
    remove_shared_files();
}

// A file that grew past the load cap after this window loaded it (an older
// npad still running, say) is moved aside, not read whole to copy it
TEST_CASE(file_grown_past_the_cap_before_a_save_is_moved_aside) {
    start_instance("{\"a\": \"1\"}");
    FILE *f = fopen(SHARED_FILE, "wb");
    TEST_ASSERT_NOT_NULL(f, "oversized file created");
    if (f) {
        fputs("{\n  \"theme\": \"dark\",\n  \"runaway\": \"", f);
        for (long i = 0; i < 9L * 1024 * 1024; i++)
            fputc('x', f);
        fputs("\"\n}\n", f);
        fclose(f);
    }
    size_t big = file_get_size(SHARED_FILE);
    settings_set_string("b", "2");
    TEST_ASSERT(settings_save(), "save works");
    TEST_ASSERT_EQ((int) big, (int) file_get_size(SHARED_FILE ".corrupt"),
                   "the whole original is kept aside, byte for byte");
    TEST_ASSERT(file_get_size(SHARED_FILE) < 4096, "settings.json is small again");
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("theme", "dark"), "what came before the runaway value is salvaged");
    TEST_ASSERT(value_is("b", "2"), "and this window's change is saved");
    TEST_ASSERT(!settings_has_key("runaway"), "the runaway value is not");
    settings_clear_all();
    remove_shared_files();
}

TEST_CASE(an_earlier_corrupt_copy_is_never_overwritten) {
    put_file(SHARED_FILE ".corrupt", "earlier");
    // start_instance removes the shared files, so set up by hand
    settings_clear_all();
    settings_set_file_path(SHARED_FILE);
    put_file(SHARED_FILE, "{\"a\": \"1\", \"b\": ");
    settings_load();
    settings_set_string("probe", "1");
    TEST_ASSERT(settings_save(), "save works");
    char *first = get_file(SHARED_FILE ".corrupt");
    char *second = get_file(SHARED_FILE ".corrupt.1");
    TEST_ASSERT_STR_EQ("earlier", first, "the earlier copy is untouched");
    TEST_ASSERT_STR_EQ("{\"a\": \"1\", \"b\": ", second, "the new one goes to a free name");
    free(first);
    free(second);

    // On load too: an oversized file moved aside takes the next free name
    FILE *f = fopen(SHARED_FILE, "wb");
    if (f) {
        fputs("{\"theme\": \"dark\", \"runaway\": \"", f);
        for (long i = 0; i < 9L * 1024 * 1024; i++)
            fputc('x', f);
        fclose(f);
    }
    settings_clear_all();
    settings_load();
    TEST_ASSERT(value_is("theme", "dark"), "the oversized file is salvaged");
    TEST_ASSERT(file_get_size(SHARED_FILE ".corrupt.2") > 8u * 1024 * 1024,
                "and moved to the next free name");
    first = get_file(SHARED_FILE ".corrupt");
    TEST_ASSERT_STR_EQ("earlier", first, "still without touching the earlier copy");
    free(first);
    settings_clear_all();
    remove_shared_files();
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
static char g_saved_home[2048], g_saved_xdg[2048];

static void write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

TEST_CASE(init_reads_old_settings_when_xdg_location_is_empty) {
    // This case is about HOME/XDG, so the test-hook override must be off
    unsetenv("NPAD_TEST_CONFIG_DIR");
    // A throwaway HOME and XDG_CONFIG_HOME under the suite's private working
    // directory. Anything an earlier, aborted pass left is cleared first: a
    // stale test_xdg_conf/npad/settings.json would otherwise be read in place
    // of the legacy file and fail every assertion below.
    test_remove_tree("test_xdg_home");
    test_remove_tree("test_xdg_conf");
    TEST_ASSERT(test_make_dir("test_xdg_home") && test_make_dir("test_xdg_home/.config") &&
                    test_make_dir("test_xdg_home/.config/npad") && test_make_dir("test_xdg_conf"),
                "fixture directories created");
    char cwd[1024];
    TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof cwd), "cwd");
    char home[1100], xdg[1100];
    snprintf(home, sizeof home, "%s/test_xdg_home", cwd);
    snprintf(xdg, sizeof xdg, "%s/test_xdg_conf", cwd);

    const char *old_json = "test_xdg_home/.config/npad/settings.json";
    write_text_file(old_json, "{\n  \"theme\": \"dark\",\n  \"window_width\": \"1234\"\n}\n");

    // The framework's isolated HOME / XDG_CONFIG_HOME, put back at the end
    snprintf(g_saved_home, sizeof g_saved_home, "%s", getenv("HOME") ? getenv("HOME") : "");
    snprintf(g_saved_xdg, sizeof g_saved_xdg, "%s",
             getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "");
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
    test_remove_tree("test_xdg_conf");
    test_remove_tree("test_xdg_home");
    setenv("HOME", g_saved_home, 1);
    setenv("XDG_CONFIG_HOME", g_saved_xdg, 1);
}

#ifdef NPAD_TEST_HOOKS
// The test-hook contract: $NPAD_TEST_CONFIG_DIR decides where settings live,
// ahead of XDG_CONFIG_HOME and HOME, and the real ~/.config/npad is never
// read, not even by the migration that reads it when the new place is empty
TEST_CASE(test_config_dir_overrides_home_and_xdg) {
    mkdir("test_hook_home", 0755);
    mkdir("test_hook_home/.config", 0755);
    mkdir("test_hook_home/.config/npad", 0755);
    char cwd[1024];
    TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof cwd), "cwd");
    char home[1100], xdg[1100], conf[1100];
    snprintf(home, sizeof home, "%s/test_hook_home", cwd);
    snprintf(xdg, sizeof xdg, "%s/test_hook_xdg", cwd);
    // Not created: settings_init must make it (and its parent) itself
    snprintf(conf, sizeof conf, "%s/test_hook_conf/npad", cwd);

    const char *legacy = "test_hook_home/.config/npad/settings.json";
    write_text_file(legacy, "{\n  \"theme\": \"dark\"\n}\n");

    setenv("HOME", home, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);
    setenv("NPAD_TEST_CONFIG_DIR", conf, 1);

    settings_cleanup();
    TEST_ASSERT(settings_init(), "init succeeds");
    char expect[1200];
    snprintf(expect, sizeof expect, "%s/settings.json", conf);
    TEST_ASSERT_STR_EQ(expect, settings_get_file_path(),
                       "settings live under NPAD_TEST_CONFIG_DIR, not XDG or HOME");
    char *theme = settings_get_string("theme", "none");
    TEST_ASSERT_STR_EQ("none", theme, "the HOME settings are not read, even to migrate them");
    free(theme);

    struct stat st;
    TEST_ASSERT(stat(conf, &st) == 0 && S_ISDIR(st.st_mode),
                "the directory is created at init, parents included");
    settings_set_string("theme", "light");
    TEST_ASSERT(settings_save(), "save succeeds");
    TEST_ASSERT(file_exists(expect), "and writes there");
    TEST_ASSERT(stat(xdg, &st) != 0, "nothing is created under XDG_CONFIG_HOME");

    // An empty value is the same as unset
    setenv("NPAD_TEST_CONFIG_DIR", "", 1);
    settings_cleanup();
    settings_init();
    snprintf(expect, sizeof expect, "%s/npad/settings.json", xdg);
    TEST_ASSERT_STR_EQ(expect, settings_get_file_path(), "empty: back to XDG_CONFIG_HOME");

    settings_cleanup();
    unsetenv("NPAD_TEST_CONFIG_DIR");
    snprintf(expect, sizeof expect, "%s/settings.json", conf);
    remove(expect);
    remove(conf);
    remove("test_hook_conf");
    remove("test_hook_xdg/npad");
    remove("test_hook_xdg");
    remove(legacy);
    remove("test_hook_home/.config/npad");
    remove("test_hook_home/.config");
    remove("test_hook_home");
}
#endif
#endif

int main(void) {
    thread_safety_init();
    TEST_INIT();

    RUN_TEST(reset_clears_preferences);
    RUN_TEST(reset_preserves_kept_categories);
    RUN_TEST(reset_returns_removed_count);
    RUN_TEST(reset_except_prefixes_primitive);
    RUN_TEST(roundtrip_preserves_windows_path);
    RUN_TEST(roundtrip_does_not_grow_value);
    RUN_TEST(roundtrip_preserves_quotes_and_tabs);
    RUN_TEST(oversized_value_is_dropped_on_load);
    RUN_TEST(oversized_file_is_salvaged_not_discarded);
    RUN_TEST(reload_keeps_unsaved_changes);
    RUN_TEST(exit_save_does_not_overwrite_newer_values);
    RUN_TEST(removals_merge_both_ways);
    RUN_TEST(setting_the_held_value_is_not_a_change);
    RUN_TEST(save_with_nothing_changed_leaves_file_alone);
    RUN_TEST(window_state_is_always_this_windows);
    RUN_TEST(discard_unsaved_changes_adopts_the_file);
    RUN_TEST(reload_of_unreadable_file_keeps_settings);
    RUN_TEST(reader_skips_nested_values_and_keeps_them);
    RUN_TEST(reader_decodes_every_json_escape);
    RUN_TEST(key_with_backslash_and_quote_does_not_grow);
    RUN_TEST(reader_survives_malformed_input);
    RUN_TEST(many_keys_are_capped);
    RUN_TEST(writer_escapes_control_characters);
    RUN_TEST(writer_refuses_what_the_reader_would_drop);
    RUN_TEST(entry_order_is_stable);
    RUN_TEST(typed_getters_fall_back_on_malformed_text);
    RUN_TEST(recent_files_keep_unreachable_entries);
    RUN_TEST(recent_files_are_saved_as_a_whole_list);
    RUN_TEST(history_list_has_no_gap_after_a_merge);
    RUN_TEST(discard_one_unsaved_change_keeps_the_rest);
    RUN_TEST(file_grown_past_the_cap_before_a_save_is_moved_aside);
    RUN_TEST(an_earlier_corrupt_copy_is_never_overwritten);
    remove(ROUNDTRIP_FILE ".lock"); // The save lock the round-trip tests left
#ifndef _WIN32
    RUN_TEST(config_dir_prefers_absolute_xdg);
    RUN_TEST(init_reads_old_settings_when_xdg_location_is_empty);
#ifdef NPAD_TEST_HOOKS
    RUN_TEST(test_config_dir_overrides_home_and_xdg);
#endif
#endif

    settings_cleanup();
    thread_safety_cleanup();
    TEST_SUMMARY();
    return 0;
}
