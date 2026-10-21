/*
 * npad - Stub UI layer for the editor core tests
 * See stub_ui_editor.h. Replaces src/ui_interface.c and every platform layer
 * in tests/test_editor, so src/core/editor.c runs unmodified with no window.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "stub_ui_editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Window {
    int unused;
};

StubUi g_stub;
static Window g_window;
static UIEventHandler g_handler = NULL;

static char *copy_string(const char *text) {
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (copy)
        memcpy(copy, text, len + 1);
    return copy;
}

void stub_reset(void) {
    free(g_stub.text);
    memset(&g_stub, 0, sizeof(g_stub));
    g_stub.text = copy_string("");
    g_stub.auto_save_seconds = -1;
    g_stub.session_seconds = -1;
    g_stub.default_answer = true;
    g_stub.save_prompt_answer = UI_SAVE_PROMPT_CANCEL;
    g_stub.binary_choice = UI_OPEN_IN_NPAD;
}

Window *stub_window(void) {
    return &g_window;
}

void stub_answer(bool answer) {
    if (g_stub.answer_count < STUB_MAX_ANSWERS)
        g_stub.answers[g_stub.answer_count++] = answer;
}

bool stub_post(UIEventType type, void *data) {
    if (!g_handler)
        return false;
    UIEvent event = { type, &g_window, data };
    return g_handler(&event);
}

void stub_type(const char *text) {
    free(g_stub.text);
    g_stub.text = copy_string(text);
    g_stub.text_modified = true;
    stub_post(UI_EVENT_TEXT_CHANGED, NULL);
}

// --- Text -----------------------------------------------------------------

bool ui_set_text(Window *window, const char *text) {
    (void) window;
    free(g_stub.text);
    g_stub.text = copy_string(text ? text : "");
    g_stub.text_modified = false; // Like the platforms: fresh content is unmodified
    return true;
}

bool ui_text_altered_by_load(Window *window) {
    (void) window;
    return false; // The stub holds any text exactly
}

char *ui_get_text(Window *window) {
    (void) window;
    return copy_string(g_stub.text ? g_stub.text : "");
}

void ui_clear_text(Window *window) {
    ui_set_text(window, "");
}

void ui_append_time_date_line(Window *window) {
    (void) window;
    // A fixed stamp: the format is the platform's, the placement is the test
    static const char stamp[] = "\n12:34 21/10/2026\n";
    size_t len = strlen(g_stub.text);
    char *grown = malloc(len + sizeof(stamp));
    if (!grown)
        return;
    memcpy(grown, g_stub.text, len);
    memcpy(grown + len, stamp, sizeof(stamp));
    free(g_stub.text);
    g_stub.text = grown;
    g_stub.stamps_appended++;
    g_stub.text_modified = true;
    stub_post(UI_EVENT_TEXT_CHANGED, NULL);
}

void ui_set_text_modified(Window *window, bool modified) {
    (void) window;
    g_stub.text_modified = modified;
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

void ui_undo(Window *window) {
    (void) window;
}

void ui_redo(Window *window) {
    (void) window;
}

// --- Window, status bar and timers ------------------------------------------

void ui_set_window_title(Window *window, const char *title) {
    (void) window;
    snprintf(g_stub.title, sizeof(g_stub.title), "%s", title ? title : "");
}

void ui_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    (void) window;
    snprintf(g_stub.status_encoding, sizeof(g_stub.status_encoding), "%s",
             encoding_name ? encoding_name : "");
    snprintf(g_stub.status_eol, sizeof(g_stub.status_eol), "%s", eol_name ? eol_name : "");
}

void ui_set_status_message(Window *window, const char *message) {
    (void) window;
    snprintf(g_stub.status_message, sizeof(g_stub.status_message), "%s", message ? message : "");
}

void ui_set_auto_save_timer(Window *window, int seconds) {
    (void) window;
    g_stub.auto_save_seconds = seconds;
    g_stub.auto_save_arms++;
}

void ui_set_session_timer(Window *window, int seconds) {
    (void) window;
    g_stub.session_seconds = seconds;
    g_stub.session_arms++;
}

void ui_quit(void) {
}

void ui_set_dark_mode(bool enabled) {
    (void) enabled;
}

bool ui_is_dark_mode(void) {
    return false;
}

void ui_set_event_handler(UIEventHandler handler) {
    g_handler = handler;
}

// --- Dialogs ----------------------------------------------------------------

bool ui_show_message_box(Window *parent, const char *title, const char *message,
                         bool is_question) {
    (void) parent;
    (void) title;
    g_stub.message_boxes++;
    snprintf(g_stub.last_message, sizeof(g_stub.last_message), "%s", message ? message : "");

    g_stub.open_boxes++;
    if (g_stub.open_boxes > g_stub.max_open_boxes)
        g_stub.max_open_boxes = g_stub.open_boxes;
    if (g_stub.during_message_box)
        g_stub.during_message_box();
    g_stub.open_boxes--;

    if (!is_question)
        return true;
    if (g_stub.answer_next < g_stub.answer_count)
        return g_stub.answers[g_stub.answer_next++];
    return g_stub.default_answer;
}

// The tri-state question: the scripted answers, Yes or No (never dismissed)
UiAnswer ui_ask_question(Window *parent, const char *title, const char *message) {
    return ui_show_message_box(parent, title, message, true) ? UI_ANSWER_YES : UI_ANSWER_NO;
}

char *ui_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    g_stub.save_dialogs++;
    snprintf(g_stub.save_dialog_default, sizeof(g_stub.save_dialog_default), "%s",
             params && params->default_filename ? params->default_filename : "");
    if (g_stub.during_save_dialog)
        g_stub.during_save_dialog();
    if (!g_stub.save_dialog_result)
        return NULL;
    if (params && g_stub.save_dialog_picks_encoding)
        params->encoding = g_stub.save_dialog_encoding;
    return copy_string(g_stub.save_dialog_result);
}

char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return g_stub.open_dialog_result ? copy_string(g_stub.open_dialog_result) : NULL;
}

SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    g_stub.save_prompts++;
    g_stub.open_boxes++;
    if (g_stub.during_message_box)
        g_stub.during_message_box(); // A timer firing while "Save changes?" is up
    g_stub.open_boxes--;
    return g_stub.save_prompt_answer;
}

UiOpenChoice ui_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    g_stub.binary_prompts++;
    return g_stub.binary_choice;
}

void ui_open_with_default_app(const char *filename) {
    (void) filename;
    g_stub.default_app_opens++;
}

void ui_show_find_dialog(Window *parent) {
    (void) parent;
}

void ui_show_replace_dialog(Window *parent) {
    (void) parent;
}

// --- System -----------------------------------------------------------------

size_t ui_system_memory_mb(void) {
    return 0; // Unknown: the editor falls back to its fixed large-file threshold
}

bool ui_pid_is_running(long pid, long long *start_time) {
    (void) pid;
    if (start_time)
        *start_time = 0;
    return false;
}

void ui_launch_recovery_instance(const char *slot_id, int cascade_index) {
    (void) slot_id;
    (void) cascade_index;
}
