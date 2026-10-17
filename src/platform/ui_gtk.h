/*
 * npad - GTK3 UI (Linux, X11 and Wayland)
 * Private header shared by the ui_gtk*.c files. Nothing outside
 * src/platform/ui_gtk*.c includes this; the rest of npad talks to the
 * platform through ui_interface.h only.
 *
 * File layout:
 *   ui_gtk.c          - init and main loop, the main window, menus and
 *                       shortcuts, command dispatch, text access, undo, view
 *                       state (wrap, font, zoom, theme), status bar, timers
 *   ui_gtk_dialogs.c  - file choosers, message boxes and prompts, About
 *   ui_gtk_edit.c     - context menu, file drops, external-change prompt,
 *                       Ctrl+wheel zoom
 *   ui_gtk_find.c     - Find / Replace / Go To
 *   ui_gtk_prefs.c    - Preferences and Format > Font
 *   ui_gtk_instance.c - cross-instance messaging over D-Bus: settings sync,
 *                       view sync, Close All, instance counting
 *
 * Text model: the GtkTextBuffer always holds '\n' line breaks. ui_set_text
 * normalises whatever the file had, ui_get_text hands back '\n', and the core
 * converts to the document's own line ending when it saves. Positions inside
 * this layer are GTK character offsets, i.e. Unicode code points - the same
 * unit the core undo history uses.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef UI_GTK_H
#define UI_GTK_H

#include <gtk/gtk.h>
#include <stdbool.h>

#include "../core/undo.h"
#include "../ui_interface.h"

// Application id: the D-Bus name prefix, the Wayland app_id (via prgname) and
// the .desktop file name must all agree for the desktop to match windows to
// their launcher and icon
#define NPAD_APP_ID "io.github.platima.npad"

// Fallback faces. The Windows build defaults to Consolas / Segoe UI, which a
// Linux desktop does not have; these generic names resolve through fontconfig
// to whatever the desktop uses for its monospace and UI text.
#define NPAD_GTK_DEFAULT_MONO_FONT "Monospace"
#define NPAD_GTK_DEFAULT_PROP_FONT "Sans"

// Status bar segments, left to right (same order and meaning as Win32):
// [message/counts][Ln, Col][zoom][Mono/Prop][line ending][encoding]
enum {
    NPAD_STATUS_MESSAGE = 0,
    NPAD_STATUS_POSITION,
    NPAD_STATUS_ZOOM,
    NPAD_STATUS_FONT_MODE,
    NPAD_STATUS_EOL,
    NPAD_STATUS_ENCODING,
    NPAD_STATUS_PARTS
};

// Every menu command and shortcut. Mirrors the Win32 ID_* commands; the Win32
// resource ids are not reused because nothing here is a resource.
typedef enum {
    NPAD_CMD_NONE = 0,
    NPAD_CMD_FILE_NEW,
    NPAD_CMD_FILE_NEW_WINDOW,
    NPAD_CMD_FILE_OPEN,
    NPAD_CMD_FILE_SAVE,
    NPAD_CMD_FILE_SAVE_AS,
    NPAD_CMD_FILE_PAGE_SETUP,
    NPAD_CMD_FILE_PRINT_PREVIEW,
    NPAD_CMD_FILE_PRINT,
    NPAD_CMD_FILE_RECENT_CLEAR,
    NPAD_CMD_FILE_CLOSE,
    NPAD_CMD_FILE_CLOSE_ALL,
    NPAD_CMD_FILE_EXIT,
    NPAD_CMD_EDIT_UNDO,
    NPAD_CMD_EDIT_REDO,
    NPAD_CMD_EDIT_CUT,
    NPAD_CMD_EDIT_COPY,
    NPAD_CMD_EDIT_PASTE,
    NPAD_CMD_EDIT_DELETE,
    NPAD_CMD_EDIT_FIND,
    NPAD_CMD_EDIT_FIND_NEXT,
    NPAD_CMD_EDIT_FIND_PREV,
    NPAD_CMD_EDIT_REPLACE,
    NPAD_CMD_EDIT_GOTO_LINE,
    NPAD_CMD_EDIT_SELECT_ALL,
    NPAD_CMD_EDIT_TIME_DATE,
    NPAD_CMD_EDIT_PREFERENCES,
    NPAD_CMD_EDIT_PREFERENCES_DEBUG, // Ctrl+Shift+. : Preferences on the hidden Debug page
    NPAD_CMD_FORMAT_WORD_WRAP,
    NPAD_CMD_FORMAT_MONOSPACE,
    NPAD_CMD_FORMAT_EOL_CRLF,
    NPAD_CMD_FORMAT_EOL_LF,
    NPAD_CMD_FORMAT_EOL_CR,
    NPAD_CMD_FORMAT_EOL_CYCLE,
    NPAD_CMD_FORMAT_FONT,
    NPAD_CMD_VIEW_ZOOM_IN,
    NPAD_CMD_VIEW_ZOOM_OUT,
    NPAD_CMD_VIEW_ZOOM_RESET,
    NPAD_CMD_VIEW_STATUS_BAR,
    NPAD_CMD_HELP_ABOUT,
    NPAD_CMD_COUNT
} NpadCommand;

// An edit seen inside a GTK user action, held until the action ends so a
// compound edit (typing over a selection, a paste) lands as one undo step
typedef struct {
    bool insert;
    long pos;
    char *text;
    size_t len;
} NpadPendingEdit;

struct Window {
    GtkWidget *window;
    GtkWidget *menubar;
    GtkWidget *scroller;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GtkWidget *status_bar;
    GtkWidget *status_labels[NPAD_STATUS_PARTS];
    GtkAccelGroup *accel_group;
    GtkCssProvider *font_css;

    // Menu items whose label, check state or sensitivity changes
    GtkWidget *command_items[NPAD_CMD_COUNT];
    GtkWidget *recent_menu;
    bool syncing_menu; // Set while code (not the user) flips a check item

    // View state. Word wrap and the status bar are shared settings; font
    // type and zoom are per-window, seeded from the Defaults preferences.
    bool word_wrap_enabled;
    bool status_bar_visible;
    bool monospace_current;
    int zoom;           // Percent, 10..500
    double wheel_accum; // Smooth-scroll delta not yet worth a zoom step
    bool drag_ctrl;     // Ctrl was down while a file was dragged over the window

    // Document state
    bool is_modified;
    bool loading; // Text is being replaced programmatically: not an edit

    // Undo: GtkTextBuffer (GTK3) keeps no history of its own
    UndoStack *undo;
    int user_action_depth;
    GArray *pending;      // NpadPendingEdit, flushed at the end of a user action
    long expected_cursor; // Where the last edit left the caret; -1 = unknown

    // Geometry. Wayland gives clients neither their position nor a way to
    // set it, so the last requested position is remembered and reported back
    // unchanged - that keeps an X11 session's saved position intact.
    int normal_width;
    int normal_height;
    int requested_x;
    int requested_y;
    bool maximized;

    // Status bar contents
    char status_encoding[32];
    char status_line_ending[32];
    char status_message[160];
    char status_counts[160];

    // Timers (GLib source ids; 0 = not armed)
    guint auto_save_timer;
    guint session_timer;
    guint status_timer;
    guint counts_timer;
    guint startup_timer;
};

// The one document window of this process (npad is one process per window)
extern Window *g_gtk_main_window;

// --- ui_gtk.c --------------------------------------------------------------

// Run a menu command or shortcut
void npad_gtk_handle_command(Window *window, NpadCommand command);

// Show a transient message in the leftmost status segment; NULL clears it
void npad_gtk_set_status_message(Window *window, const char *message);

// Re-read settings.json and re-apply everything that can change live
// (another instance saved preferences)
void npad_gtk_reload_and_apply_settings(Window *window);

// Another instance changed its zoom / font type and view sync is on
void npad_gtk_apply_synced_view_state(Window *window, int zoom, bool monospace);

// Replace the selection with text as one undoable edit (Time/Date, and the
// ported Replace / list tools later)
void npad_gtk_replace_selection(Window *window, const char *text);

// Launch another npad process with extra arguments (NULL-terminated array,
// may be NULL)
void npad_gtk_spawn_self(const char *const *extra_args);

// The settings key (and fallback face) of the font the editor is showing:
// monospace_font, proportional_font or opendyslexic_font
const char *npad_gtk_active_font_key(const Window *window, const char **default_face);

// Whether fontconfig knows a font family by this name
bool npad_gtk_font_is_installed(const char *family);
// Change the zoom by delta percent (clamped; no-op at the limits)
void npad_gtk_zoom_by(Window *window, int delta);

// Text as the buffer wants it: '\n' line breaks and valid UTF-8 (invalid
// sequences repaired, not dropped). g_malloc'd; NULL only for NULL input.
char *npad_gtk_normalize_text(const char *text);

// --- ui_gtk_dialogs.c --------------------------------------------------------

// Nothing beyond the ui_platform_* entry points yet

// --- ui_gtk_edit.c -----------------------------------------------------------

// Context menu, file drops, external-change prompt, Ctrl+wheel zoom: connect
// them to a freshly built main window
void npad_gtk_edit_init(Window *window);

// Whether an edit command (Undo, Redo, Cut, Copy, Delete, Paste) can act right
// now - drives the greying of both the Edit menu and the context menu
bool npad_gtk_edit_command_enabled(Window *window, NpadCommand command);

// A file dropped on the window at (x, y), in text view widget coordinates:
// open it (with the save prompt), or with insert set put its text at that
// point as one undo step
void npad_gtk_edit_drop_file(Window *window, const char *path, bool insert, int x, int y);

// --- ui_gtk_find.c -----------------------------------------------------------

// Whether a search term exists, so Find Next can run without the dialog
bool npad_gtk_find_has_term(void);
// Search again from the selection; down = forwards
void npad_gtk_find_next(Window *window, bool down);
// Go To line dialog
void npad_gtk_show_goto_dialog(Window *window);
// Re-read the persisted find options (after a settings broadcast)
void npad_gtk_find_reload_settings(void);
// Close any open find/replace dialog (shutdown)
void npad_gtk_find_cleanup(void);

// --- ui_gtk_prefs.c ----------------------------------------------------------

// The modal Preferences dialog; show_debug adds the hidden Debug page and
// opens on it. Every control saves and broadcasts as it changes.
void npad_gtk_show_preferences(Window *window, bool show_debug);
// Format > Font: GtkFontChooserDialog writing the font keys for the font type
// this window is showing
void npad_gtk_show_font_dialog(Window *window);

// --- ui_gtk_instance.c -------------------------------------------------------

// Connect to the session bus, claim this instance's name and listen for the
// other instances. Returns false when there is no session bus; npad still
// runs, just without live sync and with every count reading as zero.
bool npad_gtk_instance_init(void);
void npad_gtk_instance_cleanup(void);

// Messages to every other npad instance on this desktop
void npad_gtk_instance_broadcast_settings_changed(void);
void npad_gtk_instance_broadcast_view_state(int zoom, bool monospace);
void npad_gtk_instance_broadcast_close_all(void);

// Number of OTHER npad instances on this desktop
int npad_gtk_instance_count_others(void);

// Whether pid is a running npad instance (bus first, /proc as fallback)
bool npad_gtk_instance_pid_is_npad(long pid);

#endif // UI_GTK_H
