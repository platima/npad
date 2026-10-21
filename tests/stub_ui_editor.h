/*
 * npad - Stub UI layer for the editor core tests
 * Implements the ui_* functions src/core/editor.c calls, with no window: the
 * "edit control" is a string, dialogs and message boxes return scripted
 * answers, and everything the editor asks of the UI is counted so a test can
 * check it (how many boxes, which timer intervals, what the status bar says).
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef STUB_UI_EDITOR_H
#define STUB_UI_EDITOR_H

#include "../src/ui_interface.h"
#include <stdbool.h>

#define STUB_MAX_ANSWERS 8

typedef struct {
    // The edit control
    char *text;
    bool text_modified;
    char title[512];
    char status_encoding[64];
    char status_eol[64];
    char status_message[256];

    // Timers: the last interval armed, and how many times each was (re)armed
    int auto_save_seconds;
    int session_seconds;
    int auto_save_arms;
    int session_arms;

    // Message boxes: how many were shown, the deepest nesting seen, the last
    // text, and the answers to give in order (default_answer once they run out)
    int message_boxes;
    int open_boxes;
    int max_open_boxes;
    char last_message[1024];
    bool answers[STUB_MAX_ANSWERS];
    int answer_count;
    int answer_next;
    bool default_answer;
    // Run while a box is "open", as a timer firing inside its modal loop would
    void (*during_message_box)(void);

    // Save dialog: the path it returns (NULL = Cancel), the encoding the user
    // picks in it, and what it was pre-filled with
    const char *save_dialog_result;
    bool save_dialog_picks_encoding;
    TextEncoding save_dialog_encoding;
    int save_dialogs;
    char save_dialog_default[1024];
    // Run while the Save As dialog is "open", as the activation check posted
    // when a question box closes is dispatched by the dialog's modal loop
    void (*during_save_dialog)(void);

    const char *open_dialog_result;
    SavePromptResult save_prompt_answer;
    int save_prompts;
    UiOpenChoice binary_choice;
    int binary_prompts;
    int default_app_opens;
    int stamps_appended;
} StubUi;

extern StubUi g_stub;

// Forget everything (frees the text) and set the default answers: Yes to
// questions, Cancel to every dialog, Open in npad for binary files
void stub_reset(void);
Window *stub_window(void);

// Queue answers for the next message boxes
void stub_answer(bool answer);

// Replace the edit control's text as typing would: sets it and sends
// UI_EVENT_TEXT_CHANGED the way the platform's change notification does
void stub_type(const char *text);

// Deliver an event to the editor, as the platform's message loop does
bool stub_post(UIEventType type, void *data);

#endif // STUB_UI_EDITOR_H
