/*
 * npad - UI Interface Layer
 * Platform-independent UI abstraction
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef UI_INTERFACE_H
#define UI_INTERFACE_H

#include "core/file_ops.h" // TextEncoding / LineEnding
#include <stdbool.h>
#include <stddef.h>

// Forward declarations for platform-specific types
typedef struct Window Window;
typedef struct Menu Menu;

// Event types
typedef enum {
    UI_EVENT_QUIT,
    UI_EVENT_FILE_NEW,
    UI_EVENT_FILE_OPEN,
    UI_EVENT_FILE_SAVE,
    UI_EVENT_FILE_SAVE_AS,
    UI_EVENT_EDIT_UNDO,
    UI_EVENT_EDIT_REDO,
    UI_EVENT_EDIT_CUT,
    UI_EVENT_EDIT_COPY,
    UI_EVENT_EDIT_PASTE,
    UI_EVENT_EDIT_SELECT_ALL,
    UI_EVENT_EDIT_FIND,
    UI_EVENT_EDIT_REPLACE,
    UI_EVENT_VIEW_TOGGLE_DARK_MODE,
    UI_EVENT_TEXT_CHANGED,
    UI_EVENT_WINDOW_CLOSING,
    UI_EVENT_AUTO_SAVE,        // Auto-save timer fired
    UI_EVENT_FILE_DROPPED,     // File dragged onto the window; data = UTF-8 path
    UI_EVENT_SESSION_SNAPSHOT, // Session-recovery timer fired
    UI_EVENT_STARTUP_DEFERRED  // Fired once shortly after the window first paints
} UIEventType;

// Event structure
typedef struct {
    UIEventType type;
    Window *window;
    void *data;
} UIEvent;

// Event handler function pointer
typedef bool (*UIEventHandler)(const UIEvent *event);

// Window creation parameters
typedef struct {
    int width;
    int height;
    int x;
    int y;
    const char *title;
    bool resizable;
    bool dark_mode;
} WindowParams;

// File dialog parameters. For save dialogs, `encoding` carries the current
// encoding in and the user's choice from the dialog's encoding picker out.
typedef struct {
    const char *title;
    const char *default_filename;
    const char *filter;
    bool save_dialog;
    TextEncoding encoding;
} FileDialogParams;

// Result of the "save changes?" prompt (Notepad's three-way choice)
typedef enum {
    UI_SAVE_PROMPT_SAVE,    // Save the file, then continue
    UI_SAVE_PROMPT_DISCARD, // Continue without saving
    UI_SAVE_PROMPT_CANCEL   // Abort the operation
} SavePromptResult;

// Core UI functions
bool ui_init(void);
void ui_cleanup(void);
int ui_message_loop(void);
void ui_quit(void);

// Window management
Window *ui_create_main_window(void);
void ui_destroy_window(Window *window);
void ui_show_window(Window *window);
void ui_hide_window(Window *window);
void ui_set_window_title(Window *window, const char *title);
void ui_get_window_size(Window *window, int *width, int *height);
void ui_set_window_size(Window *window, int width, int height);
void ui_get_window_position(Window *window, int *x, int *y);
void ui_set_window_position(Window *window, int x, int y);
void ui_set_window_maximized(Window *window, bool maximized);
bool ui_is_window_maximized(Window *window);

// Sensible first-run window geometry: a DPI-correct fraction of the primary
// monitor work area, centred.
void ui_get_default_window_rect(int *x, int *y, int *width, int *height);

// Text editing
void ui_set_text(Window *window, const char *text);
char *ui_get_text(Window *window);
void ui_clear_text(Window *window);
bool ui_has_selection(Window *window);
char *ui_get_selected_text(Window *window);
void ui_select_all(Window *window);
void ui_set_cursor_position(Window *window, int position);
int ui_get_cursor_position(Window *window);

// Clipboard operations
void ui_cut(Window *window);
void ui_copy(Window *window);
void ui_paste(Window *window);

// Undo/Redo
void ui_undo(Window *window);
void ui_redo(Window *window);
bool ui_can_undo(Window *window);
bool ui_can_redo(Window *window);

// Dialogs
char *ui_show_open_dialog(Window *parent, const FileDialogParams *params);
char *ui_show_save_dialog(Window *parent, FileDialogParams *params);
bool ui_show_message_box(Window *parent, const char *title, const char *message, bool is_question);
SavePromptResult ui_show_save_prompt(Window *parent, const char *filename);
void ui_show_about_dialog(Window *parent);

// Find/Replace (modeless dialogs owned and closed by the platform layer)
void ui_show_find_dialog(Window *parent);
void ui_show_replace_dialog(Window *parent);

// Event handling
void ui_set_event_handler(UIEventHandler handler);
bool ui_post_event(UIEventType type, Window *window, void *data);

// Theme support
void ui_set_dark_mode(bool enabled);
bool ui_is_dark_mode(void);
bool ui_system_supports_dark_mode(void);

// Status information
bool ui_is_text_modified(Window *window);
void ui_set_text_modified(Window *window, bool modified);
int ui_get_line_count(Window *window);
void ui_get_cursor_line_column(Window *window, int *line, int *column);
void ui_set_status_info(Window *window, const char *encoding_name, const char *eol_name);

// Auto-save timer (seconds; 0 disables). Fires UI_EVENT_AUTO_SAVE.
void ui_set_auto_save_timer(Window *window, int seconds);

// Session-recovery snapshot timer (seconds; 0 disables). Fires
// UI_EVENT_SESSION_SNAPSHOT.
void ui_set_session_timer(Window *window, int seconds);

// Launch a new npad instance to restore a specific recovery slot
// (used when several crashed sessions must each reopen in their own window).
// cascade_index staggers the new window's position so restored windows do
// not overlap.
void ui_launch_recovery_instance(const char *slot_id, int cascade_index);

// Notify other running npad instances that settings on disk changed, so
// they reload and re-apply them live.
void ui_notify_settings_changed(void);

// Whether the given pid is a still-running npad process. Used to skip the
// live recovery snapshots of other running instances at startup.
bool ui_pid_is_running(long pid);

// Platform-specific helpers (implemented per platform)
void *ui_get_native_handle(Window *window);

#endif // UI_INTERFACE_H