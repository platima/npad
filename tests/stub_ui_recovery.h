/*
 * npad - Stub UI for the editor-level recovery tests
 *
 * Implements the ui_* functions src/core/editor.c calls, with no window
 * system: the "editor" is a string, dialogs answer from fields set by the
 * test, and process liveness comes from a table the test fills in.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef STUB_UI_RECOVERY_H
#define STUB_UI_RECOVERY_H

#include "../src/ui_interface.h"
#include <stdbool.h>

#define STUB_MAX_LAUNCHES 8
#define STUB_MAX_LIVE 8

typedef struct {
    char *text; // The editor's contents (malloc'd)

    // Questions (ui_ask_question)
    int questions;
    UiAnswer answer;
    // Run while the question is up, before it returns: a quit or a session
    // end arriving mid-prompt
    void (*during_question)(void);

    // Yes/No message boxes (ui_show_message_box with is_question)
    bool message_box_yes;
    int info_boxes; // Plain message boxes shown

    SavePromptResult save_prompt;
    int save_prompts;
    int save_dialogs; // ui_show_save_dialog calls; always answered with Cancel
    UiOpenChoice binary_choice;

    bool quit_called;

    int launches;
    char launch_args[STUB_MAX_LAUNCHES][160];
    int launch_cascade[STUB_MAX_LAUNCHES];

    // Running npad processes: pid and start time (-1 = unknown)
    int live_count;
    long live_pid[STUB_MAX_LIVE];
    long long live_start[STUB_MAX_LIVE];
} StubUi;

extern StubUi g_stub;

void stub_reset(void);
Window *stub_window(void);
void stub_set_text(const char *text);
void stub_set_live(long pid, long long start_time);

#endif // STUB_UI_RECOVERY_H
