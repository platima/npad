/*
 * npad - Editor load/save contract tests
 *
 * Runs the REAL src/core/editor.c against a fake UI layer (this file defines
 * every ui_* function the editor calls), so the core's side of the text
 * boundary is pinned without a window:
 *
 *   - ui_set_text returning false (text too large for the control, or out of
 *     memory) is a failed open or reload: the path, flags and encoding stay
 *     those of the document still on screen, so Save cannot write the old text
 *     over the new file
 *   - a load the control could not hold exactly (ui_text_altered_by_load) is
 *     never saved over its file without a word: Save diverts to Save As, and
 *     auto-save skips it, exactly like a truncated load
 *   - a failed crash-recovery restore puts the snapshot back
 *
 * Everything on disk lives under the working directory; HOME and
 * XDG_CONFIG_HOME are pointed there before settings start, so the user's real
 * configuration is never read or written.
 *
 * Author: Platima
 * https://npad.dev
 */

// setenv/mkdir (c99 mode hides them otherwise)
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "../src/core/editor.h"
#include "../src/core/file_ops.h"
#include "../src/core/session.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include "../src/ui_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

// --- Fake UI ------------------------------------------------------------------

static char g_fake_window_storage;
#define FAKE_WINDOW ((Window *) (void *) &g_fake_window_storage)

static char *g_shown_text = NULL;     // What the "control" holds
static bool g_set_text_fails = false; // Next ui_set_text refuses
static bool g_load_alters = false;    // Next successful load is not exact
static bool g_altered = false;
static int g_message_boxes = 0;
static int g_save_dialogs = 0;
static const char *g_save_dialog_answer = NULL; // NULL = the user cancels

static char *dup_str(const char *s) {
    if (!s)
        return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

bool ui_set_text(Window *window, const char *text) {
    (void) window;
    if (g_set_text_fails)
        return false;
    free(g_shown_text);
    g_shown_text = dup_str(text);
    g_altered = g_load_alters;
    return true;
}

bool ui_text_altered_by_load(Window *window) {
    (void) window;
    return g_altered;
}

char *ui_get_text(Window *window) {
    (void) window;
    return dup_str(g_shown_text ? g_shown_text : "");
}

void ui_clear_text(Window *window) {
    ui_set_text(window, "");
}

bool ui_show_message_box(Window *parent, const char *title, const char *message, bool is_question) {
    (void) parent;
    (void) title;
    (void) message;
    g_message_boxes++;
    return is_question; // Questions are answered Yes
}

char *ui_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    (void) params;
    g_save_dialogs++;
    return dup_str(g_save_dialog_answer);
}

char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}

SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_SAVE_PROMPT_DISCARD;
}

UiOpenChoice ui_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_OPEN_IN_NPAD;
}

void ui_open_with_default_app(const char *filename) {
    (void) filename;
}

size_t ui_system_memory_mb(void) {
    return 0;
}

bool ui_pid_is_running(long pid) {
    (void) pid;
    return false;
}

void ui_launch_recovery_instance(const char *slot_id, int cascade_index) {
    (void) slot_id;
    (void) cascade_index;
}

void ui_set_event_handler(UIEventHandler handler) {
    (void) handler;
}

bool ui_post_event(UIEventType type, Window *window, void *data) {
    (void) type;
    (void) window;
    (void) data;
    return true;
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

void ui_set_dark_mode(bool enabled) {
    (void) enabled;
}

bool ui_is_dark_mode(void) {
    return false;
}

void ui_quit(void) {
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

void ui_undo(Window *window) {
    (void) window;
}

void ui_redo(Window *window) {
    (void) window;
}

void ui_select_all(Window *window) {
    (void) window;
}

void ui_show_find_dialog(Window *parent) {
    (void) parent;
}

void ui_show_replace_dialog(Window *parent) {
    (void) parent;
}

// --- Helpers --------------------------------------------------------------------

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return dup_str(buf);
}

static void reset_fakes(void) {
    g_set_text_fails = false;
    g_load_alters = false;
    g_message_boxes = 0;
    g_save_dialogs = 0;
    g_save_dialog_answer = NULL;
}

#define OLD_FILE "test_editor_old.txt"
#define NEW_FILE "test_editor_new.txt"
#define COPY_FILE "test_editor_copy.txt"

// --- Failed loads ---------------------------------------------------------------

TEST_CASE(failed_set_text_is_a_failed_open) {
    reset_fakes();
    write_file(OLD_FILE, "old text\r\n");
    write_file(NEW_FILE, "new\nunix text\n");
    TEST_ASSERT(editor_open_file(OLD_FILE), "the first open works");
    TEST_ASSERT_STR_EQ(OLD_FILE, editor_get_current_file(), "bound to the old file");
    LineEnding before = editor_get_line_ending();

    g_set_text_fails = true;
    g_message_boxes = 0;
    TEST_ASSERT(!editor_open_file(NEW_FILE), "a refused load fails the open");
    TEST_ASSERT_STR_EQ(OLD_FILE, editor_get_current_file(), "the path is unchanged");
    TEST_ASSERT_EQ(before, editor_get_line_ending(), "so is the line ending");
    TEST_ASSERT(!editor_is_modified(), "and the modified flag");
    TEST_ASSERT_EQ(1, g_message_boxes, "the user is told");

    // Save now writes the old text to the OLD file, never over the new one
    g_set_text_fails = false;
    TEST_ASSERT(editor_save_file(), "save works");
    char *new_bytes = read_file(NEW_FILE);
    TEST_ASSERT_STR_EQ("new\nunix text\n", new_bytes, "the new file is untouched");
    free(new_bytes);
}

TEST_CASE(failed_set_text_is_a_failed_reload) {
    reset_fakes();
    write_file(OLD_FILE, "first\r\n");
    TEST_ASSERT(editor_open_file(OLD_FILE), "open");
    editor_set_modified(true);
    write_file(OLD_FILE, "changed on disk\n");

    g_set_text_fails = true;
    g_message_boxes = 0;
    TEST_ASSERT(!editor_reload_from_disk(), "a refused load fails the reload");
    TEST_ASSERT(editor_is_modified(), "the unsaved edit is still marked");
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, editor_get_line_ending(), "the line ending is unchanged");
    TEST_ASSERT_STR_EQ("first\r\n", g_shown_text, "the shown text is unchanged");
    TEST_ASSERT_EQ(1, g_message_boxes, "the user is told");
    editor_set_modified(false);
}

// --- Altered loads --------------------------------------------------------------

TEST_CASE(altered_load_diverts_save_to_save_as) {
    reset_fakes();
    write_file(OLD_FILE, "{\"a\":\"x\xE2\x80\xA8y\"}\r\n");
    g_load_alters = true;
    TEST_ASSERT(editor_open_file(OLD_FILE), "open loads");
    // The control "shows" a substitute; saving it would destroy the U+2028
    free(g_shown_text);
    g_shown_text = dup_str("{\"a\":\"x\r\ny\"}\r\n");

    g_message_boxes = 0;
    g_save_dialogs = 0;
    g_save_dialog_answer = NULL; // Cancelled
    TEST_ASSERT(!editor_save_file(), "save does not write the file");
    TEST_ASSERT_EQ(1, g_message_boxes, "it explains why");
    TEST_ASSERT_EQ(1, g_save_dialogs, "and offers Save As");
    char *bytes = read_file(OLD_FILE);
    TEST_ASSERT_STR_EQ("{\"a\":\"x\xE2\x80\xA8y\"}\r\n", bytes, "the original is intact");
    free(bytes);

    // Save As to another file clears the mark: that file holds what is shown
    g_save_dialog_answer = COPY_FILE;
    TEST_ASSERT(editor_save_file(), "Save As works");
    TEST_ASSERT_STR_EQ(COPY_FILE, editor_get_current_file(), "now bound to the copy");
    g_save_dialogs = 0;
    TEST_ASSERT(editor_save_file(), "a later save");
    TEST_ASSERT_EQ(0, g_save_dialogs, "goes straight to the copy");
}

TEST_CASE(altered_load_is_skipped_by_auto_save) {
    reset_fakes();
    write_file(OLD_FILE, "a\vb\r\n");
    g_load_alters = true;
    TEST_ASSERT(editor_open_file(OLD_FILE), "open loads");
    free(g_shown_text);
    g_shown_text = dup_str("a\r\nb\r\n");
    editor_set_modified(true);

    bool was = editor_is_auto_save_enabled();
    editor_enable_auto_save(true);
    g_save_dialogs = 0;
    g_message_boxes = 0;
    UIEvent ev = { UI_EVENT_AUTO_SAVE, FAKE_WINDOW, NULL };
    editor_handle_event(&ev);
    editor_enable_auto_save(was);
    char *bytes = read_file(OLD_FILE);
    TEST_ASSERT_STR_EQ("a\vb\r\n", bytes, "auto-save left the file alone");
    free(bytes);
    TEST_ASSERT_EQ(0, g_save_dialogs + g_message_boxes, "and raised nothing from a timer");
    editor_set_modified(false);
}

TEST_CASE(exact_load_saves_in_place) {
    reset_fakes();
    write_file(OLD_FILE, "plain\r\n");
    TEST_ASSERT(editor_open_file(OLD_FILE), "open");
    free(g_shown_text);
    g_shown_text = dup_str("plain edited\r\n");
    TEST_ASSERT(editor_save_file(), "save");
    TEST_ASSERT_EQ(0, g_save_dialogs, "no Save As for an exact load");
    char *bytes = read_file(OLD_FILE);
    TEST_ASSERT_STR_EQ("plain edited\r\n", bytes, "written in place");
    free(bytes);
}

TEST_CASE(an_exact_reload_clears_the_mark) {
    reset_fakes();
    write_file(OLD_FILE, "a\vb\r\n");
    g_load_alters = true;
    TEST_ASSERT(editor_open_file(OLD_FILE), "open loads");
    g_load_alters = false;
    TEST_ASSERT(editor_new_file(), "new");
    // Reopen exactly (a fixed control, say): the earlier mark must not linger
    TEST_ASSERT(editor_open_file(OLD_FILE), "reopen");
    g_save_dialogs = 0;
    TEST_ASSERT(editor_save_file(), "save");
    TEST_ASSERT_EQ(0, g_save_dialogs, "no Save As");
}

// --- Crash recovery -------------------------------------------------------------

#ifndef _WIN32
TEST_CASE(failed_restore_keeps_the_snapshot) {
    reset_fakes();
    char *dir = editor_session_dir();
    TEST_ASSERT_NOT_NULL(dir, "recovery dir");
    const char *slot = "1-7fffffff";
    TEST_ASSERT(session_write(dir, slot, "unsaved work\r\n", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF),
                "snapshot written");

    editor_set_recover_slot(slot);
    g_set_text_fails = true;
    UIEvent ev = { UI_EVENT_STARTUP_DEFERRED, FAKE_WINDOW, NULL };
    editor_handle_event(&ev);
    g_set_text_fails = false;

    int count = 0;
    char **slots = session_list_slots(dir, &count);
    bool found = false;
    for (int i = 0; i < count; i++)
        if (strcmp(slots[i], slot) == 0)
            found = true;
    session_free_slots(slots, count);
    TEST_ASSERT(found, "the snapshot is still there to offer next time");

    // And a restore that works then takes it
    editor_handle_event(&ev);
    TEST_ASSERT_STR_EQ("unsaved work\r\n", g_shown_text, "restored");
    TEST_ASSERT(editor_is_modified(), "as unsaved work");
    editor_set_recover_slot(NULL);
    editor_set_modified(false);
    session_clear_slot(dir, slot);
    free(dir);
}
#endif

int main(void) {
    TEST_INIT();
    thread_safety_init();

#ifndef _WIN32
    // A throwaway config home under the working directory: settings and the
    // recovery folder must never be the user's own
    mkdir("test_editor_home", 0755);
    char cwd[1024];
    if (!getcwd(cwd, sizeof cwd))
        return 1;
    char home[1100];
    snprintf(home, sizeof home, "%s/test_editor_home", cwd);
    setenv("HOME", home, 1);
    setenv("XDG_CONFIG_HOME", home, 1);
    if (!settings_init())
        return 1;
#endif

    editor_init();
    editor_set_main_window(FAKE_WINDOW);

    RUN_TEST(failed_set_text_is_a_failed_open);
    RUN_TEST(failed_set_text_is_a_failed_reload);
    RUN_TEST(altered_load_diverts_save_to_save_as);
    RUN_TEST(altered_load_is_skipped_by_auto_save);
    RUN_TEST(exact_load_saves_in_place);
    RUN_TEST(an_exact_reload_clears_the_mark);
#ifndef _WIN32
    RUN_TEST(failed_restore_keeps_the_snapshot);
#endif

    editor_cleanup();
    free(g_shown_text);
    remove(OLD_FILE);
    remove(NEW_FILE);
    remove(COPY_FILE);
    TEST_SUMMARY();
    return 0;
}
