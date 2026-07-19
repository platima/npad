/*
 * npad - X11 & Wayland UI Implementation (Stub)
 * Linux/Unix X11 & Wayland-specific UI implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 *
 * TODO: Implement X11 & Wayland backend
 */

#include "../ui_interface.h"
#include <stdio.h>
#include <stdlib.h>

// Placeholder implementations - to be completed
// This allows the project to build for Linux while X11 & Wayland implementation is pending

bool ui_platform_init(void) {
    printf("X11 & Wayland UI implementation not yet available\n");
    return false;
}

void ui_platform_cleanup(void) {
}
int ui_platform_message_loop(void) {
    return 0;
}
void ui_platform_quit(void) {
}

Window *ui_platform_create_main_window(void) {
    return NULL;
}
void ui_platform_destroy_window(Window *window) {
    (void) window;
}
void ui_platform_show_window(Window *window) {
    (void) window;
}
void ui_platform_hide_window(Window *window) {
    (void) window;
}
void ui_platform_set_window_title(Window *window, const char *title) {
    (void) window;
    (void) title;
}
void ui_platform_get_window_size(Window *window, int *width, int *height) {
    (void) window;
    (void) width;
    (void) height;
}
void ui_platform_set_window_size(Window *window, int width, int height) {
    (void) window;
    (void) width;
    (void) height;
}
void ui_platform_get_window_position(Window *window, int *x, int *y) {
    (void) window;
    (void) x;
    (void) y;
}
void ui_platform_set_window_position(Window *window, int x, int y) {
    (void) window;
    (void) x;
    (void) y;
}

void ui_platform_set_text(Window *window, const char *text) {
    (void) window;
    (void) text;
}
char *ui_platform_get_text(Window *window) {
    (void) window;
    return NULL;
}
void ui_platform_clear_text(Window *window) {
    (void) window;
}
bool ui_platform_has_selection(Window *window) {
    (void) window;
    return false;
}
char *ui_platform_get_selected_text(Window *window) {
    (void) window;
    return NULL;
}
void ui_platform_select_all(Window *window) {
    (void) window;
}
void ui_platform_set_cursor_position(Window *window, int position) {
    (void) window;
    (void) position;
}
int ui_platform_get_cursor_position(Window *window) {
    (void) window;
    return 0;
}

void ui_platform_cut(Window *window) {
    (void) window;
}
void ui_platform_copy(Window *window) {
    (void) window;
}
void ui_platform_paste(Window *window) {
    (void) window;
}

void ui_platform_undo(Window *window) {
    (void) window;
}
void ui_platform_redo(Window *window) {
    (void) window;
}
bool ui_platform_can_undo(Window *window) {
    (void) window;
    return false;
}
bool ui_platform_can_redo(Window *window) {
    (void) window;
    return false;
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}
char *ui_platform_show_save_dialog(Window *parent, FileDialogParams *params) {
    (void) parent;
    (void) params;
    return NULL;
}
bool ui_platform_show_message_box(Window *parent, const char *title, const char *message,
                                  bool is_question) {
    (void) parent;
    (void) title;
    (void) message;
    (void) is_question;
    return false;
}
void ui_platform_show_about_dialog(Window *parent) {
    (void) parent;
}

UiOpenChoice ui_platform_prompt_binary_open(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_OPEN_IN_NPAD; // No prompt UI here; open as text
}
void ui_platform_open_with_default_app(const char *filename) {
    (void) filename;
}

void ui_platform_show_find_dialog(Window *parent) {
    (void) parent;
}
void ui_platform_show_replace_dialog(Window *parent) {
    (void) parent;
}
void ui_platform_set_dark_mode(bool enabled) {
    (void) enabled;
}
bool ui_platform_is_dark_mode(void) {
    return false;
}
bool ui_platform_system_supports_dark_mode(void) {
    return false;
}

bool ui_platform_is_text_modified(Window *window) {
    (void) window;
    return false;
}
void ui_platform_set_text_modified(Window *window, bool modified) {
    (void) window;
    (void) modified;
}
int ui_platform_get_line_count(Window *window) {
    (void) window;
    return 0;
}
void ui_platform_get_cursor_line_column(Window *window, int *line, int *column) {
    (void) window;
    (void) line;
    (void) column;
}

void *ui_platform_get_native_handle(Window *window) {
    (void) window;
    return NULL;
}

SavePromptResult ui_platform_show_save_prompt(Window *parent, const char *filename) {
    (void) parent;
    (void) filename;
    return UI_SAVE_PROMPT_DISCARD;
}

void ui_platform_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    (void) window;
    (void) encoding_name;
    (void) eol_name;
}

void ui_platform_set_auto_save_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}

void ui_platform_set_session_timer(Window *window, int seconds) {
    (void) window;
    (void) seconds;
}

void ui_platform_launch_recovery_instance(const char *slot_id, int cascade_index) {
    (void) slot_id;
    (void) cascade_index;
}

void ui_platform_notify_settings_changed(void) {
}

bool ui_platform_pid_is_running(long pid) {
    (void) pid;
    return false;
}

void ui_platform_get_default_window_rect(int *x, int *y, int *width, int *height) {
    if (x)
        *x = 100;
    if (y)
        *y = 100;
    if (width)
        *width = 800;
    if (height)
        *height = 600;
}

void ui_platform_set_window_maximized(Window *window, bool maximized) {
    (void) window;
    (void) maximized;
}

bool ui_platform_is_window_maximized(Window *window) {
    (void) window;
    return false;
}
