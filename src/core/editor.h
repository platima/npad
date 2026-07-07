/*
 * npad - Editor Core
 * Core editor functionality and state management
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef EDITOR_H
#define EDITOR_H

#include "../ui_interface.h"
#include "file_ops.h"
#include <stdbool.h>

// Editor state. All editor functions run on the single UI thread; the
// state is not shared across threads.
typedef struct {
    char *current_file; // UTF-8 path, NULL for a new/untitled document
    bool is_modified;
    bool auto_save_enabled;
    int auto_save_interval;      // seconds
    bool session_resume_enabled; // Crash-recovery snapshots
    int session_interval;        // seconds
    TextFileInfo file_info;      // Encoding and line endings of the current file
    Window *main_window;
} EditorState;

// Core editor functions
bool editor_init(void);
void editor_cleanup(void);

// Attach the main window (applies title, status bar info, auto-save timer)
void editor_set_main_window(Window *window);

// File operations
bool editor_new_file(void);
bool editor_open_file(const char *filename);
bool editor_save_file(void);
bool editor_save_file_as(const char *filename);
bool editor_close_file(void);

// Text operations
void editor_cut(void);
void editor_copy(void);
void editor_paste(void);
void editor_select_all(void);
void editor_undo(void);
void editor_redo(void);

// State management
bool editor_is_modified(void);
void editor_set_modified(bool modified);
const char *editor_get_current_file(void);
void editor_set_startup_file(const char *filename);

// Document format (applied when the file is next saved; marks modified)
void editor_set_line_ending(LineEnding line_ending);
LineEnding editor_get_line_ending(void);
void editor_set_encoding(TextEncoding encoding);
TextEncoding editor_get_encoding(void);

// Auto-save functionality
void editor_enable_auto_save(bool enabled);
bool editor_is_auto_save_enabled(void);
void editor_set_auto_save_interval(int seconds);
int editor_get_auto_save_interval(void);

// Session recovery (crash protection)
void editor_enable_session_resume(bool enabled);
bool editor_is_session_resume_enabled(void);

// Event handling
bool editor_handle_event(const UIEvent *event);

// Utility functions
bool editor_prompt_save_changes(void);
void editor_update_title(void);

#endif // EDITOR_H
