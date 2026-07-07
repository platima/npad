/*
 * npad - Editor Core Implementation
 * Core editor functionality and state management
 *
 * All functions here run on the single UI thread (events are dispatched
 * from the platform message loop), so editor state needs no locking.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "editor.h"
#include "error.h"
#include "file_ops.h"
#include "settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Files larger than this (in MB, configurable via "large_file_warning_mb")
// prompt for confirmation before opening. 0 disables the prompt.
#define DEFAULT_LARGE_FILE_WARNING_MB 100

// Global editor state
EditorState g_editor = { 0 };
char *g_startup_file = NULL;

static char *duplicate_string(const char *str) {
    if (!str)
        return NULL;
    char *copy = malloc(strlen(str) + 1);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

// Push the current file's encoding/line-ending info to the status bar
static void editor_update_status_info(void) {
    if (g_editor.main_window) {
        ui_set_status_info(g_editor.main_window, file_encoding_name(g_editor.file_info.encoding),
                           file_line_ending_name(g_editor.file_info.line_ending));
    }
}

// Apply the auto-save settings to the platform timer
static void editor_apply_auto_save_timer(void) {
    if (g_editor.main_window) {
        ui_set_auto_save_timer(g_editor.main_window,
                               g_editor.auto_save_enabled ? g_editor.auto_save_interval : 0);
    }
}

bool editor_init(void) {
    memset(&g_editor, 0, sizeof(EditorState));

    g_editor.auto_save_enabled = settings_get_bool("auto_save_enabled", true);
    g_editor.auto_save_interval = settings_get_int("auto_save_interval", 300);
    if (g_editor.auto_save_interval < 10) {
        g_editor.auto_save_interval = 10; // Sanity floor
    }

    // New documents default to modern Notepad conventions
    g_editor.file_info.encoding = NPAD_ENC_UTF8;
    g_editor.file_info.line_ending = NPAD_EOL_CRLF;

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

void editor_set_main_window(Window *window) {
    g_editor.main_window = window;
    editor_update_title();
    editor_update_status_info();
    editor_apply_auto_save_timer();
}

bool editor_new_file(void) {
    // Check if current file needs saving
    if (!editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    if (g_editor.main_window) {
        ui_clear_text(g_editor.main_window);
    }

    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    g_editor.file_info.encoding = NPAD_ENC_UTF8;
    g_editor.file_info.line_ending = NPAD_EOL_CRLF;

    editor_set_modified(false);
    editor_update_status_info();

    return true;
}

bool editor_open_file(const char *filename) {
    if (!filename)
        return false;

    // Check if current file needs saving
    if (!editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    // Warn before opening very large files
    int warn_mb = settings_get_int("large_file_warning_mb", DEFAULT_LARGE_FILE_WARNING_MB);
    if (warn_mb > 0) {
        size_t size = file_get_size(filename);
        if (size > (size_t) warn_mb * 1024 * 1024) {
            char message[512];
            snprintf(message, sizeof(message),
                     "The file is %.1f MB and may take a while to open.\n"
                     "Do you want to continue?",
                     size / (1024.0 * 1024.0));
            if (!ui_show_message_box(g_editor.main_window, "npad", message, true)) {
                return false;
            }
        }
    }

    // Load file content, detecting encoding and line endings
    TextFileInfo info;
    char *content = file_read_text_ex(filename, &info);
    if (!content) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not open file: %.300s\n%.150s", filename,
                 file_get_last_error());
        ui_show_message_box(g_editor.main_window, "Error", error_msg, false);
        return false;
    }

    char *new_path = duplicate_string(filename);
    if (!new_path) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                         "Failed to allocate memory for current file path");
        free(content);
        return false;
    }

    if (g_editor.main_window) {
        ui_set_text(g_editor.main_window, content);
    }
    free(content);

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = new_path;
    g_editor.file_info = info;

    settings_add_recent_file(filename);

    editor_set_modified(false);
    editor_update_status_info();

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

    if (!g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    bool success = file_write_text_ex(g_editor.current_file, content, &g_editor.file_info);
    free(content);

    if (success) {
        editor_set_modified(false);
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s",
                 g_editor.current_file, file_get_last_error());
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

    bool success = file_write_text_ex(filename, content, &g_editor.file_info);
    free(content);

    if (success) {
        char *new_path = duplicate_string(filename);
        if (!new_path) {
            NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                             "Failed to allocate memory for new file path");
            return false;
        }

        if (g_editor.current_file) {
            free(g_editor.current_file);
        }
        g_editor.current_file = new_path;

        settings_add_recent_file(filename);
        editor_set_modified(false);
        editor_update_status_info();
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s", filename,
                 file_get_last_error());
        ui_show_message_box(g_editor.main_window, "Error", error_msg, false);
    }

    return success;
}

bool editor_close_file(void) {
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
        g_startup_file = duplicate_string(filename);
        if (!g_startup_file) {
            NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                             "Failed to allocate memory for startup file path");
        }
    }
}

void editor_enable_auto_save(bool enabled) {
    g_editor.auto_save_enabled = enabled;
    settings_set_bool("auto_save_enabled", enabled);
    editor_apply_auto_save_timer();
}

bool editor_is_auto_save_enabled(void) {
    return g_editor.auto_save_enabled;
}

void editor_set_auto_save_interval(int seconds) {
    if (seconds < 10) {
        seconds = 10;
    }
    g_editor.auto_save_interval = seconds;
    settings_set_int("auto_save_interval", seconds);
    editor_apply_auto_save_timer();
}

int editor_get_auto_save_interval(void) {
    return g_editor.auto_save_interval;
}

bool editor_handle_event(const UIEvent *event) {
    if (!event)
        return false;

    switch (event->type) {
        case UI_EVENT_QUIT:
            if (!editor_prompt_save_changes()) {
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

        case UI_EVENT_FILE_DROPPED:
            if (event->data) {
                editor_open_file((const char *) event->data);
            }
            return true;

        case UI_EVENT_AUTO_SAVE:
            // Silent auto-save: only for documents that already have a file
            if (g_editor.auto_save_enabled && g_editor.is_modified && g_editor.current_file) {
                editor_save_file();
            }
            return true;

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
            ui_show_find_dialog(g_editor.main_window);
            return true;

        case UI_EVENT_EDIT_REPLACE:
            ui_show_replace_dialog(g_editor.main_window);
            return true;

        case UI_EVENT_VIEW_TOGGLE_DARK_MODE:
            ui_set_dark_mode(!ui_is_dark_mode());
            settings_set_string("theme", ui_is_dark_mode() ? "dark" : "light");
            return true;

        case UI_EVENT_TEXT_CHANGED:
            if (!g_editor.is_modified) {
                g_editor.is_modified = true;
                editor_update_title();
            }
            return true;

        case UI_EVENT_WINDOW_CLOSING:
            // This is handled in the platform-specific code
            return true;

        default:
            return false;
    }
}

// Notepad's three-way prompt. Returns true when the pending operation may
// proceed (saved or discarded), false when the user cancelled or the save
// failed.
bool editor_prompt_save_changes(void) {
    if (!g_editor.is_modified)
        return true;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";

    switch (ui_show_save_prompt(g_editor.main_window, filename)) {
        case UI_SAVE_PROMPT_SAVE:
            return editor_save_file();
        case UI_SAVE_PROMPT_DISCARD:
            return true;
        case UI_SAVE_PROMPT_CANCEL:
        default:
            return false;
    }
}

void editor_update_title(void) {
    if (!g_editor.main_window)
        return;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";

    // Extract just the filename from the full path
    const char *display_name = filename;
    const char *last_slash = strrchr(filename, '/');
    const char *last_backslash = strrchr(filename, '\\');
    const char *separator = last_slash;
    if (!separator || (last_backslash && last_backslash > separator)) {
        separator = last_backslash;
    }
    if (separator) {
        display_name = separator + 1;
    }

    char title[512];
    snprintf(title, sizeof(title), "%s%.400s - npad", g_editor.is_modified ? "*" : "",
             display_name);

    ui_set_window_title(g_editor.main_window, title);
}
