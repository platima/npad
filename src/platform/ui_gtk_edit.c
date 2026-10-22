/*
 * npad - GTK3 UI: editing behaviours around the text view
 *
 * The context menu (npad's own, not GTK's stock one), dragging a file onto the
 * window, noticing that the open file changed on disk, and Ctrl+wheel zoom.
 * Each mirrors the Win32 build - show_context_menu, WM_DROPFILES,
 * prompt_external_change and RichEdit's native Ctrl+wheel zoom. Also the
 * text view's own key bindings that Notepad has no counterpart for (Insert's
 * overtype, Ctrl+. / Ctrl+; emoji), the undo boundaries of the edits GTK makes
 * by itself, and the clipboard state the Paste items are greyed from.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/editor.h"
#include "../core/file_ops.h"
#include "../core/settings.h"

// Ctrl+wheel zooms in the same 10% steps as Ctrl+Plus / Ctrl+Minus
#define NPAD_WHEEL_ZOOM_STEP 10

// Drag-and-drop target ids (anything but the text view's own 0)
enum { NPAD_DND_URI_LIST = 1 };

// ---------------------------------------------------------------------------
// What the clipboard holds
//
// Greying Paste needs to know whether the clipboard has text. Asking when a
// menu opens (gtk_clipboard_wait_is_text_available) runs a nested main loop
// until the owning program answers - a stall when it is slow, and redraws and
// timers running in the middle of opening the menu. Instead the answer is kept
// current: every owner-change starts an asynchronous targets request, and the
// menus read the cached result.
// ---------------------------------------------------------------------------

static GtkClipboard *edit_clipboard(Window *window) {
    return gtk_widget_get_clipboard(window->text_view, GDK_SELECTION_CLIPBOARD);
}

static GdkAtom html_target(void) {
    return gdk_atom_intern_static_string("text/html");
}

static void on_clipboard_targets(GtkClipboard *clipboard, GdkAtom *atoms, gint count,
                                 gpointer data) {
    (void) clipboard;
    (void) data;
    Window *window = g_gtk_main_window; // The request may outlive a window
    if (!window)
        return;
    bool html = false;
    for (gint i = 0; atoms && i < count && !html; i++)
        html = atoms[i] == html_target();
    window->clipboard_has_text = atoms && count > 0 && gtk_targets_include_text(atoms, count);
    window->clipboard_has_html = html;
    window->clipboard_known = true;
    // The Edit menu opened before the answer came: correct Paste in place
    GtkWidget *paste = window->command_items[NPAD_CMD_EDIT_PASTE];
    if (window->edit_menu_open && paste)
        gtk_widget_set_sensitive(paste, window->clipboard_has_text);
}

static void on_clipboard_owner_change(GtkClipboard *clipboard, GdkEvent *event, gpointer data) {
    (void) event;
    (void) data;
    gtk_clipboard_request_targets(clipboard, on_clipboard_targets, NULL);
}

static bool clipboard_cache_usable(Window *window) {
    return window->clipboard_known &&
           gdk_display_supports_selection_notification(gtk_widget_get_display(window->text_view));
}

bool npad_gtk_clipboard_has_text(Window *window) {
    if (!window)
        return false;
    if (clipboard_cache_usable(window))
        return window->clipboard_has_text;
    // A display that cannot report owner changes (or no answer yet): ask
    return gtk_clipboard_wait_is_text_available(edit_clipboard(window));
}

bool npad_gtk_clipboard_has_html(Window *window) {
    if (!window)
        return false;
    if (clipboard_cache_usable(window))
        return window->clipboard_has_html;
    return gtk_clipboard_wait_is_target_available(edit_clipboard(window), html_target());
}

void npad_gtk_clipboard_note_own_text(Window *window) {
    if (!window)
        return;
    // Known at once; the owner-change that follows confirms it
    window->clipboard_has_text = true;
    window->clipboard_has_html = false;
    window->clipboard_known = true;
}

static void setup_clipboard_tracking(Window *window) {
    GtkClipboard *clipboard = edit_clipboard(window);
    g_signal_connect(clipboard, "owner-change", G_CALLBACK(on_clipboard_owner_change), NULL);
    gtk_clipboard_request_targets(clipboard, on_clipboard_targets, NULL); // The state at start
}

// ---------------------------------------------------------------------------
// Edit command states (shared by the menu bar and the context menu)
// ---------------------------------------------------------------------------

// Whether an edit command can act right now, like notepad.exe: Cut / Copy /
// Delete need a selection, Paste needs text on the clipboard, Undo / Redo need
// history. Commands that are not state-dependent are always enabled.
bool npad_gtk_edit_command_enabled(Window *window, NpadCommand command) {
    if (!window)
        return false;
    switch (command) {
        case NPAD_CMD_EDIT_UNDO:
            return undo_can_undo(window->undo);
        case NPAD_CMD_EDIT_REDO:
            return undo_can_redo(window->undo);
        case NPAD_CMD_EDIT_CUT:
            // With the Markdown tools on, Ctrl+X without a selection cuts the
            // line, so Cut then only needs a non-empty document
            if (settings_get_bool("list_tools_enabled", false))
                return gtk_text_buffer_get_has_selection(window->buffer) ||
                       gtk_text_buffer_get_char_count(window->buffer) > 0;
            return gtk_text_buffer_get_has_selection(window->buffer);
        case NPAD_CMD_EDIT_COPY:
        case NPAD_CMD_EDIT_DELETE:
            return gtk_text_buffer_get_has_selection(window->buffer);
        case NPAD_CMD_EDIT_PASTE:
            return npad_gtk_clipboard_has_text(window);
        default:
            if (command >= NPAD_CMD_LIST_SORT_ASC || command == NPAD_CMD_EDIT_PASTE_MARKDOWN)
                return npad_gtk_lists_command_enabled(window, command);
            return true;
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

static void on_context_item_activate(GtkMenuItem *item, gpointer data) {
    (void) item;
    if (g_gtk_main_window)
        npad_gtk_handle_command(g_gtk_main_window, (NpadCommand) GPOINTER_TO_INT(data));
}

static void add_context_item(Window *window, GtkWidget *menu, const char *label,
                             NpadCommand command) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    gtk_widget_set_sensitive(item, npad_gtk_edit_command_enabled(window, command));
    g_signal_connect(item, "activate", G_CALLBACK(on_context_item_activate),
                     GINT_TO_POINTER(command));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void add_context_separator(GtkWidget *menu) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

// The Markdown list tools append here (a separator, then the items of the
// Markdown menu, mirrored) when the list_tools_enabled setting is on - exactly
// where Win32's show_context_menu calls populate_list_menu. The menu is built
// fresh for every popup, so their sensitivity is set as they are added.
static void extend_context_menu(Window *window, GtkWidget *menu) {
    npad_gtk_lists_populate_context_menu(window, menu);
}

// Replace GTK's stock text-view menu (Emoji, input methods, its own idea of
// Cut / Copy ...) with npad's, in the same order as the Win32 one. The signal
// fires for the mouse and for Shift+F10 / the Menu key alike, and GTK has
// already positioned the menu.
static void on_populate_popup(GtkTextView *view, GtkWidget *popup, gpointer data) {
    (void) view;
    Window *window = data;
    if (!GTK_IS_MENU(popup))
        return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(popup));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    add_context_item(window, popup, "_Undo", NPAD_CMD_EDIT_UNDO);
    add_context_item(window, popup, "_Redo", NPAD_CMD_EDIT_REDO);
    add_context_separator(popup);
    add_context_item(window, popup, "Cu_t", NPAD_CMD_EDIT_CUT);
    add_context_item(window, popup, "_Copy", NPAD_CMD_EDIT_COPY);
    add_context_item(window, popup, "_Paste", NPAD_CMD_EDIT_PASTE);
    add_context_item(window, popup, "_Delete", NPAD_CMD_EDIT_DELETE);
    add_context_separator(popup);
    add_context_item(window, popup, "Select _All", NPAD_CMD_EDIT_SELECT_ALL);
    extend_context_menu(window, popup);
    gtk_widget_show_all(popup);
}

// ---------------------------------------------------------------------------
// Dragging files onto the window
// ---------------------------------------------------------------------------

// Whether Ctrl is down. GDK offers two views of it and neither is reliable for
// every combination of display server and drag source, so ask both: the
// keyboard's modifier state, and the pointer's (which carries them on X11).
static bool ctrl_is_down(Window *window) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display)
        return false;
    GdkModifierType keys = gdk_keymap_get_modifier_state(gdk_keymap_get_for_display(display));
    GdkModifierType pointer = 0;
    GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
    GdkWindow *gdk = gtk_widget_get_window(window->text_view);
    if (device && gdk)
        gdk_device_get_state(device, gdk, NULL, &pointer);
    return ((keys | pointer) & GDK_CONTROL_MASK) != 0;
}

// Insert a file's text at (x, y) - widget coordinates, the drop point - as a
// single undo step. The binary, size and read checks are Open's, shared with
// Win32 (editor_read_file_for_insert), which also reports a failure.
static void insert_file_at(Window *window, const char *path, int x, int y) {
    char *content = editor_read_file_for_insert(path);
    if (!content)
        return;
    char *text = npad_gtk_normalize_text(content);
    free(content);
    if (!text)
        return;
    if (!npad_gtk_text_fits(text)) {
        g_free(text);
        char message[512];
        char *shown = npad_gtk_display_text(path); // The dialog takes UTF-8 only
        snprintf(message, sizeof(message), "Could not insert file: %.300s\nIt is too large.", shown);
        g_free(shown);
        ui_show_message_box(window, "npad", message, false);
        return;
    }

    if (text[0]) {
        GtkTextView *view = GTK_TEXT_VIEW(window->text_view);
        gint bx = 0, by = 0;
        GtkTextIter where;
        gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, x, y, &bx, &by);
        gtk_text_view_get_iter_at_location(view, &where, bx, by);

        // The caret goes to the drop point first (which also ends the typing
        // run in the undo history); the whole insert is one undo step, even
        // when the file holds a single character
        gtk_text_buffer_place_cursor(window->buffer, &where);
        undo_begin_group(window->undo);
        gtk_text_buffer_begin_user_action(window->buffer);
        gtk_text_buffer_insert_at_cursor(window->buffer, text, (gint) strlen(text));
        gtk_text_buffer_end_user_action(window->buffer);
        undo_end_group(window->undo);
        gtk_text_view_scroll_mark_onscreen(view, gtk_text_buffer_get_insert(window->buffer));
    }
    g_free(text);
}

static gboolean open_dropped_file_idle(gpointer data) {
    char *path = data;
    if (g_gtk_main_window)
        ui_post_event(UI_EVENT_FILE_DROPPED, g_gtk_main_window, path);
    g_free(path);
    return G_SOURCE_REMOVE;
}

void npad_gtk_edit_drop_file(Window *window, const char *path, bool insert, int x, int y) {
    if (!window || !path)
        return;
    if (insert)
        insert_file_at(window, path, x, y);
    else // After the drag has finished: the open may raise a save prompt
        g_idle_add(open_dropped_file_idle, g_strdup(path));
}

static gboolean on_drag_motion(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                               guint time, gpointer data) {
    (void) widget;
    (void) context;
    (void) x;
    (void) y;
    (void) time;
    // Remember Ctrl as the file travels over the window: by the drop the
    // source may already have swallowed the keyboard
    Window *window = data;
    window->drag_ctrl = ctrl_is_down(window);
    return FALSE; // Let the text view draw its own drop caret
}

// Only files are taken here. Dropped text carries no uri-list target and is
// inserted by GtkTextView itself, at the drop point, as always.
static void on_drag_data_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                  GtkSelectionData *selection, guint info, guint time,
                                  gpointer data) {
    Window *window = data;
    if (info != NPAD_DND_URI_LIST) {
        // Text, which GtkTextView inserts itself: a step of its own, even a
        // single dropped character (the far side is on_text_drop_done)
        undo_break(window->undo);
        return;
    }
    g_signal_stop_emission_by_name(widget, "drag-data-received");

    gchar **uris = gtk_selection_data_get_uris(selection);
    gchar *path = NULL;
    // The first local file, as Win32 takes only the first dropped file
    for (int i = 0; uris && uris[i] && !path; i++)
        path = g_filename_from_uri(uris[i], NULL, NULL);
    g_strfreev(uris);

    gtk_drag_finish(context, path != NULL, FALSE, time);
    if (path) {
        bool insert = window->drag_ctrl || ctrl_is_down(window);
        npad_gtk_edit_drop_file(window, path, insert, x, y);
        g_free(path);
    }
    window->drag_ctrl = false;
}

// After GtkTextView inserted dropped text: the typing that follows starts a
// new step. (Not reached for a file drop, whose emission stops above.)
static void on_text_drop_done(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                              GtkSelectionData *selection, guint info, guint time, gpointer data) {
    (void) widget;
    (void) context;
    (void) x;
    (void) y;
    (void) selection;
    (void) info;
    (void) time;
    Window *window = data;
    undo_break(window->undo);
}

static void setup_file_drop(Window *window) {
    GtkWidget *view = window->text_view;
    // The text view is already a destination for text; add files to it. A
    // destination on the window itself would never be asked, because the text
    // view covers it and takes the drop for itself.
    GtkTargetList *targets = gtk_drag_dest_get_target_list(view);
    if (!targets)
        return;
    gtk_target_list_add_uri_targets(targets, NPAD_DND_URI_LIST);
    gtk_drag_dest_set_target_list(view, targets);
    g_signal_connect(view, "drag-motion", G_CALLBACK(on_drag_motion), window);
    g_signal_connect(view, "drag-data-received", G_CALLBACK(on_drag_data_received), window);
    g_signal_connect_after(view, "drag-data-received", G_CALLBACK(on_text_drop_done), window);
}

// ---------------------------------------------------------------------------
// Ctrl + mouse wheel zoom
// ---------------------------------------------------------------------------

static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void) widget;
    Window *window = data;
    if ((event->state & gtk_accelerator_get_default_mod_mask()) != GDK_CONTROL_MASK) {
        window->wheel_accum = 0;
        return FALSE; // Plain scrolling
    }

    int steps = 0;
    switch (event->direction) {
        case GDK_SCROLL_UP:
            steps = 1;
            break;
        case GDK_SCROLL_DOWN:
            steps = -1;
            break;
        case GDK_SCROLL_SMOOTH: {
            // High-resolution wheels and touchpads deliver many small deltas.
            // Collect them and zoom one step per whole unit, so a notch of an
            // ordinary wheel (delta 1) is one step and a touchpad swipe is
            // not a runaway.
            if (gdk_event_is_scroll_stop_event((GdkEvent *) event)) {
                window->wheel_accum = 0;
                return TRUE;
            }
            double dx = 0, dy = 0;
            gdk_event_get_scroll_deltas((GdkEvent *) event, &dx, &dy);
            if (dy * window->wheel_accum > 0) // Reversed direction: start over
                window->wheel_accum = 0;
            window->wheel_accum -= dy; // Scrolling up (negative) zooms in
            steps = (int) window->wheel_accum;
            window->wheel_accum -= steps;
            break;
        }
        default:
            return FALSE; // Sideways
    }
    if (steps != 0)
        npad_gtk_zoom_by(window, steps * NPAD_WHEEL_ZOOM_STEP);
    return TRUE; // Zoomed (or waiting for more): never also scroll the text
}

// ---------------------------------------------------------------------------
// The open file changed on disk
// ---------------------------------------------------------------------------

enum { EXT_RELOAD = 101, EXT_SAVE_AS = 102, EXT_KEEP = 103 };

static void on_choice_clicked(GtkButton *button, gpointer dialog) {
    gtk_dialog_response(GTK_DIALOG(dialog),
                        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "npad-response")));
}

// A button in the style of the Win32 command links: the action, then a
// smaller line saying what it does
static GtkWidget *add_choice_button(GtkWidget *dialog, GtkWidget *box, const char *label,
                                    const char *detail, int response) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new_with_mnemonic(label);
    GtkWidget *sub = gtk_label_new(detail);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(sub), 0.0f);
    gtk_label_set_mnemonic_widget(GTK_LABEL(title), button);
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "dim-label");
    gtk_box_pack_start(GTK_BOX(column), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(column), sub, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(column), 4);
    gtk_container_add(GTK_CONTAINER(button), column);
    g_signal_connect(button, "clicked", G_CALLBACK(on_choice_clicked), dialog);
    g_object_set_data(G_OBJECT(button), "npad-response", GINT_TO_POINTER(response));
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    return button;
}

// Something else changed the file npad has open. Offer the three things a user
// can sensibly want and make the safe one the default: Reload discards their
// unsaved edits, so it is never what Enter or Esc does.
//
// Re-entrancy matters. This runs on window activation, and every button leads
// to a modal dialog of its own whose closing re-activates the window - without
// the guard it would ask again at once, forever.
static void prompt_external_change(Window *window) {
    static bool in_prompt = false;
    if (!window || in_prompt)
        return;

    EditorExternalChange change = editor_check_external_change();
    if (change == EDITOR_FILE_UNCHANGED)
        return;
    in_prompt = true;
    bool deleted = (change == EDITOR_FILE_DELETED);

    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 14);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-warning", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *instruction = gtk_label_new(deleted ? "This file has been deleted or moved"
                                                   : "This file has changed on disk");
    PangoAttrList *bold = pango_attr_list_new();
    pango_attr_list_insert(bold, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(instruction), bold);
    pango_attr_list_unref(bold);
    GtkWidget *detail = gtk_label_new(
        deleted ? "Something else removed it since npad opened it. Your text is unaffected "
                  "until you save or close."
                : "Something else has modified it since npad opened it.");
    gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(detail), 52);
    gtk_label_set_xalign(GTK_LABEL(instruction), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
    gtk_box_pack_start(GTK_BOX(text), instruction, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), detail, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), text, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), head, FALSE, FALSE, 0);

    GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    if (!deleted)
        add_choice_button(dialog, choices, "_Reload from disk", "Discards your unsaved changes",
                          EXT_RELOAD);
    add_choice_button(dialog, choices, "_Save as a different file", "Keeps your version",
                      EXT_SAVE_AS);
    GtkWidget *keep = add_choice_button(dialog, choices, "_Keep editing",
                                        "Decide later; nothing is written", EXT_KEEP);
    gtk_box_pack_start(GTK_BOX(content), choices, FALSE, FALSE, 0);
    GtkWidget *stop = gtk_check_button_new_with_label("Stop telling me about this file");
    gtk_box_pack_start(GTK_BOX(content), stop, FALSE, FALSE, 0);

    gtk_widget_show_all(content);
    // Keep editing starts focused: the only choice that writes nothing
    gtk_window_set_focus(GTK_WINDOW(dialog), keep);

    gint choice = gtk_dialog_run(GTK_DIALOG(dialog));
    bool suppress = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stop));
    gtk_widget_destroy(dialog);

    if (suppress)
        editor_suppress_change_prompt();

    switch (choice) {
        case EXT_RELOAD:
            // Declined or failed (the core has said why): treat it as Keep
            // editing, or the same prompt returns on the next activation
            if (!editor_reload_from_disk())
                editor_accept_external_change();
            break;
        case EXT_SAVE_AS:
            ui_post_event(UI_EVENT_FILE_SAVE_AS, window, NULL);
            // Cancelling Save As is "decide later" too. If it saved, the file
            // is now a different one with a fresh baseline and this is a no-op.
            editor_accept_external_change();
            break;
        default:
            // Keep editing, Esc or the close box: adopt what is on disk as the
            // new baseline so the same change is not reported on every
            // activation from here on
            editor_accept_external_change();
            break;
    }
    in_prompt = false;
}

static gboolean check_external_change_idle(gpointer data) {
    Window *window = data;
    if (window == g_gtk_main_window)
        prompt_external_change(window);
    return G_SOURCE_REMOVE;
}

// Coming to the foreground is when the user would notice a file changed
// underneath them anyway, and it costs nothing while npad is not in front -
// unlike polling. Deferred to idle so the activation completes first and the
// dialog has an active window to belong to.
static void on_active_changed(GObject *object, GParamSpec *pspec, gpointer data) {
    (void) pspec;
    if (gtk_window_is_active(GTK_WINDOW(object)))
        g_idle_add(check_external_change_idle, data);
}

// ---------------------------------------------------------------------------
// Scroll bars that stay put
// ---------------------------------------------------------------------------

// A bar that is always on screen but has nothing to scroll is drawn inert -
// the slider greyed, nothing to drag - which is Notepad's disabled gutter.
// (GTK would otherwise show a full-length slider that looks live.)
static void sync_scrollbar_state(GtkAdjustment *adjustment, gpointer bar) {
    gboolean scrollable =
        gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_lower(adjustment) >
        gtk_adjustment_get_page_size(adjustment);
    if (gtk_widget_get_sensitive(GTK_WIDGET(bar)) != scrollable)
        gtk_widget_set_sensitive(GTK_WIDGET(bar), scrollable);
}

static void setup_inert_scrollbars(Window *window) {
    GtkScrolledWindow *scroller = GTK_SCROLLED_WINDOW(window->scroller);
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(scroller);
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(scroller);
    GtkWidget *vbar = gtk_scrolled_window_get_vscrollbar(scroller);
    GtkWidget *hbar = gtk_scrolled_window_get_hscrollbar(scroller);
    g_signal_connect(vadj, "changed", G_CALLBACK(sync_scrollbar_state), vbar);
    g_signal_connect(hadj, "changed", G_CALLBACK(sync_scrollbar_state), hbar);
    sync_scrollbar_state(vadj, vbar);
    sync_scrollbar_state(hadj, hbar);
}

// ---------------------------------------------------------------------------
// The text view's own bindings
// ---------------------------------------------------------------------------

// Insert (and keypad Insert) toggles GtkTextView's overtype mode, which
// Notepad does not have: a stray press would silently start overwriting text,
// with nothing on screen saying so. Shift+Insert and Ctrl+Insert (paste and
// copy) are separate bindings and keep working.
static void on_toggle_overwrite(GtkTextView *view, gpointer data) {
    (void) data;
    g_signal_stop_emission_by_name(view, "toggle-overwrite");
}

// Ctrl+. and Ctrl+; open GTK's emoji chooser; in Notepad they do nothing
static void on_insert_emoji(GtkTextView *view, gpointer data) {
    (void) data;
    g_signal_stop_emission_by_name(view, "insert-emoji");
}

// Shift+Insert, the text view's own paste binding, goes the way Ctrl+V does:
// one undo step, '\n' line breaks
static void on_paste_binding(GtkTextView *view, gpointer data) {
    g_signal_stop_emission_by_name(view, "paste-clipboard");
    npad_gtk_paste_plain(data);
}

// Shift+Delete (the text view's cut binding) runs GTK's cut, one edit: keep
// it from merging with the edits on either side. Breaks rather than a group,
// because a group left open by a handler that never ran would swallow every
// later edit.
static bool g_cut_had_selection = false;

static void on_cut_binding(GtkTextView *view, gpointer data) {
    (void) view;
    Window *window = data;
    undo_break(window->undo);
    g_cut_had_selection = gtk_text_buffer_get_has_selection(window->buffer);
}

static void on_cut_binding_done(GtkTextView *view, gpointer data) {
    (void) view;
    Window *window = data;
    undo_break(window->undo);
    if (g_cut_had_selection)
        npad_gtk_clipboard_note_own_text(window);
    g_cut_had_selection = false;
}

// A middle-click paste of the PRIMARY selection is GTK's own, asynchronous
// paste: break before it here, and after it on the buffer's paste-done
static gboolean on_button_press(GtkWidget *widget, const GdkEventButton *event, gpointer data) {
    (void) widget;
    Window *window = data;
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_MIDDLE)
        undo_break(window->undo);
    return FALSE;
}

static void on_paste_done(GtkTextBuffer *buffer, GtkClipboard *clipboard, gpointer data) {
    (void) buffer;
    (void) clipboard;
    Window *window = data;
    undo_break(window->undo);
}

// A signal by name, if this GTK has it (insert-emoji arrived during 3.22)
static void connect_if_present(GtkWidget *widget, const char *signal, GCallback handler,
                               gpointer data, bool after) {
    if (!g_signal_lookup(signal, G_OBJECT_TYPE(widget)))
        return;
    if (after)
        g_signal_connect_after(widget, signal, handler, data);
    else
        g_signal_connect(widget, signal, handler, data);
}

static void setup_text_view_bindings(Window *window) {
    GtkWidget *view = window->text_view;
    gtk_text_view_set_overwrite(GTK_TEXT_VIEW(view), FALSE);
    connect_if_present(view, "toggle-overwrite", G_CALLBACK(on_toggle_overwrite), window, false);
    connect_if_present(view, "insert-emoji", G_CALLBACK(on_insert_emoji), window, false);
    connect_if_present(view, "paste-clipboard", G_CALLBACK(on_paste_binding), window, false);
    connect_if_present(view, "cut-clipboard", G_CALLBACK(on_cut_binding), window, false);
    connect_if_present(view, "cut-clipboard", G_CALLBACK(on_cut_binding_done), window, true);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_button_press), window);
    g_signal_connect(window->buffer, "paste-done", G_CALLBACK(on_paste_done), window);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void npad_gtk_edit_init(Window *window) {
    g_signal_connect(window->text_view, "populate-popup", G_CALLBACK(on_populate_popup), window);
    g_signal_connect(window->scroller, "scroll-event", G_CALLBACK(on_scroll_event), window);
    g_signal_connect(window->window, "notify::is-active", G_CALLBACK(on_active_changed), window);
    setup_file_drop(window);
    setup_inert_scrollbars(window);
    setup_text_view_bindings(window);
    setup_clipboard_tracking(window);
}
