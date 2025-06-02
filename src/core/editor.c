/*
 * npad - Editor Core Implementation
 * Core editor functionality and state management
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "editor.h"
#include "file_ops.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global editor state
EditorState g_editor = { 0 };
char *g_startup_file = NULL;

bool editor_init(void) {
    memset(&g_editor, 0, sizeof(EditorState));

    // Load settings
    g_editor.auto_save_enabled = settings_get_bool("auto_save_enabled", true); // Enabled by default
    g_editor.auto_save_interval = settings_get_int("auto_save_interval", 300); // 5 minutes default

    // Set event handler
    ui_set_event_handler(editor_handle_event);

    return true;
}

void editor_cleanup(void) {
    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    if (g_startup_file) {
        free(g_startup_file);
        g_startup_file = NULL;
    }
}

bool editor_new_file(void) {
    // Check if current file needs saving
    if (g_editor.is_modified && !editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    // Clear the editor
    if (g_editor.main_window) {
        ui_clear_text(g_editor.main_window);
    }

    // Reset state
    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    g_editor.is_modified = false;
    editor_update_title();

    return true;
}

bool editor_open_file(const char *filename) {
    if (!filename)
        return false;

    // Check if current file needs saving
    if (g_editor.is_modified && !editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    // Load file content
    char *content = file_read_text(filename);
    if (!content) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not open file: %s", filename);
        ui_show_message_box(g_editor.main_window, "Error", error_msg, false);
        return false;
    }

    // Set content in editor
    if (g_editor.main_window) {
        ui_set_text(g_editor.main_window, content);
    }

    free(content);

    // Update state
    if (g_editor.current_file) {
        free(g_editor.current_file);
    }

    g_editor.current_file = malloc(strlen(filename) + 1);
    strcpy(g_editor.current_file, filename);
    g_editor.is_modified = false;

    editor_update_title();

    return true;
}

bool editor_save_file(void) {
    if (!g_editor.current_file) {
        // No current file, prompt for save as
        FileDialogParams params = { .title = "Save As",
                                    .default_filename = "Untitled.txt",
                                    .filter = "Text Files (*.txt)|*.txt|All Files (*.*)|*.*",
                                    .save_dialog = true };

        char *filename = ui_show_save_dialog(g_editor.main_window, &params);
        if (!filename) {
            return false; // User cancelled
        }

        bool result = editor_save_file_as(filename);
        free(filename);
        return result;
    }

    // Save to current file
    if (!g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    bool success = file_write_text(g_editor.current_file, content);
    free(content);

    if (success) {
        g_editor.is_modified = false;
        editor_update_title();
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %s", g_editor.current_file);
        ui_show_message_box(g_editor.main_window, "Error", error_msg, false);
    }

    return success;
}

bool editor_save_file_as(const char *filename) {
    if (!filename || !g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    bool success = file_write_text(filename, content);
    free(content);

    if (success) {
        // Update current file
        if (g_editor.current_file) {
            free(g_editor.current_file);
        }

        g_editor.current_file = malloc(strlen(filename) + 1);
        strcpy(g_editor.current_file, filename);
        g_editor.is_modified = false;

        editor_update_title();
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %s", filename);
        ui_show_message_box(g_editor.main_window, "Error", error_msg, false);
    }

    return success;
}

bool editor_close_file(void) {
    // Check if current file needs saving
    if (g_editor.is_modified && !editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    return editor_new_file();
}

void editor_cut(void) {
    if (g_editor.main_window) {
        ui_cut(g_editor.main_window);
    }
}

void editor_copy(void) {
    if (g_editor.main_window) {
        ui_copy(g_editor.main_window);
    }
}

void editor_paste(void) {
    if (g_editor.main_window) {
        ui_paste(g_editor.main_window);
    }
}

void editor_select_all(void) {
    if (g_editor.main_window) {
        ui_select_all(g_editor.main_window);
    }
}

void editor_undo(void) {
    if (g_editor.main_window) {
        ui_undo(g_editor.main_window);
    }
}

void editor_redo(void) {
    if (g_editor.main_window) {
        ui_redo(g_editor.main_window);
    }
}

bool editor_find(const char *text, bool case_sensitive, bool whole_word) {
    // TODO: Implement find functionality
    // This would involve searching through the text content
    (void) text;
    (void) case_sensitive;
    (void) whole_word;
    return false;
}

bool editor_replace(const char *find_text, const char *replace_text, bool case_sensitive,
                    bool whole_word, bool replace_all) {
    // TODO: Implement replace functionality
    (void) find_text;
    (void) replace_text;
    (void) case_sensitive;
    (void) whole_word;
    (void) replace_all;
    return false;
}

bool editor_is_modified(void) {
    return g_editor.is_modified;
}

void editor_set_modified(bool modified) {
    g_editor.is_modified = modified;
    if (g_editor.main_window) {
        ui_set_text_modified(g_editor.main_window, modified);
    }
    editor_update_title();
}

const char *editor_get_current_file(void) {
    return g_editor.current_file;
}

void editor_set_startup_file(const char *filename) {
    if (g_startup_file) {
        free(g_startup_file);
        g_startup_file = NULL;
    }

    if (filename) {
        g_startup_file = malloc(strlen(filename) + 1);
        strcpy(g_startup_file, filename);
    }
}

void editor_enable_auto_save(bool enabled) {
    g_editor.auto_save_enabled = enabled;
    settings_set_bool("auto_save_enabled", enabled);
}

bool editor_is_auto_save_enabled(void) {
    return g_editor.auto_save_enabled;
}

void editor_set_auto_save_interval(int seconds) {
    g_editor.auto_save_interval = seconds;
    settings_set_int("auto_save_interval", seconds);
}

int editor_get_auto_save_interval(void) {
    return g_editor.auto_save_interval;
}

bool editor_handle_event(const UIEvent *event) {
    if (!event)
        return false;

    switch (event->type) {
        case UI_EVENT_QUIT:
            if (g_editor.is_modified && !editor_prompt_save_changes()) {
                return false; // Cancel quit
            }
            ui_quit();
            return true;

        case UI_EVENT_FILE_NEW:
            editor_new_file();
            return true;

        case UI_EVENT_FILE_OPEN: {
            FileDialogParams params = { .title = "Open",
                                        .default_filename = "",
                                        .filter = "Text Files (*.txt)|*.txt|All Files (*.*)|*.*",
                                        .save_dialog = false };

            char *filename = ui_show_open_dialog(g_editor.main_window, &params);
            if (filename) {
                editor_open_file(filename);
                free(filename);
            }
            return true;
        }

        case UI_EVENT_FILE_SAVE:
            editor_save_file();
            return true;

        case UI_EVENT_FILE_SAVE_AS: {
            FileDialogParams params = { .title = "Save As",
                                        .default_filename = "Untitled.txt",
                                        .filter = "Text Files (*.txt)|*.txt|All Files (*.*)|*.*",
                                        .save_dialog = true };

            char *filename = ui_show_save_dialog(g_editor.main_window, &params);
            if (filename) {
                editor_save_file_as(filename);
                free(filename);
            }
            return true;
        }

        case UI_EVENT_EDIT_UNDO:
            editor_undo();
            return true;

        case UI_EVENT_EDIT_REDO:
            editor_redo();
            return true;

        case UI_EVENT_EDIT_CUT:
            editor_cut();
            return true;

        case UI_EVENT_EDIT_COPY:
            editor_copy();
            return true;

        case UI_EVENT_EDIT_PASTE:
            editor_paste();
            return true;

        case UI_EVENT_EDIT_SELECT_ALL:
            editor_select_all();
            return true;

        case UI_EVENT_EDIT_FIND:
            // TODO: Show find dialog
            return true;

        case UI_EVENT_EDIT_REPLACE:
            // TODO: Show replace dialog
            return true;

        case UI_EVENT_VIEW_TOGGLE_DARK_MODE:
            ui_set_dark_mode(!ui_is_dark_mode());
            return true;

        case UI_EVENT_TEXT_CHANGED:
            g_editor.is_modified = true;
            editor_update_title();
            return true;

        case UI_EVENT_WINDOW_CLOSING:
            // This is handled in the platform-specific code
            return true;

        default:
            return false;
    }
}

bool editor_prompt_save_changes(void) {
    if (!g_editor.is_modified)
        return true;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";

    char message[512];
    snprintf(message, sizeof(message), "Do you want to save changes to %s?", filename);

    // This should return: true for Yes/No, false for Cancel
    // For now, we'll use a simple message box
    return ui_show_message_box(g_editor.main_window, "npad", message, true);
}

void editor_update_title(void) {
    if (!g_editor.main_window)
        return;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";

    // Extract just the filename from the full path
    const char *display_name = filename;
    const char *last_slash = strrchr(filename, '/');
    const char *last_backslash = strrchr(filename, '\\');

    if (last_slash || last_backslash) {
        const char *separator = (last_slash > last_backslash) ? last_slash : last_backslash;
        display_name = separator + 1;
    }

    char title[512];
    snprintf(title, sizeof(title), "%s%s - npad", g_editor.is_modified ? "*" : "", display_name);

    ui_set_window_title(g_editor.main_window, title);
}