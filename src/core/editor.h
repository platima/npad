/*
 * npad - Editor Core
 * Core editor functionality and state management
 * 
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include "../ui_interface.h"

// Editor state
typedef struct {
    char* current_file;
    bool is_modified;
    bool auto_save_enabled;
    int auto_save_interval; // seconds
    Window* main_window;
} EditorState;

// Core editor functions
bool editor_init(void);
void editor_cleanup(void);

// File operations
bool editor_new_file(void);
bool editor_open_file(const char* filename);
bool editor_save_file(void);
bool editor_save_file_as(const char* filename);
bool editor_close_file(void);

// Text operations
void editor_cut(void);
void editor_copy(void);
void editor_paste(void);
void editor_select_all(void);
void editor_undo(void);
void editor_redo(void);

// Search operations
bool editor_find(const char* text, bool case_sensitive, bool whole_word);
bool editor_replace(const char* find_text, const char* replace_text, 
                   bool case_sensitive, bool whole_word, bool replace_all);

// State management
bool editor_is_modified(void);
void editor_set_modified(bool modified);
const char* editor_get_current_file(void);
void editor_set_startup_file(const char* filename);

// Auto-save functionality
void editor_enable_auto_save(bool enabled);
bool editor_is_auto_save_enabled(void);
void editor_set_auto_save_interval(int seconds);
int editor_get_auto_save_interval(void);

// Event handling
bool editor_handle_event(const UIEvent* event);

// Utility functions
bool editor_prompt_save_changes(void);
void editor_update_title(void);

#endif // EDITOR_H