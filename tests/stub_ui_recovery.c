/*
 * npad - Stub UI for the editor-level recovery tests
 *
 * See stub_ui_recovery.h. Only what src/core/editor.c needs; anything with
 * no bearing on recovery is a no-op.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "stub_ui_recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Window {
    int unused;
};

StubUi g_stub;
static Window g_window;
static UIEventHandler g_handler = NULL;

static char *dup_text(const char *text) {
    size_t len = strlen(text ? text : "");
    char *copy = malloc(len + 1);
    if (copy)
        memcpy(copy, text ? text : "", len + 1);
    return copy;
}

void stub_reset(void) {
    free(g_stub.text);
    memset(&g_stub, 0, sizeof(g_stub));
    g_stub.text = dup_text("");
    g_stub.answer = UI_ANSWER_DISMISSED;
    g_stub.save_prompt = UI_SAVE_PROMPT_CANCEL;
    g_stub.binary_choice = UI_OPEN_IN_NPAD;
}

Window *stub_window(void) {
    return &g_window;
}

void stub_set_text(const char *text) {
    free(g_stub.text);
    g_stub.text = dup_text(text);
}

void stub_set_live(long pid, long long start_time) {
    if (g_stub.live_count < STUB_MAX_LIVE) {
        g_stub.live_pid[g_stub.live_count] = pid;
        g_stub.live_start[g_stub.live_count] = start_time;
        g_stub.live_count++;
    }
}

// --- What editor.c calls --------------------------------------------------------

void ui_set_event_handler(UIEventHandler handler) {
    g_handler = handler;
}

bool ui_post_event(UIEventType type, Window *window, void *data) {
    if (!g_handler)
        return false;
    UIEvent event = { type, window, data };
    return g_handler(&event);
}

void ui_quit(void) {
    g_stub.quit_called = true;
}

void ui_set_text(Window *window, const char *text) {
    (void) window;
    stub_set_text(text);
}

char *ui_get_text(Window *window) {
    (void) window;
    return dup_text(g_stub.text);
}

void ui_clear_text(Window *window) {
    (void) window;
    stub_set_text("");
}

void ui_set_text_modified(Window *window, bool modified) {
    (void) window;
    (void) modified;
}

void ui_set_window_title(Window *window, const char *title) {
    (void) window;
    (void) title;
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

UiAnswer ui_ask_question(Window *parent, const char *title, const char *message) {
    (void) parent;
    (void) title;
    (void) message;
    g_stub.questions++;
    if (g_stub.during_question)
        g_stub.during_question();
    return g_stub.answer;
}

bool ui_show_message_box(Window *parent, const char *title, const char *message, bool is_question) {
    (void) parent;
    (void) title;
    (void) message;
    if (is_question)
        return g_stub.message_box_yes;
    g_stub.info_boxes++;
    return true;
}

SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    g_stub.save_prompts++;
    return g_stub.save_prompt;
}

char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}

char *ui_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    (void) params;
    g_stub.save_dialogs++;
    return NULL; // Cancel
}

UiOpenChoice ui_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return g_stub.binary_choice;
}

void ui_open_with_default_app(const char *filename) {
    (void) filename;
}

size_t ui_system_memory_mb(void) {
    return 0;
}

void ui_launch_recovery_instance(const char *slot_id, int cascade_index) {
    if (g_stub.launches < STUB_MAX_LAUNCHES) {
        snprintf(g_stub.launch_args[g_stub.launches], sizeof(g_stub.launch_args[0]), "%s",
                 slot_id ? slot_id : "");
        g_stub.launch_cascade[g_stub.launches] = cascade_index;
    }
    g_stub.launches++;
}

bool ui_pid_is_running(long pid, long long *start_time) {
    if (start_time)
        *start_time = -1;
    for (int i = 0; i < g_stub.live_count; i++) {
        if (g_stub.live_pid[i] == pid) {
            if (start_time)
                *start_time = g_stub.live_start[i];
            return true;
        }
    }
    return false;
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

void ui_show_find_dialog(Window *parent) {
    (void) parent;
}

void ui_show_replace_dialog(Window *parent) {
    (void) parent;
}

static bool g_dark = false;

void ui_set_dark_mode(bool enabled) {
    g_dark = enabled;
}

bool ui_is_dark_mode(void) {
    return g_dark;
}
