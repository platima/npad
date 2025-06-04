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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strnicmp _strnicmp
#else
#include <strings.h>
#define strnicmp strncasecmp
#endif

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
    if (!g_editor.current_file) {
        return false;
    }
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
        if (!g_editor.current_file) {
            return false;
        }
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
    if (!text || !g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    int cursor_pos = ui_get_cursor_position(g_editor.main_window);
    char *search_start = content + cursor_pos;
    char *found = NULL;

    if (case_sensitive) {
        if (whole_word) {
            // Case-sensitive whole word search
            char *pos = search_start;
            while ((pos = strstr(pos, text)) != NULL) {
                // Check if it's a whole word
                bool is_start = (pos == content || !isalnum(*(pos - 1)));
                bool is_end = !isalnum(*(pos + strlen(text)));
                if (is_start && is_end) {
                    found = pos;
                    break;
                }
                pos++;
            }
        } else {
            // Case-sensitive search
            found = strstr(search_start, text);
        }
    } else {
        // Case-insensitive search - simplified implementation
        size_t text_len = strlen(text);
        char *pos = search_start;
        while (*pos) {
            if (strnicmp(pos, text, text_len) == 0) {
                if (!whole_word ||
                    ((pos == content || !isalnum(*(pos - 1))) && !isalnum(*(pos + text_len)))) {
                    found = pos;
                    break;
                }
            }
            pos++;
        }
    }

    bool result = false;
    if (found) {
        int found_pos = found - content;
        ui_set_cursor_position(g_editor.main_window, found_pos);
        // Select the found text
        ui_set_cursor_position(g_editor.main_window, found_pos + strlen(text));
        result = true;
    }

    free(content);
    return result;
}

bool editor_replace(const char *find_text, const char *replace_text, bool case_sensitive,
                    bool whole_word, bool replace_all) {
    if (!find_text || !replace_text || !g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    size_t content_len = strlen(content);
    size_t find_len = strlen(find_text);
    size_t replace_len = strlen(replace_text);

    // Calculate new content size
    int replacement_count = 0;
    char *pos = content;

    // Count replacements needed
    while (*pos) {
        char *match = NULL;
        if (case_sensitive) {
            match = strstr(pos, find_text);
        } else {
            // Simple case-insensitive search
            for (char *p = pos; *p; p++) {
                if (strnicmp(p, find_text, find_len) == 0) {
                    if (!whole_word ||
                        ((p == content || !isalnum(*(p - 1))) && !isalnum(*(p + find_len)))) {
                        match = p;
                        break;
                    }
                }
            }
        }

        if (match) {
            if (whole_word && case_sensitive) {
                // Check word boundaries
                bool is_start = (match == content || !isalnum(*(match - 1)));
                bool is_end = !isalnum(*(match + find_len));
                if (!is_start || !is_end) {
                    pos = match + 1;
                    continue;
                }
            }
            replacement_count++;
            pos = match + find_len;
            if (!replace_all)
                break;
        } else {
            break;
        }
    }

    if (replacement_count == 0) {
        free(content);
        return false;
    }

    // Allocate new content buffer
    size_t new_size = content_len + (replacement_count * (replace_len - find_len)) + 1;
    char *new_content = malloc(new_size);
    if (!new_content) {
        free(content);
        return false;
    }

    // Perform replacements
    char *src = content;
    char *dst = new_content;
    int replacements_done = 0;

    while (*src && (replace_all || replacements_done == 0)) {
        char *match = NULL;
        if (case_sensitive) {
            match = strstr(src, find_text);
        } else {
            for (char *p = src; *p; p++) {
                if (strnicmp(p, find_text, find_len) == 0) {
                    if (!whole_word ||
                        ((p == content || !isalnum(*(p - 1))) && !isalnum(*(p + find_len)))) {
                        match = p;
                        break;
                    }
                }
            }
        }

        if (match) {
            if (whole_word && case_sensitive) {
                bool is_start = (match == content || !isalnum(*(match - 1)));
                bool is_end = !isalnum(*(match + find_len));
                if (!is_start || !is_end) {
                    *dst++ = *src++;
                    continue;
                }
            }

            // Copy text before match
            while (src < match) {
                *dst++ = *src++;
            }

            // Copy replacement text
            strcpy(dst, replace_text);
            dst += replace_len;
            src += find_len;
            replacements_done++;
        } else {
            *dst++ = *src++;
        }
    }

    // Copy remaining text
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';

    // Update editor content
    ui_set_text(g_editor.main_window, new_content);
    g_editor.is_modified = true;
    editor_update_title();

    free(content);
    free(new_content);
    return replacements_done > 0;
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
        if (!g_startup_file) {
            return;
        }
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
    snprintf(message, sizeof(message), "Do you want to save changes to %.400s?", filename);

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
    snprintf(title, sizeof(title), "%s%.400s - npad", g_editor.is_modified ? "*" : "",
             display_name);

    ui_set_window_title(g_editor.main_window, title);
}