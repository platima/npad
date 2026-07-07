/*
 * npad - UI Interface Implementation
 * Platform-independent UI abstraction implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "ui_interface.h"
#include <stdlib.h>
#include <string.h>

// Global event handler
static UIEventHandler g_event_handler = NULL;

// Platform-specific function prototypes
// These will be implemented in platform-specific files

// Platform initialization
extern bool ui_platform_init(void);
extern void ui_platform_cleanup(void);
extern int ui_platform_message_loop(void);
extern void ui_platform_quit(void);

// Platform window functions
extern Window *ui_platform_create_main_window(void);
extern void ui_platform_destroy_window(Window *window);
extern void ui_platform_show_window(Window *window);
extern void ui_platform_hide_window(Window *window);
extern void ui_platform_set_window_title(Window *window, const char *title);
extern void ui_platform_get_window_size(Window *window, int *width, int *height);
extern void ui_platform_set_window_size(Window *window, int width, int height);
extern void ui_platform_get_window_position(Window *window, int *x, int *y);
extern void ui_platform_set_window_position(Window *window, int x, int y);
extern void ui_platform_set_window_maximized(Window *window, bool maximized);
extern bool ui_platform_is_window_maximized(Window *window);

// Platform text functions
extern void ui_platform_set_text(Window *window, const char *text);
extern char *ui_platform_get_text(Window *window);
extern void ui_platform_clear_text(Window *window);
extern bool ui_platform_has_selection(Window *window);
extern char *ui_platform_get_selected_text(Window *window);
extern void ui_platform_select_all(Window *window);
extern void ui_platform_set_cursor_position(Window *window, int position);
extern int ui_platform_get_cursor_position(Window *window);

// Platform clipboard functions
extern void ui_platform_cut(Window *window);
extern void ui_platform_copy(Window *window);
extern void ui_platform_paste(Window *window);

// Platform undo/redo functions
extern void ui_platform_undo(Window *window);
extern void ui_platform_redo(Window *window);
extern bool ui_platform_can_undo(Window *window);
extern bool ui_platform_can_redo(Window *window);

// Platform dialog functions
extern char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params);
extern char *ui_platform_show_save_dialog(Window *parent, const FileDialogParams *params);
extern bool ui_platform_show_message_box(Window *parent, const char *title, const char *message,
                                         bool is_question);
extern SavePromptResult ui_platform_show_save_prompt(Window *parent, const char *filename);
extern void ui_platform_show_about_dialog(Window *parent);

// Platform find/replace functions
extern void ui_platform_show_find_dialog(Window *parent);
extern void ui_platform_show_replace_dialog(Window *parent);

// Platform theme functions
extern void ui_platform_set_dark_mode(bool enabled);
extern bool ui_platform_is_dark_mode(void);
extern bool ui_platform_system_supports_dark_mode(void);

// Platform status functions
extern bool ui_platform_is_text_modified(Window *window);
extern void ui_platform_set_text_modified(Window *window, bool modified);
extern int ui_platform_get_line_count(Window *window);
extern void ui_platform_get_cursor_line_column(Window *window, int *line, int *column);
extern void ui_platform_set_status_info(Window *window, const char *encoding_name,
                                        const char *eol_name);
extern void ui_platform_set_auto_save_timer(Window *window, int seconds);

// Platform-specific helpers
extern void *ui_platform_get_native_handle(Window *window);

// Implementation of interface functions

bool ui_init(void) {
    return ui_platform_init();
}

void ui_cleanup(void) {
    ui_platform_cleanup();
}

int ui_message_loop(void) {
    return ui_platform_message_loop();
}

void ui_quit(void) {
    ui_platform_quit();
}

Window *ui_create_main_window(void) {
    return ui_platform_create_main_window();
}

void ui_destroy_window(Window *window) {
    ui_platform_destroy_window(window);
}

void ui_show_window(Window *window) {
    ui_platform_show_window(window);
}

void ui_hide_window(Window *window) {
    ui_platform_hide_window(window);
}

void ui_set_window_title(Window *window, const char *title) {
    ui_platform_set_window_title(window, title);
}

void ui_get_window_size(Window *window, int *width, int *height) {
    ui_platform_get_window_size(window, width, height);
}

void ui_set_window_size(Window *window, int width, int height) {
    ui_platform_set_window_size(window, width, height);
}

void ui_get_window_position(Window *window, int *x, int *y) {
    ui_platform_get_window_position(window, x, y);
}

void ui_set_window_position(Window *window, int x, int y) {
    ui_platform_set_window_position(window, x, y);
}

void ui_set_window_maximized(Window *window, bool maximized) {
    ui_platform_set_window_maximized(window, maximized);
}

bool ui_is_window_maximized(Window *window) {
    return ui_platform_is_window_maximized(window);
}

void ui_set_text(Window *window, const char *text) {
    ui_platform_set_text(window, text);
}

char *ui_get_text(Window *window) {
    return ui_platform_get_text(window);
}

void ui_clear_text(Window *window) {
    ui_platform_clear_text(window);
}

bool ui_has_selection(Window *window) {
    return ui_platform_has_selection(window);
}

char *ui_get_selected_text(Window *window) {
    return ui_platform_get_selected_text(window);
}

void ui_select_all(Window *window) {
    ui_platform_select_all(window);
}

void ui_set_cursor_position(Window *window, int position) {
    ui_platform_set_cursor_position(window, position);
}

int ui_get_cursor_position(Window *window) {
    return ui_platform_get_cursor_position(window);
}

void ui_cut(Window *window) {
    ui_platform_cut(window);
}

void ui_copy(Window *window) {
    ui_platform_copy(window);
}

void ui_paste(Window *window) {
    ui_platform_paste(window);
}

void ui_undo(Window *window) {
    ui_platform_undo(window);
}

void ui_redo(Window *window) {
    ui_platform_redo(window);
}

bool ui_can_undo(Window *window) {
    return ui_platform_can_undo(window);
}

bool ui_can_redo(Window *window) {
    return ui_platform_can_redo(window);
}

char *ui_show_open_dialog(Window *parent, const FileDialogParams *params) {
    return ui_platform_show_open_dialog(parent, params);
}

char *ui_show_save_dialog(Window *parent, const FileDialogParams *params) {
    return ui_platform_show_save_dialog(parent, params);
}

bool ui_show_message_box(Window *parent, const char *title, const char *message, bool is_question) {
    return ui_platform_show_message_box(parent, title, message, is_question);
}

SavePromptResult ui_show_save_prompt(Window *parent, const char *filename) {
    return ui_platform_show_save_prompt(parent, filename);
}

void ui_show_about_dialog(Window *parent) {
    ui_platform_show_about_dialog(parent);
}

void ui_show_find_dialog(Window *parent) {
    ui_platform_show_find_dialog(parent);
}

void ui_show_replace_dialog(Window *parent) {
    ui_platform_show_replace_dialog(parent);
}

void ui_set_event_handler(UIEventHandler handler) {
    g_event_handler = handler;
}

bool ui_post_event(UIEventType type, Window *window, void *data) {
    if (g_event_handler) {
        UIEvent event = { type, window, data };
        return g_event_handler(&event);
    }
    return false;
}

void ui_set_dark_mode(bool enabled) {
    ui_platform_set_dark_mode(enabled);
}

bool ui_is_dark_mode(void) {
    return ui_platform_is_dark_mode();
}

bool ui_system_supports_dark_mode(void) {
    return ui_platform_system_supports_dark_mode();
}

bool ui_is_text_modified(Window *window) {
    return ui_platform_is_text_modified(window);
}

void ui_set_text_modified(Window *window, bool modified) {
    ui_platform_set_text_modified(window, modified);
}

int ui_get_line_count(Window *window) {
    return ui_platform_get_line_count(window);
}

void ui_get_cursor_line_column(Window *window, int *line, int *column) {
    ui_platform_get_cursor_line_column(window, line, column);
}

void ui_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    ui_platform_set_status_info(window, encoding_name, eol_name);
}

void ui_set_auto_save_timer(Window *window, int seconds) {
    ui_platform_set_auto_save_timer(window, seconds);
}

void *ui_get_native_handle(Window *window) {
    return ui_platform_get_native_handle(window);
}