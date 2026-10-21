/*
 * npad - Reload from disk tests
 * The editor core's "Reload from disk" (the file changed underneath npad)
 * against stubbed ui_* functions: it must ask Open's binary and large-file
 * questions before reading, report a read failure instead of failing
 * silently, and leave the document alone whenever it does not reload.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/editor.h"
#include "../src/core/settings.h"
#include <stdio.h>
#include <string.h>

#define RELOAD_FILE "test_editor_reload_tmp.txt"

// --- UI stubs -------------------------------------------------------------
// One fake window; the core only passes the pointer back to these stubs.

static int g_fake_window_storage;
#define FAKE_WINDOW ((Window *) (void *) &g_fake_window_storage)

static char g_text[256];         // What the "control" shows
static int g_set_text_calls;     // ui_set_text calls
static int g_message_boxes;      // ui_show_message_box calls
static char g_last_message[512]; // Its text
static bool g_message_answer;    // What a question box answers
static int g_binary_prompts;     // ui_prompt_binary_open calls
static UiOpenChoice g_binary_answer;
static int g_default_app_opens; // ui_open_with_default_app calls

void ui_set_text(Window *window, const char *text) {
    (void) window;
    g_set_text_calls++;
    snprintf(g_text, sizeof(g_text), "%s", text ? text : "");
}

char *ui_get_text(Window *window) {
    (void) window;
    char *copy = malloc(strlen(g_text) + 1);
    if (copy)
        strcpy(copy, g_text);
    return copy;
}

void ui_clear_text(Window *window) {
    ui_set_text(window, "");
}

bool ui_show_message_box(Window *parent, const char *title, const char *message,
                         bool is_question) {
    (void) parent;
    (void) title;
    g_message_boxes++;
    snprintf(g_last_message, sizeof(g_last_message), "%s", message ? message : "");
    return is_question ? g_message_answer : true;
}

UiOpenChoice ui_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    g_binary_prompts++;
    return g_binary_answer;
}

void ui_open_with_default_app(const char *filename) {
    (void) filename;
    g_default_app_opens++;
}

SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_SAVE_PROMPT_DISCARD;
}

char *ui_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}

char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}

size_t ui_system_memory_mb(void) {
    return 0;
}

bool ui_pid_is_running(long pid) {
    (void) pid;
    return false;
}

bool ui_is_dark_mode(void) {
    return false;
}

void ui_set_dark_mode(bool enabled) {
    (void) enabled;
}

void ui_set_event_handler(UIEventHandler handler) {
    (void) handler;
}

void ui_set_window_title(Window *window, const char *title) {
    (void) window;
    (void) title;
}

void ui_set_text_modified(Window *window, bool modified) {
    (void) window;
    (void) modified;
}

void ui_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    (void) window;
    (void) encoding_name;
    (void) eol_name;
}

void ui_set_auto_save_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}

void ui_set_session_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}

void ui_launch_recovery_instance(const char *slot_id, int cascade_index) {
    (void) slot_id;
    (void) cascade_index;
}

void ui_quit(void) {
}

void ui_show_find_dialog(Window *parent) {
    (void) parent;
}

void ui_show_replace_dialog(Window *parent) {
    (void) parent;
}

void ui_undo(Window *window) {
    (void) window;
}

void ui_redo(Window *window) {
    (void) window;
}

void ui_cut(Window *window) {
    (void) window;
}

void ui_copy(Window *window) {
    (void) window;
}

void ui_paste(Window *window) {
    (void) window;
}

void ui_select_all(Window *window) {
    (void) window;
}

// --- Helpers --------------------------------------------------------------

static bool write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static void reset_counters(void) {
    g_set_text_calls = 0;
    g_message_boxes = 0;
    g_last_message[0] = '\0';
    g_message_answer = true;
    g_binary_prompts = 0;
    g_binary_answer = UI_OPEN_IN_NPAD;
    g_default_app_opens = 0;
}

// A fresh editor with RELOAD_FILE open, showing "original"
static bool open_original(void) {
    settings_clear_all();
    editor_init();
    editor_set_main_window(FAKE_WINDOW);
    if (!write_file(RELOAD_FILE, "original", 8))
        return false;
    reset_counters();
    bool ok = editor_open_file(RELOAD_FILE);
    reset_counters();
    return ok && strcmp(g_text, "original") == 0;
}

static void finish(void) {
    editor_cleanup();
    settings_clear_all();
    remove(RELOAD_FILE);
}

// --- Cases ----------------------------------------------------------------

TEST_CASE(reload_reads_the_new_contents) {
    TEST_ASSERT(open_original(), "setup: the file opened");
    TEST_ASSERT(write_file(RELOAD_FILE, "changed", 7), "setup: rewrite");
    TEST_ASSERT(editor_reload_from_disk(), "reload succeeds");
    TEST_ASSERT_STR_EQ("changed", g_text, "the new contents are shown");
    TEST_ASSERT_EQ(0, g_message_boxes, "no message for a plain reload");
    TEST_ASSERT(!editor_is_modified(), "identical to the file again");
    finish();
}

TEST_CASE(read_failure_is_reported_and_keeps_the_document) {
    TEST_ASSERT(open_original(), "setup: the file opened");
    remove(RELOAD_FILE); // Unreadable now
    TEST_ASSERT(!editor_reload_from_disk(), "reload fails");
    TEST_ASSERT_EQ(1, g_message_boxes, "the failure is reported, not silent");
    TEST_ASSERT(strstr(g_last_message, "Could not reload") != NULL, "and says what failed");
    TEST_ASSERT(strstr(g_last_message, RELOAD_FILE) != NULL, "naming the file");
    TEST_ASSERT_EQ(0, g_set_text_calls, "the document is untouched");
    TEST_ASSERT_STR_EQ("original", g_text, "still the original text");
    finish();
}

TEST_CASE(binary_replacement_asks_first) {
    static const unsigned char binary[] = { 'P', 'N', 'G', 0x00, 0x01, 0x02, 'x' };

    TEST_ASSERT(open_original(), "setup: the file opened");
    TEST_ASSERT(write_file(RELOAD_FILE, binary, sizeof(binary)), "setup: binary rewrite");

    g_binary_answer = UI_OPEN_CANCEL;
    TEST_ASSERT(!editor_reload_from_disk(), "Cancel declines the reload");
    TEST_ASSERT_EQ(1, g_binary_prompts, "the binary question was asked");
    TEST_ASSERT_EQ(0, g_set_text_calls, "nothing was loaded");

    reset_counters();
    g_binary_answer = UI_OPEN_WITH_DEFAULT;
    TEST_ASSERT(!editor_reload_from_disk(), "the default app takes it instead");
    TEST_ASSERT_EQ(1, g_default_app_opens, "handed to the default app");
    TEST_ASSERT_EQ(0, g_set_text_calls, "and npad's document is untouched");
    TEST_ASSERT_STR_EQ("original", g_text, "still the original text");

    reset_counters();
    g_binary_answer = UI_OPEN_IN_NPAD;
    TEST_ASSERT(editor_reload_from_disk(), "Open in npad reloads it");
    TEST_ASSERT_EQ(1, g_set_text_calls, "loaded once");
    finish();
}

TEST_CASE(large_replacement_asks_first) {
    TEST_ASSERT(open_original(), "setup: the file opened");
    settings_set_int("large_file_warning_mb", 1);
    size_t size = 2u * 1024u * 1024u;
    char *big = malloc(size);
    TEST_ASSERT_NOT_NULL(big, "setup: allocation");
    memset(big, 'a', size);
    bool written = write_file(RELOAD_FILE, big, size);
    free(big);
    TEST_ASSERT(written, "setup: 2 MB rewrite");

    g_message_answer = false;
    TEST_ASSERT(!editor_reload_from_disk(), "No declines the reload");
    TEST_ASSERT_EQ(1, g_message_boxes, "the size question was asked");
    TEST_ASSERT(strstr(g_last_message, "MB") != NULL, "it is the size question");
    TEST_ASSERT_EQ(0, g_set_text_calls, "nothing was loaded");

    reset_counters();
    g_message_answer = true;
    TEST_ASSERT(editor_reload_from_disk(), "Yes reloads it");
    TEST_ASSERT_EQ(1, g_set_text_calls, "loaded once");
    finish();
}

TEST_CASE(open_still_asks_the_same_questions) {
    // The checks moved into a helper shared with reload; Open keeps them
    static const unsigned char binary[] = { 'a', 0x00, 'b' };
    settings_clear_all();
    editor_init();
    editor_set_main_window(FAKE_WINDOW);
    TEST_ASSERT(write_file(RELOAD_FILE, binary, sizeof(binary)), "setup: binary file");
    reset_counters();
    g_binary_answer = UI_OPEN_CANCEL;
    TEST_ASSERT(!editor_open_file(RELOAD_FILE), "Cancel declines the open");
    TEST_ASSERT_EQ(1, g_binary_prompts, "the binary question was asked");
    TEST_ASSERT_EQ(0, g_set_text_calls, "nothing was loaded");
    TEST_ASSERT_NULL(editor_get_current_file(), "no file was bound");
    finish();
}

int main(void) {
    TEST_INIT();

    RUN_TEST(reload_reads_the_new_contents);
    RUN_TEST(read_failure_is_reported_and_keeps_the_document);
    RUN_TEST(binary_replacement_asks_first);
    RUN_TEST(large_replacement_asks_first);
    RUN_TEST(open_still_asks_the_same_questions);

    TEST_SUMMARY();
    return 0;
}
