/*
 * npad - GTK3 UI: Find, Replace and Go To
 *
 * One modeless dialog serves both Find and Replace, like the Win32 build
 * (find_replace_proc in ui_win32.c is the reference for behaviour). Searching
 * runs on GtkTextIter, so every position here is a GTK character offset or an
 * iterator - never a byte index. The buffer holds '\n' only, so search and
 * replacement text are normalised to that (an escaped "\r" becomes a line
 * break, as it does on Win32 where the edit control stores a bare CR).
 *
 *  - Search is gtk_text_iter_forward_search / backward_search over the text
 *    only. Match case off adds GTK's case-insensitive flag; Whole word is a
 *    boundary check around each hit, applied at an edge of the term only
 *    when that edge is itself a word character.
 *  - Replace All brackets its edits in one GTK user action, which ui_gtk.c
 *    turns into a single undo group.
 *  - Highlight All is a GtkTextTag over every match (capped at 10,000). A tag
 *    is not text: it never marks the document modified or enters the undo
 *    history.
 *  - The options persist under the same settings keys as the Win32 build and
 *    are re-read when another instance broadcasts a settings change.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/list_ops.h"
#include "../core/settings.h"

// Find/Replace term history: most-recent-first, de-duplicated, capped
#define FIND_HISTORY_MAX 10
// Highlight-all and the "Match n of m" count stop here, like Win32
#define FIND_MATCH_CAP 10000
// Coalesce live highlight refreshes (typing in the box or the document)
#define FIND_HIGHLIGHT_DELAY_MS 120

#define HIGHLIGHT_TAG "npad-find-highlight"

typedef struct {
    gint start; // Character offsets, end exclusive
    gint end;
} MatchRange;

// --- Search state (one set per process: npad is one process per window) ------

static bool g_loaded = false;
static char *g_search_text = NULL; // As typed (history keeps this form)
static char *g_replace_text = NULL;
static char *g_search_eff = NULL; // What is searched for (escapes interpreted)
static char *g_replace_eff = NULL;
static bool g_match_case = false;
static bool g_whole_word = false;
static bool g_search_down = true;
static bool g_wrap_around = false;
static bool g_interpret_escapes = false;
static bool g_highlight_all = false;
static unsigned g_persisted_bits = 0; // Option values last read from / written to settings

// --- Dialog state --------------------------------------------------------------

static GtkWidget *g_dialog = NULL;
static Window *g_owner = NULL;
static bool g_dialog_replace = false;
static bool g_syncing = false;            // Widgets are being set from code, not by the user
static bool g_highlights_applied = false; // The tag is on some text
static guint g_highlight_timer = 0;
static gulong g_changed_handler = 0;
static struct {
    GtkWidget *find_entry;
    GtkWidget *replace_entry;
    GtkWidget *case_check;
    GtkWidget *word_check;
    GtkWidget *wrap_check;
    GtkWidget *highlight_check;
    GtkWidget *escapes_check;
    GtkWidget *up_radio;
    GtkWidget *down_radio;
} g_ui;

static void refresh_highlights(Window *window, bool want);
static void schedule_highlight_refresh(void);

// ---------------------------------------------------------------------------
// Options and effective search text
// ---------------------------------------------------------------------------

static unsigned option_bits(void) {
    return (g_match_case ? 1u : 0u) | (g_whole_word ? 2u : 0u) | (g_search_down ? 4u : 0u) |
           (g_wrap_around ? 8u : 0u) | (g_interpret_escapes ? 16u : 0u) |
           (g_highlight_all ? 32u : 0u);
}

static void load_options(void) {
    g_match_case = settings_get_bool("find_match_case", false);
    g_whole_word = settings_get_bool("find_whole_word", false);
    g_search_down = settings_get_bool("find_search_down", true);
    // Off by default to match notepad.exe (see the Win32 build)
    g_wrap_around = settings_get_bool("find_wrap_around", false);
    g_interpret_escapes = settings_get_bool("find_interpret_escapes", false);
    g_highlight_all = settings_get_bool("find_highlight_all", false);
    g_persisted_bits = option_bits();
}

static void ensure_loaded(void) {
    if (g_loaded)
        return;
    g_loaded = true;
    load_options();
    g_search_text = g_strdup("");
    g_replace_text = g_strdup("");
    g_search_eff = g_strdup("");
    g_replace_eff = g_strdup("");
}

// CRLF and lone CR become '\n': the buffer holds nothing else
static char *normalize_breaks(const char *s) {
    GString *out = g_string_new(NULL);
    for (const char *p = s; *p; p++) {
        if (*p == '\r') {
            g_string_append_c(out, '\n');
            if (p[1] == '\n')
                p++;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

static char *effective_text(const char *raw) {
    char *interpreted = NULL;
    // Gated like read_search_text: inert while the Markdown tools are off
    if (g_interpret_escapes && settings_get_bool("list_tools_enabled", false)) {
        char *unescaped = list_unescape(raw);
        if (unescaped && g_utf8_validate(unescaped, -1, NULL))
            interpreted = g_strdup(unescaped);
        free(unescaped);
    }
    char *result = normalize_breaks(interpreted ? interpreted : raw);
    g_free(interpreted);
    return result;
}

static void recompute_effective(void) {
    g_free(g_search_eff);
    g_free(g_replace_eff);
    g_search_eff = effective_text(g_search_text);
    g_replace_eff = effective_text(g_replace_text);
}

// Read the fields and option checkboxes into the globals (no history push, no
// settings write): shared by the action buttons and the live highlight refresh
static void read_search_text(void) {
    ensure_loaded();
    if (!g_dialog)
        return;
    g_free(g_search_text);
    g_search_text = g_strdup(gtk_entry_get_text(GTK_ENTRY(g_ui.find_entry)));
    if (g_ui.replace_entry) {
        g_free(g_replace_text);
        g_replace_text = g_strdup(gtk_entry_get_text(GTK_ENTRY(g_ui.replace_entry)));
    }
    g_match_case = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.case_check));
    g_whole_word = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.word_check));
    g_wrap_around = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.wrap_check));
    // Escape interpretation belongs to the optional Markdown tools: inert (not
    // just hidden) while they are off, even if a stale ticked state persisted
    g_interpret_escapes = settings_get_bool("list_tools_enabled", false) &&
                          gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.escapes_check));
    g_highlight_all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.highlight_check));
    if (g_ui.up_radio)
        g_search_down = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ui.up_radio));
    recompute_effective();
}

// Write changed options to settings.json right away and tell the other
// instances (shared settings propagate live). A reload in this process clears
// unsaved in-memory settings, so nothing is left unsaved.
static void persist_options(void) {
    unsigned bits = option_bits();
    if (bits == g_persisted_bits)
        return;
    g_persisted_bits = bits;
    settings_set_bool("find_match_case", g_match_case);
    settings_set_bool("find_whole_word", g_whole_word);
    settings_set_bool("find_search_down", g_search_down);
    settings_set_bool("find_wrap_around", g_wrap_around);
    settings_set_bool("find_interpret_escapes", g_interpret_escapes);
    settings_set_bool("find_highlight_all", g_highlight_all);
    settings_save();
    ui_notify_settings_changed();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

static void history_load(GtkWidget *combo, const char *prefix) {
    for (int i = 0; i < FIND_HISTORY_MAX; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        char *value = settings_get_string(key, NULL);
        if (!value)
            break;
        if (value[0])
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), value);
        free(value);
    }
}

// Put value first in the history, dropping an older duplicate. Returns true
// when the stored list changed.
static bool history_push(const char *prefix, const char *value) {
    if (!value || !value[0])
        return false;

    char *entries[FIND_HISTORY_MAX];
    int count = 0;
    entries[count++] = g_strdup(value); // g_malloc is malloc (GLib >= 2.46): free() below is right
    bool changed = true;
    for (int i = 0; i < FIND_HISTORY_MAX && count < FIND_HISTORY_MAX; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        char *existing = settings_get_string(key, NULL);
        if (!existing)
            break;
        if (existing[0] && strcmp(existing, value) != 0) {
            entries[count++] = existing;
        } else {
            if (i == 0 && strcmp(existing, value) == 0)
                changed = false; // Already first; the rest keeps its order
            free(existing);
        }
    }

    if (changed) {
        for (int i = 0; i < FIND_HISTORY_MAX; i++) {
            char key[48];
            snprintf(key, sizeof(key), "%s_%d", prefix, i);
            if (i < count)
                settings_set_string(key, entries[i]);
            else
                settings_remove_key(key);
        }
    }
    for (int i = 0; i < count; i++)
        free(entries[i]);
    return changed;
}

// Read the fields, persist the options and record the history
static void read_search_fields(void) {
    read_search_text();
    persist_options();
    bool changed = history_push("find_hist", g_search_text);
    if (g_ui.replace_entry)
        changed = history_push("replace_hist", g_replace_text) || changed;
    if (changed)
        settings_save();
    if (g_owner)
        refresh_highlights(g_owner, g_highlight_all);
}

// ---------------------------------------------------------------------------
// Searching
// ---------------------------------------------------------------------------

static GtkTextSearchFlags search_flags(void) {
    GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!g_match_case)
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    return flags;
}

static bool is_word_char(gunichar c) {
    return c == '_' || g_unichar_isalnum(c);
}

// Whole-word check for a hit. A boundary is only required beside a term edge
// that is itself a word character, so a term like "foo(" still matches.
static bool word_boundaries_ok(const GtkTextIter *start, const GtkTextIter *end) {
    if (!g_whole_word)
        return true;
    gunichar first = g_utf8_get_char(g_search_eff);
    gunichar last = g_utf8_get_char(g_utf8_prev_char(g_search_eff + strlen(g_search_eff)));
    if (is_word_char(first)) {
        GtkTextIter before = *start;
        if (gtk_text_iter_backward_char(&before) && is_word_char(gtk_text_iter_get_char(&before)))
            return false;
    }
    if (is_word_char(last) && !gtk_text_iter_is_end(end) &&
        is_word_char(gtk_text_iter_get_char(end)))
        return false;
    return true;
}

static bool search_forward(const GtkTextIter *from, GtkTextIter *match_start,
                           GtkTextIter *match_end) {
    GtkTextIter pos = *from;
    for (;;) {
        if (!gtk_text_iter_forward_search(&pos, g_search_eff, search_flags(), match_start,
                                          match_end, NULL))
            return false;
        if (word_boundaries_ok(match_start, match_end))
            return true;
        // Rejected: resume one character into the rejected hit
        pos = *match_start;
        if (!gtk_text_iter_forward_char(&pos))
            return false;
    }
}

// The last match ending at or before from
static bool search_backward(const GtkTextIter *from, GtkTextIter *match_start,
                            GtkTextIter *match_end) {
    GtkTextIter pos = *from;
    for (;;) {
        if (!gtk_text_iter_backward_search(&pos, g_search_eff, search_flags(), match_start,
                                           match_end, NULL))
            return false;
        if (word_boundaries_ok(match_start, match_end))
            return true;
        // Rejected: look for a match that ends one character earlier
        pos = *match_end;
        if (!gtk_text_iter_backward_char(&pos))
            return false;
    }
}

// Walk every match from the top (non-overlapping, like Win32). Stores up to
// cap ranges in out when it is given, and returns the total found (capped).
// ordinal, when given, receives the 1-based index of the match starting at
// ordinal_start (0 when none does).
static int walk_matches(Window *window, GArray *out, int cap, gint ordinal_start, int *ordinal) {
    if (ordinal)
        *ordinal = 0;
    if (!g_search_eff[0])
        return 0;
    GtkTextIter pos, start, end;
    gtk_text_buffer_get_start_iter(window->buffer, &pos);
    int total = 0;
    while (total < cap && search_forward(&pos, &start, &end)) {
        MatchRange range = { gtk_text_iter_get_offset(&start), gtk_text_iter_get_offset(&end) };
        total++;
        if (out)
            g_array_append_val(out, range);
        if (ordinal && range.start == ordinal_start)
            *ordinal = total;
        pos = end;
    }
    return total;
}

static void show_not_found(Window *window) {
    // The term is shown as typed, shortened if it is huge (by characters:
    // cutting bytes could split a UTF-8 sequence)
    char *shown = g_strdup(g_search_text);
    if (g_utf8_strlen(shown, -1) > 400)
        *g_utf8_offset_to_pointer(shown, 400) = '\0';
    GtkWindow *parent = g_dialog ? GTK_WINDOW(g_dialog) : GTK_WINDOW(window->window);
    GtkWidget *box =
        gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Cannot find \"%s\"", shown);
    gtk_window_set_title(GTK_WINDOW(box), "npad");
    gtk_dialog_run(GTK_DIALOG(box));
    gtk_widget_destroy(box);
    g_free(shown);
}

static void select_match(Window *window, const GtkTextIter *start, const GtkTextIter *end) {
    // Caret at the end of the match, like the Win32 selection
    gtk_text_buffer_select_range(window->buffer, end, start);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window->text_view),
                                       gtk_text_buffer_get_insert(window->buffer));
}

// Search from the current selection in the given direction, wrapping around if
// enabled, select the match, and report the result like Notepad
static bool find_next(Window *window, bool down) {
    ensure_loaded();
    if (!window || !g_search_eff[0])
        return false;

    GtkTextIter sel_start, sel_end, match_start, match_end;
    gtk_text_buffer_get_selection_bounds(window->buffer, &sel_start, &sel_end);

    bool hit = down ? search_forward(&sel_end, &match_start, &match_end)
                    : search_backward(&sel_start, &match_start, &match_end);
    bool wrapped = false;
    if (!hit && g_wrap_around) {
        GtkTextIter edge;
        if (down) {
            gtk_text_buffer_get_start_iter(window->buffer, &edge);
            hit = search_forward(&edge, &match_start, &match_end);
        } else {
            gtk_text_buffer_get_end_iter(window->buffer, &edge);
            hit = search_backward(&edge, &match_start, &match_end);
        }
        wrapped = hit;
    }

    if (!hit) {
        show_not_found(window);
        return false;
    }

    gint found_start = gtk_text_iter_get_offset(&match_start);
    select_match(window, &match_start, &match_end);

    int ordinal = 0;
    int total = walk_matches(window, NULL, FIND_MATCH_CAP, found_start, &ordinal);
    char message[128];
    snprintf(message, sizeof(message), "%sMatch %'d of %'d", wrapped ? "Wrapped around - " : "",
             ordinal, total);
    npad_gtk_set_status_message(window, message);
    return true;
}

// ---------------------------------------------------------------------------
// Replace
// ---------------------------------------------------------------------------

// Is the current selection exactly the current search text?
static bool selection_matches_search(Window *window) {
    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(window->buffer, &start, &end))
        return false;
    char *selected = gtk_text_buffer_get_text(window->buffer, &start, &end, TRUE);
    bool matches;
    if (g_match_case) {
        matches = strcmp(selected, g_search_eff) == 0;
    } else {
        char *a = g_utf8_casefold(selected, -1);
        char *b = g_utf8_casefold(g_search_eff, -1);
        matches = strcmp(a, b) == 0;
        g_free(a);
        g_free(b);
    }
    g_free(selected);
    return matches;
}

static void replace_current(Window *window) {
    if (!g_search_eff[0])
        return;
    if (selection_matches_search(window))
        npad_gtk_replace_selection(window, g_replace_eff);
    find_next(window, g_search_down);
}

static void replace_all(Window *window) {
    if (!g_search_eff[0])
        return;

    int replaced = 0;
    gint replacement_len = (gint) g_utf8_strlen(g_replace_eff, -1);
    gint resume = 0; // Offset to search from; stays valid across edits
    gint last_end = -1;

    // One user action: ui_gtk.c records everything inside as ONE undo group
    gtk_text_buffer_begin_user_action(window->buffer);
    for (;;) {
        GtkTextIter from, start, end;
        gtk_text_buffer_get_iter_at_offset(window->buffer, &from, resume);
        if (!search_forward(&from, &start, &end))
            break;
        gint at = gtk_text_iter_get_offset(&start);
        gtk_text_buffer_delete(window->buffer, &start, &end);
        gtk_text_buffer_get_iter_at_offset(window->buffer, &start, at);
        gtk_text_buffer_insert(window->buffer, &start, g_replace_eff, -1);
        resume = at + replacement_len;
        last_end = resume;
        replaced++;
    }
    gtk_text_buffer_end_user_action(window->buffer);

    char message[128];
    if (replaced > 0) {
        GtkTextIter caret;
        gtk_text_buffer_get_iter_at_offset(window->buffer, &caret, last_end);
        gtk_text_buffer_place_cursor(window->buffer, &caret);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window->text_view),
                                           gtk_text_buffer_get_insert(window->buffer));
        snprintf(message, sizeof(message), "Replaced %'d occurrence%s", replaced,
                 replaced == 1 ? "" : "s");
    } else {
        snprintf(message, sizeof(message), "Cannot find the search text");
    }
    // After the edits: any buffer change clears the status message
    npad_gtk_set_status_message(window, message);
}

// ---------------------------------------------------------------------------
// Highlight all
// ---------------------------------------------------------------------------

static GtkTextTag *highlight_tag(Window *window) {
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(window->buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(table, HIGHLIGHT_TAG);
    if (!tag) {
        // Translucent amber like the Win32 wash, so it reads on any scheme
        GdkRGBA amber = { 1.0, 0.82, 0.16, 0.45 };
        tag = gtk_text_buffer_create_tag(window->buffer, HIGHLIGHT_TAG, "background-rgba", &amber,
                                         NULL);
    }
    return tag;
}

static void refresh_highlights(Window *window, bool want) {
    // Nothing to clear and nothing wanted: every Find Next comes through here
    if (!window || (!want && !g_highlights_applied))
        return;
    GtkTextTag *tag = highlight_tag(window);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(window->buffer, &start, &end);
    gtk_text_buffer_remove_tag(window->buffer, tag, &start, &end);
    g_highlights_applied = false;
    if (!want || !g_search_eff[0])
        return;

    // Collect first: applying a tag invalidates iterators
    GArray *matches = g_array_new(FALSE, FALSE, sizeof(MatchRange));
    walk_matches(window, matches, FIND_MATCH_CAP, -1, NULL);
    for (guint i = 0; i < matches->len; i++) {
        const MatchRange *m = &g_array_index(matches, MatchRange, i);
        gtk_text_buffer_get_iter_at_offset(window->buffer, &start, m->start);
        gtk_text_buffer_get_iter_at_offset(window->buffer, &end, m->end);
        gtk_text_buffer_apply_tag(window->buffer, tag, &start, &end);
    }
    g_highlights_applied = matches->len > 0;
    g_array_free(matches, TRUE);
}

static gboolean on_highlight_timer(gpointer data) {
    (void) data;
    g_highlight_timer = 0;
    if (g_dialog && g_owner) {
        read_search_text();
        refresh_highlights(g_owner, g_highlight_all);
    }
    return G_SOURCE_REMOVE;
}

// Coalesce (not re-arm) so highlights refresh while you keep typing, not just
// after you stop
static void schedule_highlight_refresh(void) {
    if (g_highlight_timer || !g_dialog || !g_highlight_all)
        return;
    g_highlight_timer = g_timeout_add(FIND_HIGHLIGHT_DELAY_MS, on_highlight_timer, NULL);
}

static void on_document_changed(GtkTextBuffer *buffer, gpointer data) {
    (void) buffer;
    (void) data;
    schedule_highlight_refresh();
}

// ---------------------------------------------------------------------------
// The Find / Replace dialog
// ---------------------------------------------------------------------------

static void on_find_text_changed(GtkEditable *editable, gpointer data) {
    (void) editable;
    (void) data;
    if (!g_syncing)
        schedule_highlight_refresh();
}

static void on_option_toggled(GtkToggleButton *button, gpointer data) {
    (void) button;
    (void) data;
    if (g_syncing || !g_dialog)
        return;
    read_search_text();
    persist_options();
    if (g_owner)
        refresh_highlights(g_owner, g_highlight_all);
}

static void on_find_next_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    read_search_fields();
    if (g_owner)
        find_next(g_owner, g_search_down);
}

static void on_replace_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    read_search_fields();
    if (g_owner)
        replace_current(g_owner);
}

static void on_replace_all_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    read_search_fields();
    if (g_owner)
        replace_all(g_owner);
}

static void on_cancel_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    if (g_dialog)
        gtk_widget_destroy(g_dialog);
}

static gboolean on_dialog_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void) widget;
    (void) data;
    if (event->keyval == GDK_KEY_Escape) {
        if (g_dialog)
            gtk_widget_destroy(g_dialog);
        return TRUE;
    }
    // The main window's shortcuts keep working while the dialog has the focus
    if (event->keyval == GDK_KEY_F3 && g_owner) {
        read_search_fields();
        find_next(g_owner, !(event->state & GDK_SHIFT_MASK));
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && g_owner) {
        Window *owner = g_owner;
        switch (event->keyval) {
            case GDK_KEY_f:
                npad_gtk_handle_command(owner, NPAD_CMD_EDIT_FIND);
                return TRUE;
            case GDK_KEY_h:
                npad_gtk_handle_command(owner, NPAD_CMD_EDIT_REPLACE);
                return TRUE;
            case GDK_KEY_g:
                npad_gtk_handle_command(owner, NPAD_CMD_EDIT_GOTO_LINE);
                return TRUE;
            default:
                break;
        }
    }
    return FALSE;
}

static void on_dialog_destroy(GtkWidget *widget, gpointer data) {
    (void) widget;
    (void) data;
    Window *owner = g_owner;
    if (g_highlight_timer) {
        g_source_remove(g_highlight_timer);
        g_highlight_timer = 0;
    }
    g_dialog = NULL;
    g_owner = NULL;
    memset(&g_ui, 0, sizeof(g_ui));
    if (owner) {
        if (g_changed_handler)
            g_signal_handler_disconnect(owner->buffer, g_changed_handler);
        g_changed_handler = 0;
        refresh_highlights(owner, false); // Highlights go with the dialog
        // ...and the match count with them
        npad_gtk_set_status_message(owner, NULL);
        gtk_window_present(GTK_WINDOW(owner->window));
    }
}

// Show or hide the "Interpret escapes" row and shrink the dialog to fit
static void apply_escapes_visibility(void) {
    if (!g_dialog)
        return;
    bool show = settings_get_bool("list_tools_enabled", false);
    if (gtk_widget_get_visible(g_ui.escapes_check) == show)
        return;
    gtk_widget_set_visible(g_ui.escapes_check, show);
    gtk_window_resize(GTK_WINDOW(g_dialog), 1, 1);
}

static GtkWidget *add_history_row(GtkWidget *grid, int row, const char *label, const char *prefix,
                                  const char *initial) {
    GtkWidget *combo = gtk_combo_box_text_new_with_entry();
    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(combo));
    GtkWidget *text = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(text), entry);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    history_load(combo, prefix);
    gtk_entry_set_text(GTK_ENTRY(entry), initial);
    gtk_grid_attach(GTK_GRID(grid), text, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo, 1, row, 1, 1);
    return entry;
}

static GtkWidget *add_check(GtkWidget *box, const char *label, bool active) {
    GtkWidget *check = gtk_check_button_new_with_mnemonic(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
    g_signal_connect(check, "toggled", G_CALLBACK(on_option_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
    return check;
}

static GtkWidget *add_button(GtkWidget *box, const char *label, GCallback handler) {
    GtkWidget *button = gtk_button_new_with_mnemonic(label);
    g_signal_connect(button, "clicked", handler, NULL);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    return button;
}

static void show_find_replace(Window *parent, bool replace) {
    if (!parent)
        return;
    ensure_loaded();

    if (g_dialog) {
        // Keep whatever was typed, then either reuse the dialog or switch it
        read_search_text();
        if (g_dialog_replace == replace) {
            gtk_window_present(GTK_WINDOW(g_dialog));
            gtk_widget_grab_focus(g_ui.find_entry);
            gtk_editable_select_region(GTK_EDITABLE(g_ui.find_entry), 0, -1);
            return;
        }
        gtk_widget_destroy(g_dialog);
    }

    g_owner = parent;
    g_dialog_replace = replace;
    g_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWindow *window = GTK_WINDOW(g_dialog);
    gtk_window_set_title(window, replace ? "Replace" : "Find");
    gtk_window_set_transient_for(window, GTK_WINDOW(parent->window));
    gtk_window_set_destroy_with_parent(window, TRUE);
    gtk_window_set_type_hint(window, GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_resizable(window, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 12);
    g_signal_connect(window, "destroy", G_CALLBACK(on_dialog_destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_dialog_key), NULL);

    // Left: fields and options. Right: the action buttons.
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_add(GTK_CONTAINER(window), outer);
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), left, TRUE, TRUE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(left), grid, FALSE, FALSE, 0);
    g_ui.find_entry = add_history_row(grid, 0, "Fi_nd what:", "find_hist", g_search_text);
    if (replace)
        g_ui.replace_entry =
            add_history_row(grid, 1, "Re_place with:", "replace_hist", g_replace_text);
    g_signal_connect(g_ui.find_entry, "changed", G_CALLBACK(on_find_text_changed), NULL);

    GtkWidget *options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(left), options, FALSE, FALSE, 0);
    GtkWidget *checks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(options), checks, TRUE, TRUE, 0);
    g_ui.case_check = add_check(checks, "Match _case", g_match_case);
    g_ui.word_check = add_check(checks, "Match _whole word only", g_whole_word);
    g_ui.wrap_check = add_check(checks, "Wrap ar_ound", g_wrap_around);
    g_ui.highlight_check = add_check(checks, "_Highlight all matches", g_highlight_all);
    g_ui.escapes_check =
        add_check(checks, "Interpret _escapes (\\n \\t \\\\ \\uXXXX)", g_interpret_escapes);
    gtk_widget_set_no_show_all(g_ui.escapes_check, TRUE);

    if (!replace) {
        // Replace has no Direction group, like classic notepad.exe
        GtkWidget *frame = gtk_frame_new("Direction");
        GtkWidget *radios = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(radios), 6);
        g_ui.up_radio = gtk_radio_button_new_with_mnemonic(NULL, "_Up");
        g_ui.down_radio = gtk_radio_button_new_with_mnemonic_from_widget(
            GTK_RADIO_BUTTON(g_ui.up_radio), "_Down");
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(g_search_down ? g_ui.down_radio : g_ui.up_radio), TRUE);
        g_signal_connect(g_ui.up_radio, "toggled", G_CALLBACK(on_option_toggled), NULL);
        gtk_box_pack_start(GTK_BOX(radios), g_ui.up_radio, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(radios), g_ui.down_radio, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), radios);
        gtk_widget_set_valign(frame, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(options), frame, FALSE, FALSE, 0);
    }

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(outer), buttons, FALSE, FALSE, 0);
    GtkWidget *find_next_button =
        add_button(buttons, "_Find Next", G_CALLBACK(on_find_next_clicked));
    if (replace) {
        add_button(buttons, "_Replace", G_CALLBACK(on_replace_clicked));
        add_button(buttons, "Replace _All", G_CALLBACK(on_replace_all_clicked));
    }
    add_button(buttons, "Cancel", G_CALLBACK(on_cancel_clicked));
    gtk_widget_set_can_default(find_next_button, TRUE);
    gtk_window_set_default(window, find_next_button);

    gtk_widget_show_all(g_dialog);
    apply_escapes_visibility();

    // Live highlight while the document changes underneath the dialog
    g_changed_handler =
        g_signal_connect_after(parent->buffer, "changed", G_CALLBACK(on_document_changed), NULL);

    gtk_widget_grab_focus(g_ui.find_entry);
    gtk_editable_select_region(GTK_EDITABLE(g_ui.find_entry), 0, -1);
    gtk_window_present(window);

    read_search_text(); // Seeds the effective text from the remembered term
    if (g_highlight_all)
        refresh_highlights(parent, true);
}

void ui_platform_show_find_dialog(Window *parent) {
    show_find_replace(parent, false);
}

void ui_platform_show_replace_dialog(Window *parent) {
    show_find_replace(parent, true);
}

bool npad_gtk_find_has_term(void) {
    ensure_loaded();
    return g_search_eff[0] != '\0';
}

void npad_gtk_find_next(Window *window, bool down) {
    find_next(window, down);
}

// A settings broadcast: another instance (or Preferences) changed the options
void npad_gtk_find_reload_settings(void) {
    ensure_loaded();
    load_options();
    if (!g_dialog)
        return;
    // Keep what is typed; take the options from settings and show them
    g_syncing = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_ui.case_check), g_match_case);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_ui.word_check), g_whole_word);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_ui.wrap_check), g_wrap_around);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_ui.escapes_check), g_interpret_escapes);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_ui.highlight_check), g_highlight_all);
    if (g_ui.up_radio)
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(g_search_down ? g_ui.down_radio : g_ui.up_radio), TRUE);
    g_syncing = false;
    apply_escapes_visibility();
    recompute_effective();
    if (g_owner)
        refresh_highlights(g_owner, g_highlight_all);
}

void npad_gtk_find_cleanup(void) {
    if (g_dialog)
        gtk_widget_destroy(g_dialog);
}

// ---------------------------------------------------------------------------
// Go To Line
// ---------------------------------------------------------------------------

void npad_gtk_show_goto_dialog(Window *window) {
    if (!window)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Go To Line", GTK_WINDOW(window->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL, "_Go To", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 6);
    GtkWidget *entry = gtk_entry_new();
    GtkWidget *label = gtk_label_new_with_mnemonic("_Line number:");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_entry_set_text(GTK_ENTRY(entry), "1");
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_widget_grab_focus(entry);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    char *text = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dialog);

    // Digits only, like an ES_NUMBER edit; anything else (or zero) is ignored
    g_strstrip(text);
    bool valid = response == GTK_RESPONSE_OK && text[0];
    for (const char *p = text; valid && *p; p++)
        valid = *p >= '0' && *p <= '9';
    guint64 line = valid ? g_ascii_strtoull(text, NULL, 10) : 0;
    g_free(text);
    if (line == 0)
        return;

    // GtkTextBuffer lines are logical lines, even with word wrap on, so the
    // number matches what Ln shows and what the file contains
    if (line > (guint64) gtk_text_buffer_get_line_count(window->buffer)) {
        GtkWidget *box = gtk_message_dialog_new(
            GTK_WINDOW(window->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
            "The line number is beyond the total number of lines");
        gtk_window_set_title(GTK_WINDOW(box), "npad");
        gtk_dialog_run(GTK_DIALOG(box));
        gtk_widget_destroy(box);
        return;
    }

    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(window->buffer, &iter, (gint) line - 1);
    gtk_text_buffer_place_cursor(window->buffer, &iter);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window->text_view),
                                       gtk_text_buffer_get_insert(window->buffer));
    gtk_widget_grab_focus(window->text_view);
}
