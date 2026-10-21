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
 * https://npad.dev
 */

#define _DEFAULT_SOURCE // setsid(), nl_langinfo()

#include "ui_gtk.h"

#include <langinfo.h>
#include <locale.h>
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
#include "../core/time_format.h"
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
//
// A user action does not say whether it was a keystroke, so a one-character
// paste, cut or Replace would merge with the typing beside it. Those paths
// therefore make their own step: npad's own commands open an undo group around
// the action (npad_gtk_replace_selection, npad_gtk_paste_plain, Cut, Delete,
// the list tools), and the edits GTK makes by itself (Shift+Delete, a
// middle-click paste, a text drop) get an undo_break on both sides
// (ui_gtk_edit.c).
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

const char *npad_gtk_active_font_key(const Window *window, const char **default_face) {
    return active_font_key(window, default_face);
}

bool npad_gtk_font_is_installed(const char *family) {
    return font_is_installed(family);
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
        // The desktop's own preference, live from the portal; without one,
        // GTK's setting as it stood at startup
        int portal = npad_gtk_portal_color_scheme();
        *dark = portal == 1 ? true : portal == 2 ? false : g_initial_prefer_dark != FALSE;
    } else { // "light", the default: classic Notepad has no schemes
        *bg = "#ffffff";
        *fg = "#000000";
    }
}

// Whether the GTK theme paints its own text area light. Measured on a
// throwaway text view's style path plus its "text" node (the node that carries
// the page colour), so npad's own provider, which sits on the real widget,
// does not colour the answer. Unknown (a transparent node, an odd theme)
// counts as not light: leave the theme alone.
static bool context_background_is_light(GtkStyleContext *context) {
    GdkRGBA *background = NULL;
    gtk_style_context_get(context, GTK_STATE_FLAG_NORMAL, "background-color", &background, NULL);
    bool light = false;
    if (background) {
        light =
            background->alpha > 0.5 &&
            0.299 * background->red + 0.587 * background->green + 0.114 * background->blue > 0.5;
        gdk_rgba_free(background);
    }
    return light;
}

static bool theme_text_area_is_light(void) {
    GtkWidget *probe = g_object_ref_sink(gtk_text_view_new());
    GtkWidgetPath *path = gtk_widget_path_copy(gtk_widget_get_path(probe));
    gtk_widget_path_append_type(path, GTK_TYPE_TEXT_VIEW);
    gtk_widget_path_iter_set_object_name(path, -1, "text");

    GtkStyleContext *context = gtk_style_context_new();
    gtk_style_context_set_path(context, path);
    gtk_widget_path_unref(path);

    bool light = context_background_is_light(context);
    g_object_unref(context);
    g_object_unref(probe);
    return light;
}

// Whether the GTK theme's window background is light, measured on a window
// that is never shown
static bool theme_chrome_is_light(void) {
    GtkWidget *probe = g_object_ref_sink(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    bool light = context_background_is_light(gtk_widget_get_style_context(probe));
    gtk_widget_destroy(probe);
    g_object_unref(probe);
    return light;
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
    // `system` is the GTK theme's own colours - but a dark desktop with a theme
    // that keeps its text area white (BlackMATE, many others) would show a
    // white page in dark chrome. Then the dark scheme's colours stand in.
    if (!bg && dark && theme && strcmp(theme, "system") == 0 && theme_text_area_is_light()) {
        bg = "#1e1e1e";
        fg = "#dcdcdc";
    }
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

// Windows and dialogs opened later follow the main window's icon, which is
// re-applied whenever the colour scheme changes (see npad_gtk_app_icon_name)
static void apply_app_icon(Window *window) {
    const char *name = npad_gtk_app_icon_name();
    gtk_window_set_default_icon_name(name);
    gtk_window_set_icon_name(GTK_WINDOW(window->window), name);
}

// --- Dark chrome for themes with no dark variant ---------------------------
//
// gtk-application-prefer-dark-theme only does anything when the GTK theme ships
// a gtk-3.0/gtk-dark.css (Adwaita, Yaru). The Mint-Y family, Arc and most
// others make "dark" a separate sibling theme instead (Mint-Y -> Mint-Y-Dark),
// so on those the property is silently ignored and a dark scheme sat inside a
// light frame. For a dark scheme we therefore switch this process to the
// sibling theme, and switch back (only if nobody changed it meanwhile) when
// the scheme turns light. Nothing is written to the desktop's settings. A light
// theme with no sibling at all (Cinnamon's TraditionalOk) falls back to
// Adwaita, which ships a dark variant inside GTK itself.

static char *g_dark_sibling_theme = NULL; // The sibling we switched to, or NULL
static char *g_original_gtk_theme = NULL; // What it replaced

// Whether <data dir>/themes/<name>/gtk-3.0/<file> exists in any theme folder
static bool gtk_theme_has_file(const char *name, const char *file) {
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(dirs, g_build_filename(g_get_home_dir(), ".themes", NULL));
    g_ptr_array_add(dirs, g_build_filename(g_get_user_data_dir(), "themes", NULL));
    for (const gchar *const *sys = g_get_system_data_dirs(); *sys; sys++)
        g_ptr_array_add(dirs, g_build_filename(*sys, "themes", NULL));

    bool found = false;
    for (guint i = 0; i < dirs->len && !found; i++) {
        char *path = g_build_filename(g_ptr_array_index(dirs, i), name, "gtk-3.0", file, NULL);
        found = g_file_test(path, G_FILE_TEST_IS_REGULAR);
        g_free(path);
    }
    g_ptr_array_free(dirs, TRUE);
    return found;
}

static bool theme_name_is_dark(const char *name) {
    char *lower = g_ascii_strdown(name, -1);
    bool dark = strstr(lower, "dark") != NULL;
    g_free(lower);
    return dark;
}

// The installed dark sibling of a GTK theme, or NULL: Name-Dark, Name-dark, and
// Mint-Y-Aqua -> Mint-Y-Dark-Aqua (the colour is the last segment)
static char *find_dark_sibling_theme(const char *name) {
    GPtrArray *candidates = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(candidates, g_strconcat(name, "-Dark", NULL));
    g_ptr_array_add(candidates, g_strconcat(name, "-dark", NULL));
    const char *last_dash = strrchr(name, '-');
    if (last_dash && last_dash != name) {
        char *base = g_strndup(name, (gsize) (last_dash - name));
        g_ptr_array_add(candidates, g_strconcat(base, "-Dark", last_dash, NULL));
        g_free(base);
    }

    char *found = NULL;
    for (guint i = 0; i < candidates->len && !found; i++) {
        const char *candidate = g_ptr_array_index(candidates, i);
        if (gtk_theme_has_file(candidate, "gtk.css"))
            found = g_strdup(candidate);
    }
    g_ptr_array_free(candidates, TRUE);
    return found;
}

// Bring the GTK theme in line with a dark / light scheme. Both directions only
// act on a change: setting gtk-theme-name restyles every widget.
static void apply_chrome_variant(bool dark) {
    GtkSettings *settings = gtk_settings_get_default();
    char *name = NULL;
    g_object_get(settings, "gtk-theme-name", &name, NULL);

    if (dark) {
        if (!g_dark_sibling_theme && name && !theme_name_is_dark(name) &&
            !gtk_theme_has_file(name, "gtk-dark.css")) {
            char *sibling = find_dark_sibling_theme(name);
            // No dark sibling, and the theme is light (BlackMATE is already
            // dark, so it is left alone): Adwaita is built into GTK, always
            // there, and has a dark variant that the prefer-dark hint selects
            if (!sibling && theme_chrome_is_light())
                sibling = g_strdup("Adwaita");
            if (sibling) {
                g_original_gtk_theme = g_strdup(name);
                g_dark_sibling_theme = sibling;
                g_object_set(settings, "gtk-theme-name", sibling, NULL);
            }
        }
    } else if (g_dark_sibling_theme) {
        // Restore only if the theme is still the one we set: a desktop theme
        // change in between is the user's newer choice
        if (g_strcmp0(name, g_dark_sibling_theme) == 0)
            g_object_set(settings, "gtk-theme-name", g_original_gtk_theme, NULL);
        g_clear_pointer(&g_dark_sibling_theme, g_free);
        g_clear_pointer(&g_original_gtk_theme, g_free);
    }
    g_free(name);
}

// What the colour-scheme code sees, for the Debug page: why a dark desktop does
// or does not darken the window. Caller g_free()s.
char *npad_gtk_theme_diagnostics(void) {
    char *name = NULL;
    gboolean prefer_dark = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &name,
                 "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
    char *scheme = settings_get_string("theme", "light");
    int portal = npad_gtk_portal_color_scheme();
    const char *portal_text = portal == 1 ? "dark" : portal == 2 ? "light" : "none";
    bool has_dark_variant = name && gtk_theme_has_file(name, "gtk-dark.css");

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "Colour scheme: %s (desktop colour-scheme via portal: %s)\n",
                           scheme ? scheme : "light", portal_text);
    g_string_append_printf(out, "GTK prefer-dark: %s at startup, %s now\n",
                           g_initial_prefer_dark ? "on" : "off", prefer_dark ? "on" : "off");
    g_string_append_printf(out, "Theme has a dark variant (gtk-dark.css): %s\n",
                           has_dark_variant ? "yes" : "no");
    g_string_append_printf(out, "Switched to dark sibling theme: %s\n",
                           g_dark_sibling_theme ? g_dark_sibling_theme : "no");
    g_free(scheme);
    g_free(name);
    return g_string_free(out, FALSE);
}

// Colour scheme: the text colours come from apply_view_css; the window chrome
// follows via GTK's dark variant (or the dark sibling theme), so a dark scheme
// does not sit inside a light frame
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
    apply_chrome_variant(dark);
    apply_app_icon(window);
    apply_view_css(window);
}

// The desktop's light/dark preference changed (the portal's SettingChanged,
// already filtered to a different value). Only the `system` scheme follows it;
// the others are the user's explicit choice.
void npad_gtk_system_scheme_changed(void) {
    if (!g_gtk_main_window)
        return;
    char *theme = settings_get_string("theme", "light");
    bool follows = theme && strcmp(theme, "system") == 0;
    free(theme);
    if (follows)
        apply_theme(g_gtk_main_window);
}

// The icon for windows and the About dialog: npad's own hicolor icon when the
// package (or `make install`) put it in the icon theme, otherwise a stock
// text-editor icon so a build run from the source tree still shows something
// npad's own hicolor icon when the package (or `make install`) put it in the
// icon theme - its dark artwork while a dark scheme is active, so it does not
// glare from a dark panel - and otherwise a stock text-editor icon, so a build
// run from the source tree shows something. Only the window icon follows the
// scheme: the launcher keeps the standard icon.
const char *npad_gtk_app_icon_name(void) {
    GtkIconTheme *icons = gtk_icon_theme_get_default();
    if (g_dark_mode && gtk_icon_theme_has_icon(icons, NPAD_APP_ID "-dark"))
        return NPAD_APP_ID "-dark";
    return gtk_icon_theme_has_icon(icons, NPAD_APP_ID) ? NPAD_APP_ID : "accessories-text-editor";
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
    // Scroll bars stay on screen and go inert when the text fits, as in
    // Notepad (ES_DISABLENOSCROLL on Win32), instead of coming and going.
    // That also keeps the text width constant, so the wrap does not reflow as
    // the document crosses one screen. With wrap on there is no horizontal bar.
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->scroller),
                                   window->word_wrap_enabled ? GTK_POLICY_NEVER : GTK_POLICY_ALWAYS,
                                   GTK_POLICY_ALWAYS);
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
static void on_view_state_changed(const Window *window) {
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

void npad_gtk_zoom_by(Window *window, int delta) {
    if (!window)
        return;
    int target = clamp_int(window->zoom + delta, NPAD_ZOOM_MIN, NPAD_ZOOM_MAX);
    if (target != window->zoom) // At a limit nothing changes: no broadcast either
        set_zoom(window, target);
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

static gboolean destroy_popup_idle(gpointer data) {
    gtk_widget_destroy(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

// A menu closes ("deactivate") before the chosen item activates, so destroying
// it there would destroy the choice with it: destroy it once that has run
static void on_popup_deactivate(GtkMenuShell *menu, gpointer data) {
    (void) data;
    g_idle_add(destroy_popup_idle, menu);
}

// Each status-bar popup is built fresh (its radio marks follow the document),
// so it must also go away again, or every click leaves a menu behind
static void popup_status_menu(GtkWidget *menu, GtkWidget *anchor) {
    g_signal_connect(menu, "deactivate", G_CALLBACK(on_popup_deactivate), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST,
                             NULL);
}

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
    popup_status_menu(menu, anchor);
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
    popup_status_menu(menu, anchor);
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
static gboolean on_status_click(GtkWidget *widget, const GdkEventButton *event, gpointer data) {
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
    const char *path = g_object_get_data(G_OBJECT(item), "npad-path");
    if (path) {
        Window *window = data;
        ui_post_event(UI_EVENT_FILE_DROPPED, window, (void *) path);
    }
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
        // Ten entries, as on Windows (recent_files_max caps the list at 10)
        for (int i = 0; i < count && i < 10; i++) {
            // "_1 path" .. "_9 path", then "1_0 path" (Alt+0, not a second
            // Alt+1), with the path's own underscores doubled so they are
            // shown rather than taken as mnemonics. A name that is not valid
            // UTF-8 is shown in GLib's display form; the item keeps the real
            // bytes to open.
            GString *label = g_string_new(NULL);
            if (i < 9)
                g_string_append_printf(label, "_%d ", i + 1);
            else
                g_string_append(label, "1_0 ");
            char *shown = npad_gtk_display_text(files[i]);
            for (const char *p = shown; *p; p++) {
                if (*p == '_')
                    g_string_append_c(label, '_');
                g_string_append_c(label, *p);
            }
            g_free(shown);
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
    bool has_text = gtk_text_buffer_get_char_count(window->buffer) > 0;

    // Shared with the context menu (ui_gtk_edit.c) so the two never disagree
    static const NpadCommand shared[] = {
        NPAD_CMD_EDIT_UNDO, NPAD_CMD_EDIT_REDO,   NPAD_CMD_EDIT_CUT,
        NPAD_CMD_EDIT_COPY, NPAD_CMD_EDIT_DELETE, NPAD_CMD_EDIT_PASTE,
    };
    for (size_t i = 0; i < G_N_ELEMENTS(shared); i++)
        set_command_sensitive(window, shared[i], npad_gtk_edit_command_enabled(window, shared[i]));
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
            window->edit_menu_open = true;
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
    window->edit_menu_open = false;
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
    window->help_item = gtk_menu_get_attach_widget(GTK_MENU(help));
    // Checks and says how to update; never installs (ui_gtk_update.c)
    add_item(window, help, "Check for _Updates...", NPAD_CMD_HELP_CHECK_UPDATES, 0, NONE);
    add_separator(help);
    add_item(window, help, "_About npad", NPAD_CMD_HELP_ABOUT, 0, NONE);

    // Shortcuts without a menu item of their own, or second keys for one
    add_hidden_accel(window, GDK_KEY_e, C, NPAD_CMD_FORMAT_EOL_CYCLE);
    // Ctrl+Shift+. : GTK matches the shifted symbol on most layouts, the
    // unshifted key on the rest
    add_hidden_accel(window, GDK_KEY_period, CS, NPAD_CMD_EDIT_PREFERENCES_DEBUG);
    add_hidden_accel(window, GDK_KEY_greater, CS, NPAD_CMD_EDIT_PREFERENCES_DEBUG);
    add_hidden_accel(window, GDK_KEY_greater, C, NPAD_CMD_EDIT_PREFERENCES_DEBUG);
    add_hidden_accel(window, GDK_KEY_equal, C, NPAD_CMD_VIEW_ZOOM_IN);
    add_hidden_accel(window, GDK_KEY_KP_Add, C, NPAD_CMD_VIEW_ZOOM_IN);
    add_hidden_accel(window, GDK_KEY_KP_Subtract, C, NPAD_CMD_VIEW_ZOOM_OUT);
    add_hidden_accel(window, GDK_KEY_KP_0, C, NPAD_CMD_VIEW_ZOOM_RESET);
    // The alternate rich-text paste; a plain paste while the Markdown tools are
    // off, so it is inert rather than surprising
    add_hidden_accel(window, GDK_KEY_v, CS, NPAD_CMD_EDIT_PASTE_ALT);

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
    // The header's &f and the job name are UTF-8 text, whatever the name's bytes
    char *title = path ? g_filename_display_basename(path) : NULL;

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

// strftime in the locale's own encoding, handed back as UTF-8 (GTK takes
// nothing else: a non-UTF-8 locale's "%p" or month name would otherwise be
// refused by the buffer after the selection had already been deleted)
static char *format_local_time(const char *fmt, const struct tm *local) {
    char text[256];
    size_t n = strftime(text, sizeof(text), fmt, local);
    if (n == 0)
        return NULL;
    char *utf8 = g_locale_to_utf8(text, (gssize) n, NULL, NULL, NULL);
    if (!utf8)
        utf8 = g_utf8_make_valid(text, (gssize) n);
    return utf8;
}

void npad_gtk_short_time_date(char **time_text, char **date_text) {
    if (time_text)
        *time_text = NULL;
    if (date_text)
        *date_text = NULL;
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local))
        return;

    // Windows' TIME_NOSECONDS and DATE_SHORTDATE, from the locale's full
    // formats (core/time_format.c): "%r" or "%T" lose their seconds, "%y"
    // becomes a four-digit year
    char fmt[128];
    if (time_text) {
        if (!time_format_short_time(nl_langinfo(T_FMT), fmt, sizeof(fmt)))
            g_strlcpy(fmt, "%H:%M", sizeof(fmt));
        *time_text = format_local_time(fmt, &local);
        if (!*time_text)
            *time_text = format_local_time("%H:%M", &local);
    }
    if (date_text) {
        if (!time_format_short_date(nl_langinfo(D_FMT), fmt, sizeof(fmt)))
            g_strlcpy(fmt, "%m/%d/%Y", sizeof(fmt));
        *date_text = format_local_time(fmt, &local);
        if (!*date_text)
            *date_text = format_local_time("%m/%d/%Y", &local);
    }
}

// Notepad's F5 stamp: the locale's short time (no seconds) then short date
static void insert_time_date(Window *window) {
    char *time_text = NULL;
    char *date_text = NULL;
    npad_gtk_short_time_date(&time_text, &date_text);
    if (time_text || date_text) {
        char *stamp =
            g_strdup_printf("%s %s", time_text ? time_text : "", date_text ? date_text : "");
        npad_gtk_replace_selection(window, g_strstrip(stamp));
        g_free(stamp);
    }
    g_free(time_text);
    g_free(date_text);
}

void npad_gtk_replace_selection(Window *window, const char *text) {
    if (!window || !text)
        return;
    // The group keeps a one-character result (a Replace with "x") from
    // merging into the word typed before it
    undo_begin_group(window->undo);
    gtk_text_buffer_begin_user_action(window->buffer);
    gtk_text_buffer_delete_selection(window->buffer, TRUE, TRUE);
    gtk_text_buffer_insert_at_cursor(window->buffer, text, -1);
    gtk_text_buffer_end_user_action(window->buffer);
    undo_end_group(window->undo);
    scroll_to_cursor(window);
}

char *npad_gtk_display_text(const char *text) {
    return text ? g_filename_display_name(text) : g_strdup("");
}

char *npad_gtk_self_exe_path(void) {
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    // A package upgraded under a running npad replaces the file; the kernel
    // then reports the old inode as "<path> (deleted)"
    if (exe && g_str_has_suffix(exe, " (deleted)"))
        exe[strlen(exe) - strlen(" (deleted)")] = '\0';
    return exe;
}

void npad_gtk_spawn_self(const char *const *extra_args) {
    // The installed path when it is there - after an upgrade that is the new
    // version, which is what the user installed. If it is gone (the copy was
    // removed, or moved), /proc/self/exe still executes the running image.
    gchar *exe = npad_gtk_self_exe_path();
    if (!exe || !g_file_test(exe, G_FILE_TEST_IS_EXECUTABLE)) {
        g_free(exe);
        exe = g_strdup("/proc/self/exe");
    }
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, exe);
    for (const char *const *a = extra_args; a && *a; a++)
        g_ptr_array_add(argv, (gpointer) *a);
    g_ptr_array_add(argv, NULL);

    GError *error = NULL;
    if (!g_spawn_async(NULL, (gchar **) argv->pdata, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
                       &error)) {
        // Say so: a New Window that silently does nothing looks like a hang
        char *shown = npad_gtk_display_text(error ? error->message : "");
        char *message = g_strdup_printf("npad could not start a new window.\n\n%s", shown);
        ui_show_message_box(g_gtk_main_window, "npad", message, false);
        g_free(message);
        g_free(shown);
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

// Was Shift held for the event that activated this command? (a Shift+click on
// a menu item, as the Win32 build's hidden Debug page expects)
static bool preferences_shift_held(void) {
    GdkModifierType state = 0;
    return gtk_get_current_event_state(&state) && (state & GDK_SHIFT_MASK) != 0;
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
            // A step of its own, like Cut: a one-character selection deleted
            // from the menu must not join the Backspaces next to it
            undo_begin_group(window->undo);
            gtk_text_buffer_delete_selection(window->buffer, TRUE, TRUE);
            undo_end_group(window->undo);
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
        case NPAD_CMD_EDIT_PREFERENCES:
            // Shift+click (or Shift+Enter) on the menu item opens the Debug page
            npad_gtk_show_preferences(window, preferences_shift_held());
            break;
        case NPAD_CMD_EDIT_PREFERENCES_DEBUG:
            npad_gtk_show_preferences(window, true);
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
        case NPAD_CMD_FORMAT_FONT:
            npad_gtk_show_font_dialog(window);
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
        case NPAD_CMD_HELP_CHECK_UPDATES:
            npad_gtk_update_command(window);
            break;
        case NPAD_CMD_HELP_ABOUT:
            ui_show_about_dialog(window);
            break;
        default:
            // The Markdown list tools and the alternate paste
            npad_gtk_lists_command(window, command);
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
    npad_gtk_lists_reload_settings(window); // The Markdown menu appears and goes live

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
    // Another window's check, a skip or a mode change moves the Help dot
    npad_gtk_update_apply_indicator(window);
    npad_gtk_prefs_updates_refresh();
}

// ---------------------------------------------------------------------------
// Window events
// ---------------------------------------------------------------------------

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void) widget;
    (void) event;
    // The editor decides (prompting to save) and calls ui_quit() when the
    // window may close; the window itself is never destroyed from here.
    // During a logout the close comes from the desktop, not the user: park
    // the work instead of a save prompt nobody will answer (Win32's WM_CLOSE
    // after WM_ENDSESSION).
    ui_post_event(npad_gtk_session_ending() ? UI_EVENT_QUIT_HANDOFF : UI_EVENT_QUIT, data, NULL);
    return TRUE;
}

static gboolean on_configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    (void) event;
    Window *window = data;
    // Remember the restored geometry so a maximised window persists it, as
    // GetWindowPlacement does on Win32. The position only exists on X11 (on
    // Wayland it reads 0,0); the previous record is kept for
    // on_window_state_event.
    if (!window->maximized) {
        window->prev_width = window->normal_width;
        window->prev_height = window->normal_height;
        gtk_window_get_size(GTK_WINDOW(widget), &window->normal_width, &window->normal_height);
        if (display_allows_positioning()) {
            window->prev_x = window->normal_x;
            window->prev_y = window->normal_y;
            gtk_window_get_position(GTK_WINDOW(widget), &window->normal_x, &window->normal_y);
            window->normal_pos_known = true;
        }
    }
    return FALSE;
}

// Whether the newest "normal" size record covers (nearly) the whole work area
// of the window's monitor - what a maximized window measures, title bar and
// panels allowed for
static bool record_fills_monitor(const Window *window, GtkWidget *widget) {
    GdkDisplay *display = gtk_widget_get_display(widget);
    GdkWindow *gdk = gtk_widget_get_window(widget);
    GdkMonitor *monitor = (display && gdk) ? gdk_display_get_monitor_at_window(display, gdk) : NULL;
    if (!monitor)
        return false;
    GdkRectangle work;
    gdk_monitor_get_workarea(monitor, &work);
    if (work.width <= 0 || work.height <= 0)
        return false; // Broadway and friends: nothing to measure against
    return window->normal_width >= work.width - 16 && window->normal_height >= work.height - 64;
}

static gboolean on_window_state_event(GtkWidget *widget, const GdkEventWindowState *event,
                                      gpointer data) {
    Window *window = data;
    bool maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    if (maximized && !window->maximized && window->prev_width > 0 && window->prev_height > 0 &&
        record_fills_monitor(window, widget)) {
        // The maximize's own configure can arrive while the window still
        // reads as restored, and record the maximized size and the monitor
        // corner as "normal". A record that fills the monitor is that
        // configure: the one before it is the restored geometry.
        window->normal_width = window->prev_width;
        window->normal_height = window->prev_height;
        window->normal_x = window->prev_x;
        window->normal_y = window->prev_y;
    }
    window->maximized = maximized;
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
    // Optional launch-time update check (off by default): once, after the
    // first paint, and silent on failure
    npad_gtk_update_launch_check(window);
    return G_SOURCE_REMOVE;
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

    // Every window and dialog npad opens (Preferences, Find, ...) wears the
    // app icon, not just the main window
    gtk_window_set_default_icon_name(npad_gtk_app_icon_name());

    npad_gtk_instance_init(); // Optional: npad runs without a session bus
    // Logout, shutdown, kill, a closed terminal, a lost display: park unsaved
    // work like Windows' WM_ENDSESSION (ui_gtk_session.c)
    npad_gtk_session_init();
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
    apply_app_icon(window); // Re-applied whenever the scheme changes

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
    // Overlay scroll bars hide themselves whatever the policy says; Notepad's
    // sit in a permanent gutter
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(window->scroller), FALSE);
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

    npad_gtk_edit_init(window);
    npad_gtk_lists_init(window);
    npad_gtk_update_apply_indicator(window); // An update a previous check found

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
    if (!window || !title)
        return;
    // The title carries the file name, whose bytes need not be UTF-8
    char *shown = npad_gtk_display_text(title);
    gtk_window_set_title(GTK_WINDOW(window->window), shown);
    g_free(shown);
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
    if (display_allows_positioning() && window->maximized && window->normal_pos_known) {
        // Maximized, the window sits in the monitor's corner; what is saved
        // is where it goes back to (GetWindowPlacement's rcNormalPosition)
        *x = window->normal_x;
        *y = window->normal_y;
    } else if (display_allows_positioning() && gtk_widget_get_visible(window->window)) {
        gtk_window_get_position(GTK_WINDOW(window->window), x, y);
    } else {
        // Wayland: report back what was asked for, so a session there does
        // not overwrite the position an X11 session saved
        *x = window->requested_x;
        *y = window->requested_y;
    }
}

// The monitor first-run placement is centred on (Wayland has no "primary")
static GdkMonitor *placement_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = display ? gdk_display_get_primary_monitor(display) : NULL;
    if (!monitor && display && gdk_display_get_n_monitors(display) > 0)
        monitor = gdk_display_get_monitor(display, 0);
    return monitor;
}

// Whether a window at (x, y) with this size would have a usable stretch of its
// title bar on some monitor. A saved position goes stale when a monitor is
// unplugged or the layout changes, and a window that opens entirely off every
// screen cannot be grabbed to bring it back.
static bool position_is_reachable(int x, int y, int width, int height) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display)
        return true; // Nothing to judge against
    int monitors = gdk_display_get_n_monitors(display);
    if (monitors <= 0)
        return true;

    // The strip a user would drag: the window's top edge, about a title bar tall
    GdkRectangle strip = { x, y, width, height < 40 ? height : 40 };
    bool measured = false; // A display that reports no real monitor cannot judge
    for (int i = 0; i < monitors; i++) {
        GdkRectangle geometry, shared;
        gdk_monitor_get_geometry(gdk_display_get_monitor(display, i), &geometry);
        if (geometry.width <= 0 || geometry.height <= 0)
            continue;
        measured = true;
        if (gdk_rectangle_intersect(&strip, &geometry, &shared) && shared.width >= 100 &&
            shared.height >= 20)
            return true;
    }
    return !measured;
}

void ui_platform_set_window_position(Window *window, int x, int y) {
    if (!window)
        return;
    if (display_allows_positioning()) {
        int width = window->normal_width;
        int height = window->normal_height;
        if (width <= 0 || height <= 0)
            gtk_window_get_size(GTK_WINDOW(window->window), &width, &height);

        // Off every monitor: use the default placement (centred on the primary
        // monitor, as on first run), and remember that rather than the stale
        // position so the next save repairs it
        if (!position_is_reachable(x, y, width, height)) {
            GdkRectangle work = { 0, 0, 1280, 800 };
            GdkMonitor *monitor = placement_monitor();
            if (monitor)
                gdk_monitor_get_workarea(monitor, &work);
            x = work.x + (work.width > width ? (work.width - width) / 2 : 0);
            y = work.y + (work.height > height ? (work.height - height) / 2 : 0);
        }
    }
    window->requested_x = x;
    window->requested_y = y;
    if (display_allows_positioning()) {
        // Also the restored position until a configure reports the real one:
        // a window maximized at launch may never be seen restored
        window->normal_x = x;
        window->normal_y = y;
        window->normal_pos_known = true;
        gtk_window_move(GTK_WINDOW(window->window), x, y);
    }
}

void ui_platform_set_window_maximized(Window *window, bool maximized) {
    if (!window)
        return;
    if (maximized)
        gtk_window_maximize(GTK_WINDOW(window->window));
    else
        gtk_window_unmaximize(GTK_WINDOW(window->window));
}

bool ui_platform_is_window_maximized(const Window *window) {
    return window && window->maximized;
}

void ui_platform_get_default_window_rect(int *x, int *y, int *width, int *height) {
    GdkRectangle work = { 0, 0, 1280, 800 };
    GdkMonitor *monitor = placement_monitor();
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

// The buffer holds '\n' only; the core restores the file's own line ending on
// save. And GTK refuses invalid UTF-8 outright, so anything the decoder let
// through is repaired rather than dropped.
char *npad_gtk_normalize_text(const char *text) {
    if (!text)
        return NULL;
    char *lf = file_convert_line_endings(text, NPAD_EOL_LF);
    const char *source = lf ? lf : text;
    char *result =
        g_utf8_validate(source, -1, NULL) ? g_strdup(source) : g_utf8_make_valid(source, -1);
    free(lf);
    return result;
}

void ui_platform_set_text(Window *window, const char *text) {
    if (!window || !text)
        return;

    char *source = npad_gtk_normalize_text(text);
    window->loading = true;
    gtk_text_buffer_set_text(window->buffer, source, -1);
    window->loading = false;
    g_free(source);

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
    // Caret at the end, the anchor at the start, as Notepad and the Win32
    // build leave it: Shift+arrows then adjust the end of the selection
    gtk_text_buffer_select_range(window->buffer, &end, &start);
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
    if (!window)
        return;
    // With the Markdown tools on, Ctrl+X without a selection cuts the line
    if (npad_gtk_lists_cut_line(window))
        return;
    bool had_selection = gtk_text_buffer_get_has_selection(window->buffer);
    // One step of its own: a one-character cut must not join the Backspaces
    // or typing next to it
    undo_begin_group(window->undo);
    gtk_text_buffer_cut_clipboard(window->buffer, clipboard_for(window), TRUE);
    undo_end_group(window->undo);
    if (had_selection)
        npad_gtk_clipboard_note_own_text(window);
}

void ui_platform_copy(Window *window) {
    if (!window)
        return;
    bool had_selection = gtk_text_buffer_get_has_selection(window->buffer);
    gtk_text_buffer_copy_clipboard(window->buffer, clipboard_for(window));
    if (had_selection)
        npad_gtk_clipboard_note_own_text(window);
}

// The clipboard's text has arrived (or there was none). The window is looked up
// again: the request may outlive the one it was made for.
static void on_paste_text(GtkClipboard *clipboard, const gchar *text, gpointer data) {
    (void) clipboard;
    (void) data;
    Window *window = g_gtk_main_window;
    if (!window || !text || !text[0])
        return;
    // '\n' only and valid UTF-8, like everything else in the buffer
    char *clean = npad_gtk_normalize_text(text);
    if (clean && clean[0])
        npad_gtk_replace_selection(window, clean); // One undo step, even for "x"
    g_free(clean);
}

void npad_gtk_paste_plain(Window *window) {
    if (!window)
        return;
    // npad's own paste rather than gtk_text_buffer_paste_clipboard: that one
    // ends in a user action of one edit, which the undo history cannot tell
    // from a keystroke, so pasting "x" after "abc" undid all four at once. It
    // can also carry text tags (Highlight All's wash) across from text copied
    // inside npad.
    gtk_clipboard_request_text(clipboard_for(window), on_paste_text, NULL);
}

void ui_platform_paste(Window *window) {
    if (!window)
        return;
    // With the Markdown tools on: a pending cut line goes back above the
    // caret's, and rich text converts per markdown_paste_primary
    if (!npad_gtk_lists_paste(window, NPAD_PASTE_PRIMARY))
        npad_gtk_paste_plain(window);
}

void ui_platform_undo(Window *window) {
    undo_apply(window, false);
}

void ui_platform_redo(Window *window) {
    undo_apply(window, true);
}

bool ui_platform_can_undo(const Window *window) {
    return window && undo_can_undo(window->undo);
}

bool ui_platform_can_redo(const Window *window) {
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

bool ui_platform_is_text_modified(const Window *window) {
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
