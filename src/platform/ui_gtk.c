/*
 * npad - GTK3 UI (Linux, X11 and Wayland)
 * The main window: menus and shortcuts, the text view, undo, view state,
 * status bar and timers, plus the ui_platform_* entry points behind
 * ui_interface.h. See ui_gtk.h for how the GTK layer is split across files.
 *
 * One binary serves both display servers: GDK picks X11 or Wayland at
 * runtime. The only behaviour that differs is window placement, which
 * Wayland reserves for the compositor.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#define _DEFAULT_SOURCE // setsid(), nl_langinfo()

#include "ui_gtk.h"

#include <glib-unix.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../core/editor.h"
#include "../core/error.h"
#include "../core/file_ops.h"
#include "../core/settings.h"
#include "../core/startup_prof.h"
#include "print_gtk.h"

// Status refreshes are coalesced: caret movement and scrolling fire per
// step, and recomputing Ln/Col on every one is wasted work (Win32: 33 ms)
#define NPAD_STATUS_DELAY_MS 33
// Live word/char/line counts: coalesced while typing, debounced instead on
// very large documents where a full rescan per burst is too costly
#define NPAD_COUNTS_COALESCE_MS 120
#define NPAD_COUNTS_DEBOUNCE_MS 350
#define NPAD_COUNTS_LIVE_LIMIT 1000000
// Deferred startup work (crash-recovery scan) runs after the first paint
#define NPAD_STARTUP_DELAY_MS 50
// Undo history bound. Typing coalesces per word, so this is a lot of editing.
#define NPAD_UNDO_STEPS 10000

#define NPAD_ZOOM_MIN 10
#define NPAD_ZOOM_MAX 500

Window *g_gtk_main_window = NULL;

static bool g_dark_mode = false;
static gboolean g_initial_prefer_dark = FALSE; // The desktop's own choice
static gchar *g_applied_css = NULL;            // What the text view's provider holds

static void update_status_bar(Window *window);
static void schedule_status_update(Window *window);
static void schedule_counts_update(Window *window);
static void apply_counts_pref(Window *window);
static void apply_view_css(Window *window);
static void set_zoom(Window *window, int percent);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static int clamp_int(int value, int low, int high) {
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

// Only X11 lets a client place its own top-level window
static bool display_allows_positioning(void) {
    GdkDisplay *display = gdk_display_get_default();
    return display && strcmp(G_OBJECT_TYPE_NAME(display), "GdkX11Display") == 0;
}

// Cancel a GLib timer if armed, leaving its id at 0 ("not armed")
static void stop_timer(guint *timer) {
    if (*timer) {
        g_source_remove(*timer);
        *timer = 0;
    }
}

static void get_iter_at(Window *window, GtkTextIter *iter, long offset) {
    gtk_text_buffer_get_iter_at_offset(window->buffer, iter, (gint) offset);
}

static long cursor_offset(Window *window) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(window->buffer, &iter,
                                     gtk_text_buffer_get_insert(window->buffer));
    return gtk_text_iter_get_offset(&iter);
}

static void scroll_to_cursor(Window *window) {
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window->text_view),
                                       gtk_text_buffer_get_insert(window->buffer));
}

// Group digits with the locale's thousands separator ("12,345")
static void format_count(unsigned long value, char *out, size_t size) {
    char raw[24];
    snprintf(raw, sizeof(raw), "%lu", value);
    const struct lconv *lc = localeconv();
    const char *sep = (lc && lc->thousands_sep && lc->thousands_sep[0]) ? lc->thousands_sep : ",";
    size_t digits = strlen(raw);
    size_t sep_len = strlen(sep);
    size_t pos = 0;
    for (size_t i = 0; i < digits && pos + sep_len + 1 < size; i++) {
        if (i > 0 && (digits - i) % 3 == 0) {
            memcpy(out + pos, sep, sep_len);
            pos += sep_len;
        }
        out[pos++] = raw[i];
    }
    out[pos] = '\0';
}

// ---------------------------------------------------------------------------
// Undo
//
// GtkTextBuffer in GTK3 has no undo, so every edit is recorded into the core
// history (core/undo.c) from the buffer's own signals, and undo/redo replay
// it through undo_target. Recording happens BEFORE the default handlers run,
// so the positions are the ones the edit started at.
//
// GTK wraps each interactive edit in a "user action": a keystroke, a paste,
// typing over a selection (delete + insert). Edits inside one are held and
// flushed together at its end - one edit is recorded plainly so typing can
// still coalesce per word, several become one undo group.
// ---------------------------------------------------------------------------

static void record_edit(Window *window, bool insert, long pos, const char *text, size_t len) {
    if (window->user_action_depth > 0) {
        NpadPendingEdit edit = { insert, pos, g_strndup(text, len), len };
        g_array_append_val(window->pending, edit);
        return;
    }
    if (insert)
        undo_record_insert(window->undo, pos, text, len);
    else
        undo_record_delete(window->undo, pos, text, len);
}

static void clear_pending(Window *window) {
    for (guint i = 0; i < window->pending->len; i++)
        g_free(g_array_index(window->pending, NpadPendingEdit, i).text);
    g_array_set_size(window->pending, 0);
}

static void flush_pending(Window *window) {
    guint count = window->pending->len;
    if (count == 0)
        return;
    if (count > 1)
        undo_begin_group(window->undo);
    for (guint i = 0; i < count; i++) {
        const NpadPendingEdit *edit = &g_array_index(window->pending, NpadPendingEdit, i);
        if (edit->insert)
            undo_record_insert(window->undo, edit->pos, edit->text, edit->len);
        else
            undo_record_delete(window->undo, edit->pos, edit->text, edit->len);
    }
    if (count > 1)
        undo_end_group(window->undo);
    clear_pending(window);
}

static void on_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len,
                           gpointer data) {
    (void) buffer;
    Window *window = data;
    if (window->loading || undo_is_applying(window->undo) || len <= 0)
        return;
    long pos = gtk_text_iter_get_offset(location);
    record_edit(window, true, pos, text, (size_t) len);
    window->expected_cursor = pos + g_utf8_strlen(text, len);
}

static void on_delete_range(GtkTextBuffer *buffer, const GtkTextIter *start, const GtkTextIter *end,
                            gpointer data) {
    Window *window = data;
    if (window->loading || undo_is_applying(window->undo))
        return;
    GtkTextIter a = *start;
    GtkTextIter b = *end;
    gtk_text_iter_order(&a, &b);
    gchar *text = gtk_text_buffer_get_text(buffer, &a, &b, TRUE);
    if (text && text[0]) {
        long pos = gtk_text_iter_get_offset(&a);
        record_edit(window, false, pos, text, strlen(text));
        window->expected_cursor = pos;
    }
    g_free(text);
}

static void on_begin_user_action(GtkTextBuffer *buffer, gpointer data) {
    (void) buffer;
    Window *window = data;
    window->user_action_depth++;
}

static void on_end_user_action(GtkTextBuffer *buffer, gpointer data) {
    (void) buffer;
    Window *window = data;
    if (window->user_action_depth > 0)
        window->user_action_depth--;
    if (window->user_action_depth == 0)
        flush_pending(window);
}

// The caret moved somewhere an edit did not put it (a click, an arrow key, a
// selection): typing there must start a new undo step
static void on_cursor_moved(GObject *object, GParamSpec *pspec, gpointer data) {
    (void) object;
    (void) pspec;
    Window *window = data;
    long now = cursor_offset(window);
    if (now != window->expected_cursor) {
        undo_break(window->undo);
        window->expected_cursor = now;
    }
    schedule_status_update(window);
}

static void undo_target_insert(void *ctx, long pos, const char *text, size_t len) {
    Window *window = ctx;
    GtkTextIter iter;
    get_iter_at(window, &iter, pos);
    gtk_text_buffer_insert(window->buffer, &iter, text, (gint) len);
}

static void undo_target_remove(void *ctx, long pos, long count) {
    Window *window = ctx;
    GtkTextIter start, end;
    get_iter_at(window, &start, pos);
    get_iter_at(window, &end, pos + count);
    gtk_text_buffer_delete(window->buffer, &start, &end);
}

static const UndoTarget k_undo_target = { undo_target_insert, undo_target_remove };

static void undo_apply(Window *window, bool redo) {
    if (!window)
        return;
    long sel_start = 0, sel_end = 0;
    bool done = redo ? undo_redo(window->undo, &k_undo_target, window, &sel_start, &sel_end)
                     : undo_undo(window->undo, &k_undo_target, window, &sel_start, &sel_end);
    if (!done)
        return;
    GtkTextIter start, end;
    get_iter_at(window, &start, sel_start);
    get_iter_at(window, &end, sel_end);
    window->expected_cursor = sel_end;
    // Caret at the end of the restored text, the rest of it selected
    gtk_text_buffer_select_range(window->buffer, &end, &start);
    scroll_to_cursor(window);
}

// ---------------------------------------------------------------------------
// View state: font, zoom, colour scheme, word wrap, status bar
// ---------------------------------------------------------------------------

static bool font_is_installed(const char *family) {
    PangoFontMap *map = pango_cairo_font_map_get_default();
    PangoFontFamily **families = NULL;
    int count = 0;
    pango_font_map_list_families(map, &families, &count);
    bool found = false;
    for (int i = 0; i < count && !found; i++) {
        found = g_ascii_strcasecmp(pango_font_family_get_name(families[i]), family) == 0;
    }
    g_free(families);
    return found;
}

// The settings key (and fallback face) for the font the editor is showing.
// Same keys as Win32, so one preference model serves both builds.
static const char *active_font_key(const Window *window, const char **default_face) {
    if (settings_get_bool("opendyslexic_enabled", false) && font_is_installed("OpenDyslexic")) {
        *default_face = "OpenDyslexic";
        return "opendyslexic_font";
    }
    if (!window || window->monospace_current) {
        *default_face = NPAD_GTK_DEFAULT_MONO_FONT;
        return "monospace_font";
    }
    *default_face = NPAD_GTK_DEFAULT_PROP_FONT;
    return "proportional_font";
}

// The colour scheme's text colours, NULL for "system" (the GTK theme's own).
// Same values as the Win32 theme_colors.
static void scheme_colors(const char *theme, const char **bg, const char **fg, bool *dark) {
    *bg = NULL;
    *fg = NULL;
    *dark = false;
    if (strcmp(theme, "dark") == 0) {
        *bg = "#1e1e1e";
        *fg = "#dcdcdc";
        *dark = true;
    } else if (strcmp(theme, "solarized-light") == 0) {
        *bg = "#fdf6e3";
        *fg = "#657b83";
    } else if (strcmp(theme, "solarized-dark") == 0) {
        *bg = "#002b36";
        *fg = "#839496";
        *dark = true;
    } else if (strcmp(theme, "system") == 0) {
        *dark = g_initial_prefer_dark != FALSE;
    } else { // "light", the default: classic Notepad has no schemes
        *bg = "#ffffff";
        *fg = "#000000";
    }
}

// Font, zoom and colours all live in one CSS provider on the text view.
// Zoom scales the point size, which is what RichEdit's EM_SETZOOM amounts to.
static void apply_view_css(Window *window) {
    if (!window || !window->font_css)
        return;

    const char *default_face = NPAD_GTK_DEFAULT_MONO_FONT;
    const char *key = active_font_key(window, &default_face);
    char *face = settings_get_string(key, default_face);
    int size = clamp_int(settings_get_int("font_size", 11), 6, 72);
    int weight = clamp_int(settings_get_int("font_weight", 400), 100, 900);
    bool italic = settings_get_bool("font_italic", false);

    // A face name goes inside a CSS string: drop anything that could end it
    GString *family = g_string_new(NULL);
    for (const char *p = face ? face : default_face; *p; p++) {
        if (*p != '"' && *p != '\\' && *p != '\n' && *p != ';' && *p != '}')
            g_string_append_c(family, *p);
    }
    free(face);

    // g_ascii_dtostr, not printf: a locale with a decimal comma would write
    // "10,5pt", which CSS rejects - silently, leaving the old font in place
    char points[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(points, sizeof(points), size * window->zoom / 100.0);

    char *theme = settings_get_string("theme", "light");
    const char *bg = NULL;
    const char *fg = NULL;
    bool dark = false;
    scheme_colors(theme ? theme : "light", &bg, &fg, &dark);
    free(theme);

    gchar *css;
    if (bg && fg) {
        css = g_strdup_printf("textview, textview text { font-family: \"%s\"; font-size: %spt; "
                              "font-weight: %d; font-style: %s; }\n"
                              "textview text { background-color: %s; color: %s; }\n",
                              family->str, points, weight, italic ? "italic" : "normal", bg, fg);
    } else {
        css = g_strdup_printf("textview, textview text { font-family: \"%s\"; font-size: %spt; "
                              "font-weight: %d; font-style: %s; }\n",
                              family->str, points, weight, italic ? "italic" : "normal");
    }
    // Settings broadcasts re-run this in idle windows, and reloading the
    // provider restyles - and reflows - the whole document even when nothing
    // changed (LESSONS.md, "Idle windows are not idle"). Only load a change.
    if (g_strcmp0(css, g_applied_css) != 0) {
        gtk_css_provider_load_from_data(window->font_css, css, -1, NULL);
        g_free(g_applied_css);
        g_applied_css = css;
    } else {
        g_free(css);
    }
    g_string_free(family, TRUE);
}

// Colour scheme: the text colours come from apply_view_css; the window chrome
// follows via GTK's dark variant, so a dark scheme does not sit inside a
// light frame
static void apply_theme(Window *window) {
    char *theme = settings_get_string("theme", "light");
    const char *bg = NULL;
    const char *fg = NULL;
    bool dark = false;
    scheme_colors(theme ? theme : "light", &bg, &fg, &dark);
    free(theme);
    g_dark_mode = dark;
    // Setting the property re-themes every widget even when the value is
    // unchanged, so only set a change
    gboolean current = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &current, NULL);
    if ((current != FALSE) != dark)
        g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                     dark ? TRUE : FALSE, NULL);
    apply_view_css(window);
}

static void set_check(Window *window, NpadCommand command, bool active) {
    GtkWidget *item = window->command_items[command];
    if (!item)
        return;
    // set_active emits "activate", which would run the command again
    window->syncing_menu = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
    window->syncing_menu = false;
}

static void apply_word_wrap(Window *window) {
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(window->text_view),
                                window->word_wrap_enabled ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(window->scroller),
        window->word_wrap_enabled ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    set_check(window, NPAD_CMD_FORMAT_WORD_WRAP, window->word_wrap_enabled);
}

static void set_status_bar_visible(Window *window, bool visible) {
    window->status_bar_visible = visible;
    settings_set_bool("status_bar_visible", visible);
    gtk_widget_set_visible(window->status_bar, visible);
    set_check(window, NPAD_CMD_VIEW_STATUS_BAR, visible);
    update_status_bar(window);
    apply_counts_pref(window);
}

// A user-initiated zoom or font type change in this window. Honours the two
// opt-in preferences: make it the default for new windows, and mirror it live
// in the other windows.
static void on_view_state_changed(Window *window) {
    if (settings_get_bool("auto_update_defaults", false)) {
        settings_set_bool("default_font_mono", window->monospace_current);
        settings_set_int("default_zoom", window->zoom);
        settings_save();
        ui_notify_settings_changed();
    }
    if (settings_get_bool("sync_view_state", false))
        npad_gtk_instance_broadcast_view_state(window->zoom, window->monospace_current);
}

static void set_zoom(Window *window, int percent) {
    window->zoom = clamp_int(percent, NPAD_ZOOM_MIN, NPAD_ZOOM_MAX);
    apply_view_css(window);
    update_status_bar(window);
    on_view_state_changed(window);
}

static void toggle_monospace(Window *window) {
    window->monospace_current = !window->monospace_current;
    apply_view_css(window);
    set_check(window, NPAD_CMD_FORMAT_MONOSPACE, window->monospace_current);
    update_status_bar(window);
    on_view_state_changed(window);
}

void npad_gtk_apply_synced_view_state(Window *window, int zoom, bool monospace) {
    if (!window || !settings_get_bool("sync_view_state", false))
        return;
    // Applied directly, not through set_zoom: re-broadcasting would echo
    window->zoom = clamp_int(zoom, NPAD_ZOOM_MIN, NPAD_ZOOM_MAX);
    window->monospace_current = monospace;
    apply_view_css(window);
    set_check(window, NPAD_CMD_FORMAT_MONOSPACE, monospace);
    update_status_bar(window);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

static void set_status_part(Window *window, int part, const char *text) {
    GtkLabel *label = GTK_LABEL(window->status_labels[part]);
    // Skip identical text: the caret path calls this constantly
    if (strcmp(gtk_label_get_text(label), text) != 0)
        gtk_label_set_text(label, text);
}

// Part 0 shows the optional counts first (a stable position) and any
// transient message beside them
static void compose_status_message(Window *window) {
    char text[340];
    if (window->status_counts[0] && window->status_message[0])
        snprintf(text, sizeof(text), "%s    %s", window->status_counts, window->status_message);
    else
        snprintf(text, sizeof(text), "%s",
                 window->status_counts[0] ? window->status_counts : window->status_message);
    set_status_part(window, NPAD_STATUS_MESSAGE, text);
}

void npad_gtk_set_status_message(Window *window, const char *message) {
    if (!window)
        return;
    snprintf(window->status_message, sizeof(window->status_message), "%s", message ? message : "");
    compose_status_message(window);
}

static void update_status_bar(Window *window) {
    if (!window || !window->status_bar || !window->status_bar_visible)
        return;
    int line = 0, column = 0;
    ui_get_cursor_line_column(window, &line, &column);
    char text[64];
    snprintf(text, sizeof(text), "Ln %d, Col %d", line, column);
    set_status_part(window, NPAD_STATUS_POSITION, text);
    snprintf(text, sizeof(text), "%d%%", window->zoom);
    set_status_part(window, NPAD_STATUS_ZOOM, text);
    set_status_part(window, NPAD_STATUS_FONT_MODE, window->monospace_current ? "Mono" : "Prop");
    set_status_part(window, NPAD_STATUS_EOL, window->status_line_ending);
    set_status_part(window, NPAD_STATUS_ENCODING, window->status_encoding);
}

static gboolean on_status_timer(gpointer data) {
    Window *window = data;
    window->status_timer = 0;
    update_status_bar(window);
    return G_SOURCE_REMOVE;
}

static void schedule_status_update(Window *window) {
    if (!window || window->status_timer)
        return;
    window->status_timer = g_timeout_add(NPAD_STATUS_DELAY_MS, on_status_timer, window);
}

static void update_text_counts(Window *window) {
    if (!window->status_bar_visible || !settings_get_bool("status_show_counts", false))
        return;
    char *text = ui_get_text(window);
    size_t words = 0, chars = 0, lines = 0;
    file_count_text_stats(text, &words, &chars, &lines);
    g_free(text);
    char w[32], c[32], l[32];
    format_count((unsigned long) words, w, sizeof(w));
    format_count((unsigned long) chars, c, sizeof(c));
    format_count((unsigned long) lines, l, sizeof(l));
    snprintf(window->status_counts, sizeof(window->status_counts), "%s words, %s chars, %s lines",
             w, c, l);
    compose_status_message(window);
}

static gboolean on_counts_timer(gpointer data) {
    Window *window = data;
    window->counts_timer = 0;
    update_text_counts(window);
    return G_SOURCE_REMOVE;
}

static void schedule_counts_update(Window *window) {
    if (!window || !settings_get_bool("status_show_counts", false))
        return;
    if (gtk_text_buffer_get_char_count(window->buffer) > NPAD_COUNTS_LIVE_LIMIT) {
        // Debounce: re-arm on every change so the rescan waits for a pause
        if (window->counts_timer)
            g_source_remove(window->counts_timer);
        window->counts_timer = g_timeout_add(NPAD_COUNTS_DEBOUNCE_MS, on_counts_timer, window);
        return;
    }
    if (!window->counts_timer)
        window->counts_timer = g_timeout_add(NPAD_COUNTS_COALESCE_MS, on_counts_timer, window);
}

static void apply_counts_pref(Window *window) {
    if (settings_get_bool("status_show_counts", false)) {
        update_text_counts(window);
    } else {
        window->status_counts[0] = '\0';
        compose_status_message(window);
    }
}

static void on_popup_activate(GtkMenuItem *item, gpointer data);

// Popup for the line-ending segment (mirrors Format > Line Endings)
static void show_line_ending_popup(Window *window, GtkWidget *anchor) {
    static const char *labels[] = { "_Windows (CRLF)", "_Unix (LF)", "_Mac (CR)" };
    GtkWidget *menu = gtk_menu_new();
    GSList *group = NULL;
    for (int i = 0; i < 3; i++) {
        GtkWidget *item = gtk_radio_menu_item_new_with_mnemonic(group, labels[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                       (int) editor_get_line_ending() == i);
        g_object_set_data(G_OBJECT(item), "npad-kind", GINT_TO_POINTER(1));
        g_object_set_data(G_OBJECT(item), "npad-value", GINT_TO_POINTER(i));
        g_signal_connect(item, "activate", G_CALLBACK(on_popup_activate), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                             NULL);
}

// Popup for the encoding segment
static void show_encoding_popup(Window *window, GtkWidget *anchor) {
    GtkWidget *menu = gtk_menu_new();
    GSList *group = NULL;
    for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++) {
        GtkWidget *item =
            gtk_radio_menu_item_new_with_label(group, file_encoding_name((TextEncoding) i));
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), (int) editor_get_encoding() == i);
        g_object_set_data(G_OBJECT(item), "npad-kind", GINT_TO_POINTER(2));
        g_object_set_data(G_OBJECT(item), "npad-value", GINT_TO_POINTER(i));
        g_signal_connect(item, "activate", G_CALLBACK(on_popup_activate), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                             NULL);
}

static void on_popup_activate(GtkMenuItem *item, gpointer data) {
    (void) data;
    // Radio items also "activate" as they are switched off: act on the new one
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
        return;
    int kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "npad-kind"));
    int value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "npad-value"));
    if (kind == 1 && value != (int) editor_get_line_ending())
        editor_set_line_ending((LineEnding) value);
    else if (kind == 2 && value != (int) editor_get_encoding())
        editor_set_encoding((TextEncoding) value);
}

// Clicks on the status segments: Ln/Col -> Go To, zoom -> reset, font mode ->
// toggle monospace, line ending / encoding -> pickers (as on Win32)
static gboolean on_status_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 1)
        return FALSE;
    Window *window = g_gtk_main_window;
    if (!window)
        return FALSE;
    switch (GPOINTER_TO_INT(data)) {
        case NPAD_STATUS_POSITION:
            npad_gtk_show_goto_dialog(window);
            break;
        case NPAD_STATUS_ZOOM:
            set_zoom(window, 100);
            break;
        case NPAD_STATUS_FONT_MODE:
            toggle_monospace(window);
            break;
        case NPAD_STATUS_EOL:
            show_line_ending_popup(window, widget);
            break;
        case NPAD_STATUS_ENCODING:
            show_encoding_popup(window, widget);
            break;
    }
    return TRUE;
}

static GtkWidget *create_status_bar(Window *window) {
    // Minimum widths in characters, matching the Win32 part widths
    static const int widths[NPAD_STATUS_PARTS] = { 0, 16, 5, 5, 14, 12 };

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "statusbar");

    for (int part = 0; part < NPAD_STATUS_PARTS; part++) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);
        window->status_labels[part] = label;

        if (part == NPAD_STATUS_MESSAGE) {
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_box_pack_start(GTK_BOX(bar), label, TRUE, TRUE, 0);
            continue;
        }
        gtk_label_set_width_chars(GTK_LABEL(label), widths[part]);
        gtk_box_pack_start(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE,
                           0);
        GtkWidget *box = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(box), label);
        g_signal_connect(box, "button-press-event", G_CALLBACK(on_status_click),
                         GINT_TO_POINTER(part));
        gtk_box_pack_start(GTK_BOX(bar), box, FALSE, FALSE, 0);
    }
    return bar;
}

// ---------------------------------------------------------------------------
// Menus and shortcuts
// ---------------------------------------------------------------------------

// Commands whose Linux implementation has not been written yet. Their menu
// items stay greyed out and their shortcuts do nothing. Delete a case here
// when the feature lands - LINUX_PORT.md tracks what is left.
static bool command_available(NpadCommand command) {
    switch (command) {
        case NPAD_CMD_EDIT_PREFERENCES:
        case NPAD_CMD_FORMAT_FONT:
            return false;
        default:
            return true;
    }
}

static void on_menu_activate(GtkMenuItem *item, gpointer data) {
    Window *window = g_gtk_main_window;
    if (!window || window->syncing_menu)
        return;
    // A radio item also activates as it is switched off; only the newly
    // selected one is the user's choice
    if (GTK_IS_RADIO_MENU_ITEM(item) && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
        return;
    npad_gtk_handle_command(window, (NpadCommand) GPOINTER_TO_INT(data));
}

static GtkWidget *attach_item(Window *window, GtkWidget *menu, GtkWidget *item, NpadCommand command,
                              guint key, GdkModifierType mods) {
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_activate), GINT_TO_POINTER(command));
    if (key)
        gtk_widget_add_accelerator(item, "activate", window->accel_group, key, mods,
                                   GTK_ACCEL_VISIBLE);
    if (!command_available(command))
        gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    window->command_items[command] = item;
    return item;
}

static GtkWidget *add_item(Window *window, GtkWidget *menu, const char *label, NpadCommand command,
                           guint key, GdkModifierType mods) {
    return attach_item(window, menu, gtk_menu_item_new_with_mnemonic(label), command, key, mods);
}

static GtkWidget *add_check(Window *window, GtkWidget *menu, const char *label, NpadCommand command,
                            guint key, GdkModifierType mods) {
    return attach_item(window, menu, gtk_check_menu_item_new_with_mnemonic(label), command, key,
                       mods);
}

static void add_separator(GtkWidget *menu) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

static GtkWidget *add_submenu(GtkWidget *shell, const char *label) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(shell), item);
    return menu;
}

// Show a shortcut beside an item without binding it. Del must reach the text
// view itself (deleting the next character), so it cannot be an accelerator.
static void show_accel_only(GtkWidget *item, guint key, GdkModifierType mods) {
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (GTK_IS_ACCEL_LABEL(child))
        gtk_accel_label_set_accel(GTK_ACCEL_LABEL(child), key, mods);
}

static gboolean on_hidden_accel(GtkAccelGroup *group, GObject *acceleratable, guint key,
                                GdkModifierType mods, gpointer data) {
    (void) group;
    (void) acceleratable;
    (void) key;
    (void) mods;
    NpadCommand command = (NpadCommand) GPOINTER_TO_INT(data);
    if (g_gtk_main_window && command_available(command))
        npad_gtk_handle_command(g_gtk_main_window, command);
    return TRUE;
}

// A shortcut with no menu item of its own (or a second key for one)
static void add_hidden_accel(Window *window, guint key, GdkModifierType mods, NpadCommand command) {
    GClosure *closure = g_cclosure_new(G_CALLBACK(on_hidden_accel), GINT_TO_POINTER(command), NULL);
    gtk_accel_group_connect(window->accel_group, key, mods, 0, closure);
}

// Ctrl+N and Ctrl+Shift+N swap between New and New Window per the
// ctrl_n_new_window preference
static void apply_new_window_pref(Window *window) {
    GtkWidget *item_new = window->command_items[NPAD_CMD_FILE_NEW];
    GtkWidget *item_window = window->command_items[NPAD_CMD_FILE_NEW_WINDOW];
    if (!item_new || !item_window)
        return;
    const GdkModifierType ctrl = GDK_CONTROL_MASK;
    const GdkModifierType ctrl_shift = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
    // Which assignment is installed: -1 none yet, 0 default, 1 swapped.
    // Tracked because removing an accelerator that is not there warns.
    static int installed = -1;
    bool swapped = settings_get_bool("ctrl_n_new_window", false);
    if (installed == (swapped ? 1 : 0))
        return;
    if (installed >= 0) {
        gtk_widget_remove_accelerator(item_new, window->accel_group, GDK_KEY_n,
                                      installed ? ctrl_shift : ctrl);
        gtk_widget_remove_accelerator(item_window, window->accel_group, GDK_KEY_n,
                                      installed ? ctrl : ctrl_shift);
    }
    installed = swapped ? 1 : 0;
    gtk_widget_add_accelerator(item_new, "activate", window->accel_group, GDK_KEY_n,
                               swapped ? ctrl_shift : ctrl, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(item_window, "activate", window->accel_group, GDK_KEY_n,
                               swapped ? ctrl : ctrl_shift, GTK_ACCEL_VISIBLE);
}

static void on_recent_activate(GtkMenuItem *item, gpointer data) {
    Window *window = data;
    const char *path = g_object_get_data(G_OBJECT(item), "npad-path");
    if (path)
        ui_post_event(UI_EVENT_FILE_DROPPED, window, (void *) path);
}

// Rebuild File > Recent Files from settings (every time File opens)
static void rebuild_recent_menu(Window *window) {
    GtkWidget *menu = window->recent_menu;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    int count = 0;
    char **files = settings_get_recent_files(&count);
    if (count == 0) {
        GtkWidget *empty = gtk_menu_item_new_with_label("(Empty)");
        gtk_widget_set_sensitive(empty, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty);
    } else {
        for (int i = 0; i < count && i < 9; i++) {
            // "_1 path", with the path's own underscores doubled so they are
            // shown rather than taken as mnemonics
            GString *label = g_string_new(NULL);
            g_string_append_printf(label, "_%d ", i + 1);
            for (const char *p = files[i]; *p; p++) {
                if (*p == '_')
                    g_string_append_c(label, '_');
                g_string_append_c(label, *p);
            }
            GtkWidget *item = gtk_menu_item_new_with_mnemonic(label->str);
            g_string_free(label, TRUE);
            g_object_set_data_full(G_OBJECT(item), "npad-path", g_strdup(files[i]), g_free);
            g_signal_connect(item, "activate", G_CALLBACK(on_recent_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
        add_separator(menu);
        GtkWidget *clear = gtk_menu_item_new_with_mnemonic("_Clear Recent Files");
        g_signal_connect(clear, "activate", G_CALLBACK(on_menu_activate),
                         GINT_TO_POINTER(NPAD_CMD_FILE_RECENT_CLEAR));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear);
    }
    if (files)
        settings_free_recent_files(files, count);
    gtk_widget_show_all(menu);
}

static void set_command_sensitive(Window *window, NpadCommand command, bool sensitive) {
    if (window->command_items[command])
        gtk_widget_set_sensitive(window->command_items[command],
                                 sensitive && command_available(command));
}

// Grey out the edit commands that cannot act right now, like Notepad. Only
// while the menu is open: an insensitive item's accelerator is dead, so the
// states are restored as soon as it closes (see on_menu_hide).
static void apply_edit_command_states(Window *window) {
    bool has_selection = gtk_text_buffer_get_has_selection(window->buffer);
    bool has_text = gtk_text_buffer_get_char_count(window->buffer) > 0;
    GtkClipboard *clipboard = gtk_widget_get_clipboard(window->text_view, GDK_SELECTION_CLIPBOARD);
    bool can_paste = gtk_clipboard_wait_is_text_available(clipboard);

    set_command_sensitive(window, NPAD_CMD_EDIT_UNDO, undo_can_undo(window->undo));
    set_command_sensitive(window, NPAD_CMD_EDIT_REDO, undo_can_redo(window->undo));
    set_command_sensitive(window, NPAD_CMD_EDIT_CUT, has_selection);
    set_command_sensitive(window, NPAD_CMD_EDIT_COPY, has_selection);
    set_command_sensitive(window, NPAD_CMD_EDIT_DELETE, has_selection);
    set_command_sensitive(window, NPAD_CMD_EDIT_PASTE, can_paste);
    set_command_sensitive(window, NPAD_CMD_EDIT_FIND, has_text);
    set_command_sensitive(window, NPAD_CMD_EDIT_FIND_NEXT, has_text && npad_gtk_find_has_term());
    set_command_sensitive(window, NPAD_CMD_EDIT_FIND_PREV, has_text && npad_gtk_find_has_term());
    set_command_sensitive(window, NPAD_CMD_EDIT_REPLACE, has_text);
}

static const NpadCommand k_edit_state_commands[] = {
    NPAD_CMD_EDIT_UNDO,      NPAD_CMD_EDIT_REDO,    NPAD_CMD_EDIT_CUT,  NPAD_CMD_EDIT_COPY,
    NPAD_CMD_EDIT_DELETE,    NPAD_CMD_EDIT_PASTE,   NPAD_CMD_EDIT_FIND, NPAD_CMD_EDIT_FIND_NEXT,
    NPAD_CMD_EDIT_FIND_PREV, NPAD_CMD_EDIT_REPLACE,
};

static void on_menu_show(GtkWidget *menu, gpointer data) {
    Window *window = g_gtk_main_window;
    if (!window)
        return;
    switch (GPOINTER_TO_INT(data)) {
        case 0: // File
            rebuild_recent_menu(window);
            break;
        case 1: // Edit
            apply_edit_command_states(window);
            break;
        case 2: // Format
            set_check(window, NPAD_CMD_FORMAT_MONOSPACE, window->monospace_current);
            set_check(window, NPAD_CMD_FORMAT_EOL_CRLF + (int) editor_get_line_ending(), true);
            break;
    }
    (void) menu;
}

static void on_menu_hide(GtkWidget *menu, gpointer data) {
    (void) menu;
    Window *window = g_gtk_main_window;
    if (!window || GPOINTER_TO_INT(data) != 1)
        return;
    for (size_t i = 0; i < G_N_ELEMENTS(k_edit_state_commands); i++)
        set_command_sensitive(window, k_edit_state_commands[i], true);
}

static GtkWidget *add_top_menu(GtkWidget *menubar, const char *label, int id) {
    GtkWidget *menu = add_submenu(menubar, label);
    g_signal_connect(menu, "show", G_CALLBACK(on_menu_show), GINT_TO_POINTER(id));
    g_signal_connect(menu, "hide", G_CALLBACK(on_menu_hide), GINT_TO_POINTER(id));
    return menu;
}

static void create_menu(Window *window) {
    const GdkModifierType C = GDK_CONTROL_MASK;
    const GdkModifierType CS = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
    const GdkModifierType S = GDK_SHIFT_MASK;
    const GdkModifierType NONE = 0;
    GtkWidget *bar = gtk_menu_bar_new();
    window->menubar = bar;

    GtkWidget *file = add_top_menu(bar, "_File", 0);
    add_item(window, file, "_New", NPAD_CMD_FILE_NEW, 0, NONE);
    add_item(window, file, "New _Window", NPAD_CMD_FILE_NEW_WINDOW, 0, NONE);
    add_item(window, file, "_Open...", NPAD_CMD_FILE_OPEN, GDK_KEY_o, C);
    add_separator(file);
    add_item(window, file, "_Save", NPAD_CMD_FILE_SAVE, GDK_KEY_s, C);
    add_item(window, file, "Save _As...", NPAD_CMD_FILE_SAVE_AS, GDK_KEY_s, CS);
    add_separator(file);
    add_item(window, file, "Page Set_up...", NPAD_CMD_FILE_PAGE_SETUP, 0, NONE);
    add_item(window, file, "Print Pre_view...", NPAD_CMD_FILE_PRINT_PREVIEW, GDK_KEY_p, CS);
    add_item(window, file, "_Print...", NPAD_CMD_FILE_PRINT, GDK_KEY_p, C);
    add_separator(file);
    window->recent_menu = add_submenu(file, "_Recent Files");
    add_separator(file);
    add_item(window, file, "_Close", NPAD_CMD_FILE_CLOSE, GDK_KEY_w, C);
    add_item(window, file, "Close A_ll Windows", NPAD_CMD_FILE_CLOSE_ALL, GDK_KEY_w, CS);
    add_separator(file);
    add_item(window, file, "E_xit", NPAD_CMD_FILE_EXIT, 0, NONE);
    apply_new_window_pref(window);

    GtkWidget *edit = add_top_menu(bar, "_Edit", 1);
    add_item(window, edit, "_Undo", NPAD_CMD_EDIT_UNDO, GDK_KEY_z, C);
    add_item(window, edit, "_Redo", NPAD_CMD_EDIT_REDO, GDK_KEY_y, C);
    add_separator(edit);
    add_item(window, edit, "Cu_t", NPAD_CMD_EDIT_CUT, GDK_KEY_x, C);
    add_item(window, edit, "_Copy", NPAD_CMD_EDIT_COPY, GDK_KEY_c, C);
    add_item(window, edit, "_Paste", NPAD_CMD_EDIT_PASTE, GDK_KEY_v, C);
    show_accel_only(add_item(window, edit, "De_lete", NPAD_CMD_EDIT_DELETE, 0, NONE),
                    GDK_KEY_Delete, NONE);
    add_separator(edit);
    add_item(window, edit, "_Find...", NPAD_CMD_EDIT_FIND, GDK_KEY_f, C);
    add_item(window, edit, "Find _Next", NPAD_CMD_EDIT_FIND_NEXT, GDK_KEY_F3, NONE);
    add_item(window, edit, "Find Pre_vious", NPAD_CMD_EDIT_FIND_PREV, GDK_KEY_F3, S);
    add_item(window, edit, "R_eplace...", NPAD_CMD_EDIT_REPLACE, GDK_KEY_h, C);
    add_item(window, edit, "_Go To...", NPAD_CMD_EDIT_GOTO_LINE, GDK_KEY_g, C);
    add_separator(edit);
    add_item(window, edit, "Select _All", NPAD_CMD_EDIT_SELECT_ALL, GDK_KEY_a, C);
    add_item(window, edit, "Time/_Date", NPAD_CMD_EDIT_TIME_DATE, GDK_KEY_F5, NONE);
    add_separator(edit);
    // "Preference_s": P already belongs to Paste in this popup (LESSONS.md)
    add_item(window, edit, "Preference_s...", NPAD_CMD_EDIT_PREFERENCES, GDK_KEY_comma, C);

    GtkWidget *format = add_top_menu(bar, "F_ormat", 2);
    add_check(window, format, "_Word Wrap", NPAD_CMD_FORMAT_WORD_WRAP, GDK_KEY_z, GDK_MOD1_MASK);
    add_check(window, format, "_Monospace", NPAD_CMD_FORMAT_MONOSPACE, GDK_KEY_m, C);
    add_separator(format);
    GtkWidget *eol = add_submenu(format, "_Line Endings");
    GSList *group = NULL;
    static const char *eol_labels[] = { "_Windows (CRLF)", "_Unix (LF)", "_Mac (CR)" };
    for (int i = 0; i < 3; i++) {
        GtkWidget *item = gtk_radio_menu_item_new_with_mnemonic(group, eol_labels[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        attach_item(window, eol, item, NPAD_CMD_FORMAT_EOL_CRLF + i, 0, NONE);
    }
    add_separator(format);
    add_item(window, format, "_Font...", NPAD_CMD_FORMAT_FONT, 0, NONE);

    GtkWidget *view = add_top_menu(bar, "_View", 3);
    GtkWidget *zoom = add_submenu(view, "_Zoom");
    add_item(window, zoom, "Zoom _In", NPAD_CMD_VIEW_ZOOM_IN, GDK_KEY_plus, C);
    add_item(window, zoom, "Zoom _Out", NPAD_CMD_VIEW_ZOOM_OUT, GDK_KEY_minus, C);
    add_item(window, zoom, "_Restore Default Zoom", NPAD_CMD_VIEW_ZOOM_RESET, GDK_KEY_0, C);
    add_check(window, view, "_Status Bar", NPAD_CMD_VIEW_STATUS_BAR, 0, NONE);

    GtkWidget *help = add_top_menu(bar, "_Help", 4);
    // No Check for Updates: on Linux npad updates through the package manager
    add_item(window, help, "_About npad", NPAD_CMD_HELP_ABOUT, 0, NONE);

    // Shortcuts without a menu item of their own, or second keys for one
    add_hidden_accel(window, GDK_KEY_e, C, NPAD_CMD_FORMAT_EOL_CYCLE);
    add_hidden_accel(window, GDK_KEY_equal, C, NPAD_CMD_VIEW_ZOOM_IN);
    add_hidden_accel(window, GDK_KEY_KP_Add, C, NPAD_CMD_VIEW_ZOOM_IN);
    add_hidden_accel(window, GDK_KEY_KP_Subtract, C, NPAD_CMD_VIEW_ZOOM_OUT);
    add_hidden_accel(window, GDK_KEY_KP_0, C, NPAD_CMD_VIEW_ZOOM_RESET);

    set_check(window, NPAD_CMD_FORMAT_WORD_WRAP, window->word_wrap_enabled);
    set_check(window, NPAD_CMD_FORMAT_MONOSPACE, window->monospace_current);
    set_check(window, NPAD_CMD_VIEW_STATUS_BAR, window->status_bar_visible);
    rebuild_recent_menu(window);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

// Print and Print Preview: hand the document, not the view, to print_gtk.c,
// in the font this window is showing (zoom does not apply to paper)
static void print_document(Window *window, bool preview) {
    const char *default_face = NPAD_GTK_DEFAULT_MONO_FONT;
    const char *key = active_font_key(window, &default_face);
    char *face = settings_get_string(key, default_face);
    char *text = ui_get_text(window);
    const char *path = editor_get_current_file();
    char *title = path ? g_path_get_basename(path) : NULL;

    NpadPrintDoc doc = {
        .text = text,
        .title = title,
        .font_family = face,
        .font_size = clamp_int(settings_get_int("font_size", 11), 6, 72),
        .font_weight = clamp_int(settings_get_int("font_weight", 400), 100, 900),
        .font_italic = settings_get_bool("font_italic", false),
    };
    npad_gtk_print_run(GTK_WINDOW(window->window), &doc, preview);

    g_free(title);
    g_free(text);
    free(face);
}

// Notepad's F5 stamp: the locale's short time (no seconds) then short date
static void insert_time_date(Window *window) {
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local))
        return;

    // The locale's time format minus its seconds field, e.g. "%I:%M:%S %p" ->
    // "%I:%M %p" (and "%H:%M:%S" -> "%H:%M" in the C locale)
    char time_fmt[64];
    const char *src = nl_langinfo(T_FMT);
    size_t out = 0;
    for (size_t i = 0; src[i] && out + 1 < sizeof(time_fmt); i++) {
        if ((src[i] == ':' || src[i] == '.') && src[i + 1] == '%' && src[i + 2] == 'S') {
            i += 2;
            continue;
        }
        time_fmt[out++] = src[i];
    }
    time_fmt[out] = '\0';

    char time_text[64] = "";
    char date_text[64] = "";
    if (time_fmt[0])
        strftime(time_text, sizeof(time_text), time_fmt, &local);
    strftime(date_text, sizeof(date_text), "%x", &local);

    char stamp[132];
    snprintf(stamp, sizeof(stamp), "%s %s", time_text, date_text);
    npad_gtk_replace_selection(window, stamp);
}

void npad_gtk_replace_selection(Window *window, const char *text) {
    if (!window || !text)
        return;
    gtk_text_buffer_begin_user_action(window->buffer);
    gtk_text_buffer_delete_selection(window->buffer, TRUE, TRUE);
    gtk_text_buffer_insert_at_cursor(window->buffer, text, -1);
    gtk_text_buffer_end_user_action(window->buffer);
    scroll_to_cursor(window);
}

void npad_gtk_spawn_self(const char *const *extra_args) {
    gchar *exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe)
        return;
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, exe);
    for (const char *const *a = extra_args; a && *a; a++)
        g_ptr_array_add(argv, (gpointer) *a);
    g_ptr_array_add(argv, NULL);

    GError *error = NULL;
    if (!g_spawn_async(NULL, (gchar **) argv->pdata, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
                       &error)) {
        g_warning("npad: could not start another window: %s", error ? error->message : "");
        g_clear_error(&error);
    }
    g_ptr_array_free(argv, TRUE);
    g_free(exe);
}

static void launch_new_window(void) {
    // Cascade past every window already open, this one included
    char cascade[16];
    snprintf(cascade, sizeof(cascade), "%d", npad_gtk_instance_count_others() + 1);
    const char *args[] = { "--cascade", cascade, NULL };
    npad_gtk_spawn_self(args);
}

void npad_gtk_handle_command(Window *window, NpadCommand command) {
    if (!window || !command_available(command))
        return;

    switch (command) {
        case NPAD_CMD_FILE_NEW:
            ui_post_event(UI_EVENT_FILE_NEW, window, NULL);
            break;
        case NPAD_CMD_FILE_NEW_WINDOW:
            launch_new_window();
            break;
        case NPAD_CMD_FILE_OPEN:
            ui_post_event(UI_EVENT_FILE_OPEN, window, NULL);
            break;
        case NPAD_CMD_FILE_SAVE:
            ui_post_event(UI_EVENT_FILE_SAVE, window, NULL);
            break;
        case NPAD_CMD_FILE_SAVE_AS:
            ui_post_event(UI_EVENT_FILE_SAVE_AS, window, NULL);
            break;
        case NPAD_CMD_FILE_PAGE_SETUP:
            npad_gtk_print_page_setup(GTK_WINDOW(window->window));
            break;
        case NPAD_CMD_FILE_PRINT:
        case NPAD_CMD_FILE_PRINT_PREVIEW:
            print_document(window, command == NPAD_CMD_FILE_PRINT_PREVIEW);
            break;
        case NPAD_CMD_FILE_RECENT_CLEAR:
            settings_clear_recent_files();
            settings_save();
            rebuild_recent_menu(window);
            break;
        case NPAD_CMD_FILE_CLOSE:
        case NPAD_CMD_FILE_EXIT:
            // One document per window, so Close is this window's
            // save-checked close, exactly like Exit
            ui_post_event(UI_EVENT_QUIT, window, NULL);
            break;
        case NPAD_CMD_FILE_CLOSE_ALL:
            // Every other window runs its own save prompt; then this one
            npad_gtk_instance_broadcast_close_all();
            ui_post_event(UI_EVENT_QUIT, window, NULL);
            break;
        case NPAD_CMD_EDIT_UNDO:
            ui_post_event(UI_EVENT_EDIT_UNDO, window, NULL);
            break;
        case NPAD_CMD_EDIT_REDO:
            ui_post_event(UI_EVENT_EDIT_REDO, window, NULL);
            break;
        case NPAD_CMD_EDIT_CUT:
            ui_post_event(UI_EVENT_EDIT_CUT, window, NULL);
            break;
        case NPAD_CMD_EDIT_COPY:
            ui_post_event(UI_EVENT_EDIT_COPY, window, NULL);
            break;
        case NPAD_CMD_EDIT_PASTE:
            ui_post_event(UI_EVENT_EDIT_PASTE, window, NULL);
            break;
        case NPAD_CMD_EDIT_DELETE:
            gtk_text_buffer_delete_selection(window->buffer, TRUE, TRUE);
            break;
        case NPAD_CMD_EDIT_FIND:
            ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            break;
        case NPAD_CMD_EDIT_FIND_NEXT:
        case NPAD_CMD_EDIT_FIND_PREV:
            if (npad_gtk_find_has_term())
                npad_gtk_find_next(window, command == NPAD_CMD_EDIT_FIND_NEXT);
            else
                ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            break;
        case NPAD_CMD_EDIT_REPLACE:
            ui_post_event(UI_EVENT_EDIT_REPLACE, window, NULL);
            break;
        case NPAD_CMD_EDIT_GOTO_LINE:
            npad_gtk_show_goto_dialog(window);
            break;
        case NPAD_CMD_EDIT_SELECT_ALL:
            ui_post_event(UI_EVENT_EDIT_SELECT_ALL, window, NULL);
            break;
        case NPAD_CMD_EDIT_TIME_DATE:
            insert_time_date(window);
            break;
        case NPAD_CMD_FORMAT_WORD_WRAP:
            window->word_wrap_enabled = !window->word_wrap_enabled;
            settings_set_bool("word_wrap", window->word_wrap_enabled);
            apply_word_wrap(window);
            settings_save(); // Persist and propagate to the other windows
            ui_notify_settings_changed();
            break;
        case NPAD_CMD_FORMAT_MONOSPACE:
            toggle_monospace(window);
            break;
        case NPAD_CMD_FORMAT_EOL_CRLF:
        case NPAD_CMD_FORMAT_EOL_LF:
        case NPAD_CMD_FORMAT_EOL_CR: {
            LineEnding eol = (LineEnding) (command - NPAD_CMD_FORMAT_EOL_CRLF);
            if (eol != editor_get_line_ending())
                editor_set_line_ending(eol);
            break;
        }
        case NPAD_CMD_FORMAT_EOL_CYCLE:
            editor_set_line_ending((LineEnding) ((editor_get_line_ending() + 1) % 3));
            break;
        case NPAD_CMD_VIEW_ZOOM_IN:
            set_zoom(window, window->zoom + 10);
            break;
        case NPAD_CMD_VIEW_ZOOM_OUT:
            set_zoom(window, window->zoom - 10);
            break;
        case NPAD_CMD_VIEW_ZOOM_RESET:
            set_zoom(window, 100);
            break;
        case NPAD_CMD_VIEW_STATUS_BAR:
            set_status_bar_visible(window, !window->status_bar_visible);
            settings_save(); // Persist and propagate to the other windows
            ui_notify_settings_changed();
            break;
        case NPAD_CMD_HELP_ABOUT:
            ui_show_about_dialog(window);
            break;
        default:
            break;
    }
}

void npad_gtk_reload_and_apply_settings(Window *window) {
    if (!window)
        return;
    settings_clear_all();
    settings_load();
    editor_reload_prefs(); // Auto-save / session settings and timers
    npad_gtk_find_reload_settings();
    apply_theme(window); // Also re-applies font and zoom
    apply_new_window_pref(window);

    // Shared window options follow; per-window view state (font type, zoom)
    // deliberately does not
    bool wrap = settings_get_bool("word_wrap", false);
    if (wrap != window->word_wrap_enabled) {
        window->word_wrap_enabled = wrap;
        apply_word_wrap(window);
    }
    bool status = settings_get_bool("status_bar_visible", true);
    if (status != window->status_bar_visible)
        set_status_bar_visible(window, status);
    apply_counts_pref(window);
    update_status_bar(window);
}

// ---------------------------------------------------------------------------
// Window events
// ---------------------------------------------------------------------------

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void) widget;
    (void) event;
    // The editor decides (prompting to save) and calls ui_quit() when the
    // window may close; the window itself is never destroyed from here
    ui_post_event(UI_EVENT_QUIT, data, NULL);
    return TRUE;
}

static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void) event;
    Window *window = data;
    // Remember the restored size so a maximised window persists its normal
    // geometry, as GetWindowPlacement does on Win32
    if (!window->maximized)
        gtk_window_get_size(GTK_WINDOW(widget), &window->normal_width, &window->normal_height);
    return FALSE;
}

static gboolean on_window_state_event(GtkWidget *widget, GdkEventWindowState *event,
                                      gpointer data) {
    (void) widget;
    Window *window = data;
    window->maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    return FALSE;
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
    (void) buffer;
    Window *window = data;
    // A transient message lasts until the text changes
    if (window->status_message[0])
        npad_gtk_set_status_message(window, NULL);
    schedule_counts_update(window);
    schedule_status_update(window);
    if (!window->loading) {
        window->is_modified = true;
        ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
    }
}

static gboolean on_auto_save_timer(gpointer data) {
    ui_post_event(UI_EVENT_AUTO_SAVE, data, NULL);
    return G_SOURCE_CONTINUE;
}

static gboolean on_session_timer(gpointer data) {
    ui_post_event(UI_EVENT_SESSION_SNAPSHOT, data, NULL);
    return G_SOURCE_CONTINUE;
}

static gboolean on_startup_timer(gpointer data) {
    Window *window = data;
    window->startup_timer = 0;
    startup_prof_mark("deferred tasks");
    ui_post_event(UI_EVENT_STARTUP_DEFERRED, window, NULL);
    return G_SOURCE_REMOVE;
}

// Logout or `kill`: the Linux counterpart of WM_ENDSESSION. Park unsaved
// work without prompting - a dialog nobody will see would only stall the
// session ending until npad is killed outright - then quit. A failed park is
// not a reason to stay: refusing would just get npad killed with nothing
// saved at all.
static gboolean on_terminate_signal(gpointer data) {
    (void) data;
    Window *window = g_gtk_main_window;
    if (!window || !ui_post_event(UI_EVENT_QUIT_HANDOFF, window, NULL))
        ui_quit();
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// ui_platform_*: lifecycle
// ---------------------------------------------------------------------------

bool ui_platform_init(void) {
    // Before GTK starts: the program name becomes the Wayland app_id and the
    // X11 WM_CLASS, which is how the desktop finds npad's launcher and icon
    g_set_prgname(NPAD_APP_ID);
    g_set_application_name("npad");

    if (!gtk_init_check(NULL, NULL)) {
        fprintf(stderr, "npad: cannot open a display - is a desktop session running?\n");
        return false;
    }

    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 &g_initial_prefer_dark, NULL);

    npad_gtk_instance_init(); // Optional: npad runs without a session bus
    g_unix_signal_add(SIGTERM, on_terminate_signal, NULL);
    return true;
}

void ui_platform_cleanup(void) {
    npad_gtk_find_cleanup();
    npad_gtk_print_cleanup();
    if (g_gtk_main_window)
        ui_destroy_window(g_gtk_main_window);
    npad_gtk_instance_cleanup();
}

int ui_platform_message_loop(void) {
    gtk_main();
    return 0;
}

void ui_platform_quit(void) {
    if (gtk_main_level() > 0)
        gtk_main_quit();
}

// ---------------------------------------------------------------------------
// ui_platform_*: window management
// ---------------------------------------------------------------------------

Window *ui_platform_create_main_window(void) {
    Window *window = calloc(1, sizeof(Window));
    if (!window)
        return NULL;

    snprintf(window->status_encoding, sizeof(window->status_encoding), "UTF-8");
    snprintf(window->status_line_ending, sizeof(window->status_line_ending), "Windows (CRLF)");
    window->word_wrap_enabled = settings_get_bool("word_wrap", false);
    window->status_bar_visible = settings_get_bool("status_bar_visible", true);
    // Monospace by default, which is what mirrors notepad.exe (see the Win32
    // ui_platform_create_main_window for the full reasoning)
    window->monospace_current =
        settings_get_bool("default_font_mono", settings_get_bool("monospace_enabled", true));
    window->zoom = clamp_int(settings_get_int("default_zoom", 100), NPAD_ZOOM_MIN, NPAD_ZOOM_MAX);
    window->expected_cursor = 0;
    window->undo = undo_create(NPAD_UNDO_STEPS);
    window->pending = g_array_new(FALSE, FALSE, sizeof(NpadPendingEdit));
    if (!window->undo || !window->pending) {
        undo_destroy(window->undo);
        if (window->pending)
            g_array_free(window->pending, TRUE);
        free(window);
        return NULL;
    }

    window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window->window), "npad");
    GtkIconTheme *icons = gtk_icon_theme_get_default();
    gtk_window_set_icon_name(GTK_WINDOW(window->window), gtk_icon_theme_has_icon(icons, NPAD_APP_ID)
                                                             ? NPAD_APP_ID
                                                             : "accessories-text-editor");

    window->accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window->window), window->accel_group);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window->window), layout);

    window->text_view = gtk_text_view_new();
    window->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(window->text_view));
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(window->text_view), 4);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(window->text_view), 4);
    window->font_css = gtk_css_provider_new();
    gtk_style_context_add_provider(gtk_widget_get_style_context(window->text_view),
                                   GTK_STYLE_PROVIDER(window->font_css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    window->scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(window->scroller), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(window->scroller), window->text_view);

    window->status_bar = create_status_bar(window);
    create_menu(window);

    gtk_box_pack_start(GTK_BOX(layout), window->menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(layout), window->scroller, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), window->status_bar, FALSE, FALSE, 0);

    g_signal_connect(window->buffer, "insert-text", G_CALLBACK(on_insert_text), window);
    g_signal_connect(window->buffer, "delete-range", G_CALLBACK(on_delete_range), window);
    g_signal_connect(window->buffer, "begin-user-action", G_CALLBACK(on_begin_user_action), window);
    g_signal_connect(window->buffer, "end-user-action", G_CALLBACK(on_end_user_action), window);
    g_signal_connect(window->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved),
                     window);
    g_signal_connect(window->buffer, "changed", G_CALLBACK(on_buffer_changed), window);
    g_signal_connect(window->window, "delete-event", G_CALLBACK(on_delete_event), window);
    g_signal_connect(window->window, "configure-event", G_CALLBACK(on_configure_event), window);
    g_signal_connect(window->window, "window-state-event", G_CALLBACK(on_window_state_event),
                     window);

    apply_word_wrap(window);
    apply_theme(window);

    gtk_widget_show_all(layout);
    gtk_widget_set_visible(window->status_bar, window->status_bar_visible);
    gtk_widget_grab_focus(window->text_view);

    g_gtk_main_window = window;
    update_status_bar(window);
    return window;
}

void ui_platform_destroy_window(Window *window) {
    if (!window)
        return;
    stop_timer(&window->auto_save_timer);
    stop_timer(&window->session_timer);
    stop_timer(&window->status_timer);
    stop_timer(&window->counts_timer);
    stop_timer(&window->startup_timer);
    if (window->window)
        gtk_widget_destroy(window->window);
    if (window->font_css)
        g_object_unref(window->font_css);
    g_free(g_applied_css);
    g_applied_css = NULL;
    if (window->accel_group)
        g_object_unref(window->accel_group);
    clear_pending(window);
    g_array_free(window->pending, TRUE);
    undo_destroy(window->undo);
    if (window == g_gtk_main_window)
        g_gtk_main_window = NULL;
    free(window);
}

void ui_platform_show_window(Window *window) {
    if (!window)
        return;
    gtk_widget_show(window->window);
    // Deferred startup work (the crash-recovery scan) runs just after the
    // window is up, so launching feels instant however much it has to do
    window->startup_timer = g_timeout_add(NPAD_STARTUP_DELAY_MS, on_startup_timer, window);
    schedule_counts_update(window);
}

void ui_platform_hide_window(Window *window) {
    if (window)
        gtk_widget_hide(window->window);
}

void ui_platform_set_window_title(Window *window, const char *title) {
    if (window && title)
        gtk_window_set_title(GTK_WINDOW(window->window), title);
}

void ui_platform_get_window_size(Window *window, int *width, int *height) {
    if (!window || !width || !height)
        return;
    if (window->normal_width > 0 && window->normal_height > 0) {
        *width = window->normal_width;
        *height = window->normal_height;
    } else {
        gtk_window_get_size(GTK_WINDOW(window->window), width, height);
    }
}

void ui_platform_set_window_size(Window *window, int width, int height) {
    if (!window || width <= 0 || height <= 0)
        return;
    gtk_window_resize(GTK_WINDOW(window->window), width, height);
    window->normal_width = width;
    window->normal_height = height;
}

void ui_platform_get_window_position(Window *window, int *x, int *y) {
    if (!window || !x || !y)
        return;
    if (display_allows_positioning() && gtk_widget_get_visible(window->window)) {
        gtk_window_get_position(GTK_WINDOW(window->window), x, y);
    } else {
        // Wayland: report back what was asked for, so a session there does
        // not overwrite the position an X11 session saved
        *x = window->requested_x;
        *y = window->requested_y;
    }
}

void ui_platform_set_window_position(Window *window, int x, int y) {
    if (!window)
        return;
    window->requested_x = x;
    window->requested_y = y;
    if (display_allows_positioning())
        gtk_window_move(GTK_WINDOW(window->window), x, y);
}

void ui_platform_set_window_maximized(Window *window, bool maximized) {
    if (!window)
        return;
    if (maximized)
        gtk_window_maximize(GTK_WINDOW(window->window));
    else
        gtk_window_unmaximize(GTK_WINDOW(window->window));
}

bool ui_platform_is_window_maximized(Window *window) {
    return window && window->maximized;
}

void ui_platform_get_default_window_rect(int *x, int *y, int *width, int *height) {
    GdkRectangle work = { 0, 0, 1280, 800 };
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor && display && gdk_display_get_n_monitors(display) > 0)
        monitor = gdk_display_get_monitor(display, 0); // Wayland has no "primary"
    if (monitor)
        gdk_monitor_get_workarea(monitor, &work);

    // Classic Notepad's default: ~56% of the work area's width, ~60% of its
    // height, centred
    int w = work.width * 56 / 100;
    int h = work.height * 60 / 100;
    if (width)
        *width = w;
    if (height)
        *height = h;
    if (x)
        *x = work.x + (work.width - w) / 2;
    if (y)
        *y = work.y + (work.height - h) / 2;
}

int ui_platform_count_main_windows(void) {
    // Called before this process creates its window, so "others" is all
    return npad_gtk_instance_count_others();
}

void ui_platform_launch_recovery_instance(const char *slot_id, int cascade_index) {
    if (!slot_id)
        return;
    char cascade[16];
    snprintf(cascade, sizeof(cascade), "%d", cascade_index);
    const char *args[] = { "--recover", slot_id, "--cascade", cascade, NULL };
    npad_gtk_spawn_self(args);
}

void ui_platform_notify_settings_changed(void) {
    npad_gtk_instance_broadcast_settings_changed();
}

bool ui_platform_pid_is_running(long pid) {
    return npad_gtk_instance_pid_is_npad(pid);
}

void *ui_platform_get_native_handle(Window *window) {
    return window ? window->window : NULL;
}

// ---------------------------------------------------------------------------
// ui_platform_*: text
// ---------------------------------------------------------------------------

void ui_platform_set_text(Window *window, const char *text) {
    if (!window || !text)
        return;

    // The buffer holds '\n' only; the core restores the file's own line
    // ending on save. And GTK refuses invalid UTF-8 outright, so anything the
    // decoder let through is repaired rather than dropped.
    char *lf = file_convert_line_endings(text, NPAD_EOL_LF);
    const char *source = lf ? lf : text;
    gchar *valid = NULL;
    if (!g_utf8_validate(source, -1, NULL)) {
        valid = g_utf8_make_valid(source, -1);
        source = valid;
    }

    window->loading = true;
    gtk_text_buffer_set_text(window->buffer, source, -1);
    window->loading = false;
    free(lf);
    g_free(valid);

    // New content: fresh history, caret at the top, unmodified
    undo_clear(window->undo);
    clear_pending(window);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(window->buffer, &start);
    gtk_text_buffer_place_cursor(window->buffer, &start);
    window->expected_cursor = 0;
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(window->text_view), &start, 0.0, FALSE, 0.0, 0.0);
    gtk_text_buffer_set_modified(window->buffer, FALSE);
    window->is_modified = false;
    update_status_bar(window);
}

// The returned string is g_malloc'd, which since GLib 2.46 is always the
// system malloc - so the core's free() is correct for it
char *ui_platform_get_text(Window *window) {
    if (!window)
        return NULL;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(window->buffer, &start, &end);
    return gtk_text_buffer_get_text(window->buffer, &start, &end, TRUE);
}

void ui_platform_clear_text(Window *window) {
    ui_platform_set_text(window, "");
}

bool ui_platform_has_selection(Window *window) {
    return window && gtk_text_buffer_get_has_selection(window->buffer);
}

char *ui_platform_get_selected_text(Window *window) {
    GtkTextIter start, end;
    if (!window || !gtk_text_buffer_get_selection_bounds(window->buffer, &start, &end))
        return NULL;
    return gtk_text_buffer_get_text(window->buffer, &start, &end, TRUE);
}

void ui_platform_select_all(Window *window) {
    if (!window)
        return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(window->buffer, &start, &end);
    gtk_text_buffer_select_range(window->buffer, &start, &end);
}

void ui_platform_set_cursor_position(Window *window, int position) {
    if (!window || position < 0)
        return;
    GtkTextIter iter;
    get_iter_at(window, &iter, position);
    gtk_text_buffer_place_cursor(window->buffer, &iter);
    scroll_to_cursor(window);
    update_status_bar(window);
}

int ui_platform_get_cursor_position(Window *window) {
    return window ? (int) cursor_offset(window) : 0;
}

// ---------------------------------------------------------------------------
// ui_platform_*: clipboard and undo
// ---------------------------------------------------------------------------

static GtkClipboard *clipboard_for(Window *window) {
    return gtk_widget_get_clipboard(window->text_view, GDK_SELECTION_CLIPBOARD);
}

void ui_platform_cut(Window *window) {
    if (window)
        gtk_text_buffer_cut_clipboard(window->buffer, clipboard_for(window), TRUE);
}

void ui_platform_copy(Window *window) {
    if (window)
        gtk_text_buffer_copy_clipboard(window->buffer, clipboard_for(window));
}

void ui_platform_paste(Window *window) {
    if (!window)
        return;
    gtk_text_buffer_paste_clipboard(window->buffer, clipboard_for(window), NULL, TRUE);
    scroll_to_cursor(window);
}

void ui_platform_undo(Window *window) {
    undo_apply(window, false);
}

void ui_platform_redo(Window *window) {
    undo_apply(window, true);
}

bool ui_platform_can_undo(Window *window) {
    return window && undo_can_undo(window->undo);
}

bool ui_platform_can_redo(Window *window) {
    return window && undo_can_redo(window->undo);
}

// ---------------------------------------------------------------------------
// ui_platform_*: theme
// ---------------------------------------------------------------------------

void ui_platform_set_dark_mode(bool enabled) {
    // The core's View > Dark Mode toggle saves "theme" itself right after
    // this; apply the matching scheme now
    settings_set_string("theme", enabled ? "dark" : "light");
    if (g_gtk_main_window)
        apply_theme(g_gtk_main_window);
    else
        g_dark_mode = enabled;
}

bool ui_platform_is_dark_mode(void) {
    return g_dark_mode;
}

bool ui_platform_system_supports_dark_mode(void) {
    return true;
}

// ---------------------------------------------------------------------------
// ui_platform_*: status and timers
// ---------------------------------------------------------------------------

bool ui_platform_is_text_modified(Window *window) {
    return window && window->is_modified;
}

void ui_platform_set_text_modified(Window *window, bool modified) {
    if (!window)
        return;
    window->is_modified = modified;
    gtk_text_buffer_set_modified(window->buffer, modified);
}

int ui_platform_get_line_count(Window *window) {
    // GtkTextBuffer lines are logical lines even with word wrap on
    return window ? gtk_text_buffer_get_line_count(window->buffer) : 0;
}

void ui_platform_get_cursor_line_column(Window *window, int *line, int *column) {
    if (line)
        *line = 0;
    if (column)
        *column = 0;
    if (!window || !line || !column)
        return;
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(window->buffer, &iter,
                                     gtk_text_buffer_get_insert(window->buffer));
    *line = gtk_text_iter_get_line(&iter) + 1;
    *column = gtk_text_iter_get_line_offset(&iter) + 1;
}

void ui_platform_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    if (!window)
        return;
    if (encoding_name)
        snprintf(window->status_encoding, sizeof(window->status_encoding), "%s", encoding_name);
    if (eol_name)
        snprintf(window->status_line_ending, sizeof(window->status_line_ending), "%s", eol_name);
    update_status_bar(window);
}

static void rearm_timer(guint *timer, int seconds, GSourceFunc callback, Window *window) {
    stop_timer(timer);
    if (seconds > 0)
        *timer = g_timeout_add_seconds((guint) seconds, callback, window);
}

void ui_platform_set_auto_save_timer(Window *window, int seconds) {
    if (window)
        rearm_timer(&window->auto_save_timer, seconds, on_auto_save_timer, window);
}

void ui_platform_set_session_timer(Window *window, int seconds) {
    if (window)
        rearm_timer(&window->session_timer, seconds, on_session_timer, window);
}
