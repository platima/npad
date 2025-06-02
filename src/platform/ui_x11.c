/*
 * npad - X11 UI Implementation (Stub)
 * Linux/Unix X11-specific UI implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 *
 * TODO: Implement X11 backend
 */

#include "../ui_interface.h"
#include <stdio.h>
#include <stdlib.h>

// Placeholder implementations - to be completed
// This allows the project to build for Linux while X11 implementation is pending

bool ui_platform_init(void) {
    printf("X11 UI implementation not yet available\n");
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
char *ui_platform_show_save_dialog(Window *parent, const FileDialogParams *params) {
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

Dialog *ui_platform_show_find_dialog(Window *parent) {
    (void) parent;
    return NULL;
}
Dialog *ui_platform_show_replace_dialog(Window *parent) {
    (void) parent;
    return NULL;
}
void ui_platform_close_dialog(Dialog *dialog) {
    (void) dialog;
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