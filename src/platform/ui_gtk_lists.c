/*
 * npad - GTK3 UI: the optional Markdown list tools
 *
 * Sort, unique, delimiter conversion, indent / unindent, list continuation on
 * Enter, cutting a line with Ctrl+X, and pasting rich text as lists or
 * Markdown. Everything here is gated on the list_tools_enabled setting (off by
 * default: out of the box npad behaves like Windows 10 notepad.exe), and the
 * transforms themselves live in core/list_ops.c and core/html_md.c. The Win32
 * counterparts are list_do, populate_list_menu, handle_markdown_tab,
 * handle_markdown_return, ui_platform_cut and do_paste in ui_win32.c.
 *
 * Every transform is one undo step: the edits are wrapped in a GTK user
 * action, which ui_gtk.c turns into a single undo group. Positions are GTK
 * character offsets, never byte indexes.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/html_md.h"
#include "../core/list_ops.h"
#include "../core/settings.h"

#define LIST_INDENT_FORMATS 7 // ListIndentFormat values, LIST_INDENT_CUSTOM included

// Which lines a transform acts on (Win32's list_target_range modes)
typedef enum {
    RANGE_SORT,   // a selection spanning lines -> those lines, else the whole document
    RANGE_INDENT, // the selected lines, or the caret's line
    RANGE_DELIM   // the exact selection when asked for, else the whole document
} RangeMode;

// op for list_do
typedef enum { OP_SORT, OP_UNIQUE, OP_INDENT, OP_UNINDENT } ListOp;

// The Markdown menu in the menu bar, and the settings it was built for
static GtkWidget *g_menu_item = NULL;
static GtkWidget *g_case_item = NULL; // Sort > Case Sensitive in that menu
static int g_menu_signature = -1;

// The text Ctrl+X last put on the clipboard as a whole line, while the
// clipboard still holds it: the next Ctrl+V then pastes it above the caret's
// line (Win32 compares the clipboard sequence number; GTK has none)
static char *g_cut_line = NULL;

static bool list_tools_enabled(void) {
    return settings_get_bool("list_tools_enabled", false);
}

static bool brackets_shortcut(void) {
    return settings_get_bool("list_indent_shortcut_brackets", false);
}

static GtkClipboard *clipboard_for(Window *window) {
    return gtk_widget_get_clipboard(window->text_view, GDK_SELECTION_CLIPBOARD);
}

static void scroll_to_cursor(Window *window) {
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window->text_view),
                                       gtk_text_buffer_get_insert(window->buffer));
}

static int char_count(Window *window) {
    return gtk_text_buffer_get_char_count(window->buffer);
}

static int cursor_offset(Window *window) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(window->buffer, &iter,
                                     gtk_text_buffer_get_insert(window->buffer));
    return gtk_text_iter_get_offset(&iter);
}

// The unescaped custom indent prefix, or NULL if none is configured. Caller
// frees.
static char *get_custom_indent(void) {
    char *raw = settings_get_string("list_custom_indent", NULL);
    if (!raw || !raw[0]) {
        free(raw);
        return NULL;
    }
    char *unescaped = list_unescape(raw);
    free(raw);
    return unescaped;
}

// ---------------------------------------------------------------------------
// Ranges, extraction and replacement
// ---------------------------------------------------------------------------

// The logical line (up to its '\n', not including it) holding offset. Lines
// here are paragraphs, so word wrap does not matter.
static void line_bounds(GtkTextBuffer *buffer, int offset, int *start, int *end) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset);
    gtk_text_iter_set_line_offset(&iter, 0);
    *start = gtk_text_iter_get_offset(&iter);
    // forward_to_line_end would skip to the NEXT line when already at the end
    if (!gtk_text_iter_ends_line(&iter))
        gtk_text_iter_forward_to_line_end(&iter);
    *end = gtk_text_iter_get_offset(&iter);
}

static void target_range(Window *window, RangeMode mode, bool sel_only, int *out_min,
                         int *out_max) {
    GtkTextBuffer *buffer = window->buffer;
    int total = char_count(window);
    GtkTextIter a, b;
    bool has_sel = gtk_text_buffer_get_selection_bounds(buffer, &a, &b);
    int sel_min = has_sel ? gtk_text_iter_get_offset(&a) : cursor_offset(window);
    int sel_max = has_sel ? gtk_text_iter_get_offset(&b) : sel_min;

    if (mode == RANGE_DELIM) {
        *out_min = (sel_only && has_sel) ? sel_min : 0;
        *out_max = (sel_only && has_sel) ? sel_max : total;
        return;
    }

    int first_start, first_end;
    line_bounds(buffer, sel_min, &first_start, &first_end);
    bool spans_newline = has_sel && first_end < sel_max;

    // A selection ending exactly at a line start does not include that line
    int last_pos = sel_max;
    if (spans_newline) {
        int ls, le;
        line_bounds(buffer, last_pos, &ls, &le);
        if (ls == last_pos)
            last_pos--;
    }
    int last_start, last_end;
    line_bounds(buffer, last_pos, &last_start, &last_end);

    if (mode == RANGE_SORT && !spans_newline) {
        *out_min = 0;
        *out_max = total;
    } else {
        *out_min = first_start;
        *out_max = last_end;
    }
}

static char *extract(Window *window, int from, int to) {
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(window->buffer, &a, from);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &b, to);
    return gtk_text_buffer_get_text(window->buffer, &a, &b, TRUE);
}

static void select_offsets(Window *window, int from, int to) {
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(window->buffer, &a, from);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &b, to);
    gtk_text_buffer_select_range(window->buffer, &b, &a); // Caret at the end
    scroll_to_cursor(window);
}

// Replace [from,to) with utf8 as one undo step and select the result. A
// transform that changed nothing leaves the document (and its modified flag)
// alone.
static void replace_range(Window *window, int from, int to, const char *old_text,
                          const char *utf8) {
    char *text = npad_gtk_normalize_text(utf8); // A delimiter conversion may inject CR
    if (!text)
        return;
    if (strcmp(text, old_text) != 0) {
        GtkTextBuffer *buffer = window->buffer;
        GtkTextIter a, b;
        gtk_text_buffer_begin_user_action(buffer);
        gtk_text_buffer_get_iter_at_offset(buffer, &a, from);
        gtk_text_buffer_get_iter_at_offset(buffer, &b, to);
        gtk_text_buffer_delete(buffer, &a, &b);
        gtk_text_buffer_get_iter_at_offset(buffer, &a, from);
        gtk_text_buffer_insert(buffer, &a, text, -1);
        gtk_text_buffer_end_user_action(buffer);
        to = from + (int) g_utf8_strlen(text, -1);
    }
    select_offsets(window, from, to);
    g_free(text);
}

// ---------------------------------------------------------------------------
// The transforms
// ---------------------------------------------------------------------------

static void list_do(Window *window, ListOp op, int arg) {
    if (!window || !list_tools_enabled())
        return;
    int from, to;
    target_range(window, (op == OP_SORT || op == OP_UNIQUE) ? RANGE_SORT : RANGE_INDENT, false,
                 &from, &to);
    char *text = extract(window, from, to);
    if (!text)
        return;

    char *out = NULL;
    switch (op) {
        case OP_SORT:
            out = list_sort_lines(text, arg != 0,
                                  settings_get_bool("list_sort_case_sensitive", false));
            break;
        case OP_UNIQUE:
            out = list_unique_lines(text);
            break;
        case OP_INDENT:
        case OP_UNINDENT: {
            // The custom prefix goes in for every format, so lines carrying a
            // custom bullet still deepen / unindent under a built-in format
            char *custom = get_custom_indent();
            out = (op == OP_INDENT) ? list_indent_lines(text, (ListIndentFormat) arg, custom)
                                    : list_unindent_lines(text, (ListIndentFormat) arg, custom);
            free(custom);
            break;
        }
    }
    if (out) {
        replace_range(window, from, to, text, out);
        free(out);
    }
    g_free(text);
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

// A dialog's entry takes the PRIMARY selection when it selects its text, and
// GtkTextBuffer collapses its own selection the moment it loses that - so the
// selection a tool was started on is gone by the time the dialog closes. Note
// it first and put it back afterwards.
typedef struct {
    bool had;
    int caret; // The end the caret was on
    int other; // The other end
} SavedSelection;

static SavedSelection save_selection(Window *window) {
    SavedSelection saved = { false, 0, 0 };
    GtkTextIter a, b;
    if (gtk_text_buffer_get_selection_bounds(window->buffer, &a, &b)) {
        GtkTextIter insert;
        gtk_text_buffer_get_iter_at_mark(window->buffer, &insert,
                                         gtk_text_buffer_get_insert(window->buffer));
        saved.had = true;
        saved.caret = gtk_text_iter_get_offset(&insert);
        saved.other = saved.caret == gtk_text_iter_get_offset(&a) ? gtk_text_iter_get_offset(&b)
                                                                  : gtk_text_iter_get_offset(&a);
    }
    return saved;
}

static void restore_selection(Window *window, const SavedSelection *saved) {
    if (!saved->had || gtk_text_buffer_get_has_selection(window->buffer))
        return;
    GtkTextIter caret, other;
    gtk_text_buffer_get_iter_at_offset(window->buffer, &caret, saved->caret);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &other, saved->other);
    gtk_text_buffer_select_range(window->buffer, &caret, &other);
}

// Custom Indent prompt: asks for the custom prefix (raw, escapes allowed) and
// saves it. Returns true when a prefix was entered, i.e. the caller should go
// on and indent.
static bool show_custom_indent_dialog(Window *window) {
    SavedSelection selection = save_selection(window);
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Custom Indent", GTK_WINDOW(window->window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
                                    GTK_RESPONSE_CANCEL, "_Indent", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 6);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 127);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 32);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    GtkWidget *label = gtk_label_new_with_mnemonic("Custom indent _prefix:");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    GtkWidget *hint = gtk_label_new("Escapes: \\t tab  \\\\ backslash  \\uXXXX");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);

    char *saved = settings_get_string("list_custom_indent", "");
    gtk_entry_set_text(GTK_ENTRY(entry), saved ? saved : "");
    free(saved);
    gtk_widget_show_all(content);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_widget_grab_focus(entry);

    bool ok = false;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *raw = gtk_entry_get_text(GTK_ENTRY(entry));
        if (raw[0]) {
            // Stored raw (as typed); unescaped at the point of use
            settings_set_string("list_custom_indent", raw);
            settings_save();
            ui_notify_settings_changed();
            ok = true;
        }
    }
    gtk_widget_destroy(dialog);
    restore_selection(window, &selection);
    return ok;
}

// Indent / unindent with the configured default format (menu item, Tab, or
// Ctrl+] / Ctrl+[). A Custom default with no prefix configured prompts first.
static void indent_default(Window *window, bool unindent) {
    int fmt = settings_get_int("list_default_indent_format", 0);
    if (fmt < 0 || fmt >= LIST_INDENT_FORMATS)
        fmt = 0;
    if (fmt == LIST_INDENT_CUSTOM) {
        char *custom = get_custom_indent();
        if (!custom) {
            if (!show_custom_indent_dialog(window))
                return;
        } else {
            free(custom);
        }
    }
    list_do(window, unindent ? OP_UNINDENT : OP_INDENT, fmt);
}

static bool combo_has_text(GtkComboBoxText *combo, const char *text) {
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    GtkTreeIter iter;
    for (gboolean more = gtk_tree_model_get_iter_first(model, &iter); more;
         more = gtk_tree_model_iter_next(model, &iter)) {
        gchar *value = NULL;
        gtk_tree_model_get(model, &iter, 0, &value, -1);
        bool same = g_strcmp0(value, text) == 0;
        g_free(value);
        if (same)
            return true;
    }
    return false;
}

// One From / To row: recent values first (the same history machinery as
// Find, kept under the same keys as the Windows build), then the usual
// delimiters, with the last conversion's value showing
static GtkWidget *add_delimiter_row(GtkWidget *grid, int row, const char *label, const char *prefix,
                                    const char *fallback) {
    static const char *const presets[] = { ",", ";", "|", "\\t", "\\n", "\\r\\n", " " };
    GtkWidget *combo = gtk_combo_box_text_new_with_entry();
    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(combo));
    npad_gtk_history_load(combo, prefix);
    for (size_t i = 0; i < G_N_ELEMENTS(presets); i++) {
        if (!combo_has_text(GTK_COMBO_BOX_TEXT(combo), presets[i]))
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), presets[i]);
    }

    char key[48];
    snprintf(key, sizeof(key), "%s_0", prefix);
    char *last = settings_get_string(key, fallback);
    gtk_entry_set_text(GTK_ENTRY(entry), (last && last[0]) ? last : fallback);
    free(last);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_entry_set_max_length(GTK_ENTRY(entry), 255);

    GtkWidget *text = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(text), entry);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_widget_set_hexpand(combo, TRUE);
    gtk_grid_attach(GTK_GRID(grid), text, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo, 1, row, 1, 1);
    return entry;
}

// Reverse a conversion without retyping either field
static void on_swap_clicked(GtkButton *button, gpointer data) {
    (void) button;
    GtkWidget **entries = data;
    char *from = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[0])));
    gtk_entry_set_text(GTK_ENTRY(entries[0]), gtk_entry_get_text(GTK_ENTRY(entries[1])));
    gtk_entry_set_text(GTK_ENTRY(entries[1]), from);
    g_free(from);
}

// Replace every From with To in the selection (when asked) or the document
static void convert_delimiters(Window *window, const char *from, const char *to, bool sel_only) {
    char *from_u = list_unescape(from);
    char *to_u = list_unescape(to);
    // The buffer holds bare '\n', so a literal \r\n source could never match.
    // npad taught users that token itself (it used to be the default target),
    // so accept it and mean \n.
    char *plain_from = from_u ? npad_gtk_normalize_text(from_u) : NULL;
    if (plain_from && plain_from[0]) {
        int lo, hi;
        target_range(window, RANGE_DELIM, sel_only, &lo, &hi);
        char *text = extract(window, lo, hi);
        if (text) {
            char *out = list_replace_all(text, plain_from, to_u ? to_u : "");
            if (out) {
                replace_range(window, lo, hi, text, out);
                free(out);
            }
            g_free(text);
        }
    }
    g_free(plain_from);
    free(from_u);
    free(to_u);
}

static void show_convert_delim_dialog(Window *window) {
    SavedSelection selection = save_selection(window);
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Convert Delimiters", GTK_WINDOW(window->window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
                                    GTK_RESPONSE_CANCEL, "_Convert", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    GtkWidget *entries[2];
    entries[0] = add_delimiter_row(grid, 0, "_From:", "delim_from_hist", ",");
    entries[1] = add_delimiter_row(grid, 1, "_To:", "delim_to_hist", "\\n");
    GtkWidget *swap = gtk_button_new_with_mnemonic("S_wap");
    gtk_widget_set_valign(swap, GTK_ALIGN_CENTER);
    g_signal_connect(swap, "clicked", G_CALLBACK(on_swap_clicked), entries);
    gtk_grid_attach(GTK_GRID(grid), swap, 2, 0, 1, 2);
    gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

    GtkWidget *hint =
        gtk_label_new("Escapes: \\n newline  \\r CR  \\t tab  \\\\ backslash  \\uXXXX");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);

    bool has_sel = selection.had;
    GtkWidget *sel_only = gtk_check_button_new_with_mnemonic("_Selection only");
    gtk_widget_set_sensitive(sel_only, has_sel);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sel_only), has_sel);
    gtk_box_pack_start(GTK_BOX(content), sel_only, FALSE, FALSE, 0);

    gtk_widget_show_all(content);
    gtk_widget_grab_focus(entries[0]);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        char *from = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[0])));
        char *to = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[1])));
        bool only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sel_only));
        gtk_widget_destroy(dialog);
        dialog = NULL;
        restore_selection(window, &selection);

        // Remember what was used, so the next open starts from here
        bool changed = npad_gtk_history_push("delim_from_hist", from);
        changed = npad_gtk_history_push("delim_to_hist", to) || changed;
        if (changed) {
            settings_save();
            ui_notify_settings_changed();
        }
        convert_delimiters(window, from, to, only);
        g_free(from);
        g_free(to);
    }
    if (dialog) {
        gtk_widget_destroy(dialog);
        restore_selection(window, &selection);
    }
}

// ---------------------------------------------------------------------------
// Keys: Tab / Shift+Tab and Enter
// ---------------------------------------------------------------------------

// Tab / Shift+Tab indent handling (the default binding). Shift+Tab always
// unindents - the selected lines, or the caret's - because a literal tab is
// never what it means; plain Tab indents only a selection, and otherwise types
// a tab, classic Notepad behaviour.
static bool handle_tab(Window *window, bool shift) {
    if (brackets_shortcut())
        return false;
    if (shift) {
        indent_default(window, true);
        return true;
    }
    if (gtk_text_buffer_get_has_selection(window->buffer)) {
        indent_default(window, false);
        return true;
    }
    return false;
}

// Enter on a list line continues the marker on the new line; Enter on an
// empty bullet removes the marker and leaves a plain newline (ends the list).
// Returns true when the keystroke was consumed.
static bool handle_return(Window *window) {
    GtkTextBuffer *buffer = window->buffer;
    if (gtk_text_buffer_get_has_selection(buffer))
        return false; // Selection: the stock replace-selection Enter

    int caret = cursor_offset(window);
    int start, end;
    line_bounds(buffer, caret, &start, &end);
    if (end <= start)
        return false;

    // The head of the line; any real prefix fits well within this
    char *head = extract(window, start, (end - start > 256) ? start + 256 : end);
    size_t head_len = strlen(head);
    size_t ws = 0;
    while (ws < head_len && (head[ws] == ' ' || head[ws] == '\t'))
        ws++;
    char *custom = get_custom_indent();
    // A full marker (its trailing space included), so a lone "-" the user has
    // just typed is left alone
    size_t marker = list_marker_length(head + ws, head_len - ws, custom, false);
    size_t prefix_bytes = ws + marker;
    int prefix_chars = (int) g_utf8_strlen(head, (gssize) prefix_bytes);
    if (marker == 0 ||
        caret < start + prefix_chars) { // No list line, or the caret is in the prefix
        g_free(head);
        free(custom);
        return false;
    }

    bool handled = true;
    if (end - start == prefix_chars) {
        // Empty bullet: end the list - drop the marker, keep the newline
        gtk_text_buffer_begin_user_action(buffer);
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(buffer, &a, start);
        gtk_text_buffer_get_iter_at_offset(buffer, &b, caret);
        gtk_text_buffer_delete(buffer, &a, &b);
        gtk_text_buffer_get_iter_at_offset(buffer, &a, start);
        gtk_text_buffer_insert(buffer, &a, "\n", 1);
        gtk_text_buffer_end_user_action(buffer);
        GtkTextIter after;
        gtk_text_buffer_get_iter_at_offset(buffer, &after, start + 1);
        gtk_text_buffer_place_cursor(buffer, &after);
    } else {
        // Splitting right before an existing bullet must not mint another
        // marker ("- item |- second" would become "- - second"): the text
        // after the caret already starts its own bullet, so let the plain
        // newline through instead
        bool at_end = end - caret <= 64;
        char *rest = extract(window, caret, at_end ? end : caret + 64);
        size_t rest_len = strlen(rest);
        size_t rws = 0;
        while (rws < rest_len && (rest[rws] == ' ' || rest[rws] == '\t'))
            rws++;
        size_t m = list_marker_length(rest + rws, rest_len - rws, custom, at_end);
        // A match that exactly fills a truncated fetch window is inconclusive;
        // trust it only when we saw the real line end
        bool rem_bullet = m > 0 && (rws + m < rest_len || at_end);
        g_free(rest);
        if (rem_bullet) {
            handled = false;
        } else {
            // Continue the list: a newline and the same whitespace + marker
            // (which splits the line when the caret is mid-content)
            char *prefix = g_strndup(head, prefix_bytes);
            char *insert = g_strconcat("\n", prefix, NULL);
            g_free(prefix);
            gtk_text_buffer_begin_user_action(buffer);
            gtk_text_buffer_insert_at_cursor(buffer, insert, -1);
            gtk_text_buffer_end_user_action(buffer);
            g_free(insert);
        }
    }
    g_free(head);
    free(custom);
    if (handled)
        scroll_to_cursor(window);
    return handled;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void) widget;
    Window *window = data;
    if (!list_tools_enabled())
        return FALSE;
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    switch (event->keyval) {
        case GDK_KEY_Tab:
        case GDK_KEY_ISO_Left_Tab:
            // Ctrl+Tab and Alt+Tab are not ours
            if (mods & ~GDK_SHIFT_MASK)
                return FALSE;
            return handle_tab(window, event->keyval == GDK_KEY_ISO_Left_Tab ||
                                          (mods & GDK_SHIFT_MASK) != 0);
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (mods & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK))
                return FALSE;
            return handle_return(window) ? TRUE : FALSE;
        default:
            return FALSE;
    }
}

// ---------------------------------------------------------------------------
// Ctrl+X cuts the line; the next Ctrl+V pastes it above
// ---------------------------------------------------------------------------

bool npad_gtk_lists_cut_line(Window *window) {
    g_free(g_cut_line); // Any cut ends paste-above mode
    g_cut_line = NULL;
    if (!window || !list_tools_enabled() || gtk_text_buffer_get_has_selection(window->buffer))
        return false;

    // Ctrl+X with nothing selected cuts the whole logical line, including its
    // line break, onto the clipboard
    int start, end;
    line_bounds(window->buffer, cursor_offset(window), &start, &end);
    if (end < char_count(window))
        end++; // Consume the following '\n'
    else if (start > 0)
        start--; // Last line: consume the preceding break instead
    if (start == end)
        return true; // Empty document: nothing to cut, clipboard untouched

    char *text = extract(window, start, end);
    GtkTextIter a, b;
    gtk_text_buffer_begin_user_action(window->buffer);
    gtk_clipboard_set_text(clipboard_for(window), text, -1);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &a, start);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &b, end);
    gtk_text_buffer_delete(window->buffer, &a, &b);
    gtk_text_buffer_end_user_action(window->buffer);
    scroll_to_cursor(window);
    g_cut_line = text;
    return true;
}

// Paste the line cut by Ctrl+X back ABOVE the caret's line, keeping the caret
// where it is within its own line. It stays in effect for repeat pastes until
// something else is on the clipboard. Always plain.
static bool paste_line_above(Window *window) {
    if (!g_cut_line || gtk_text_buffer_get_has_selection(window->buffer))
        return false;
    char *clip = gtk_clipboard_wait_for_text(clipboard_for(window));
    bool same = clip && strcmp(clip, g_cut_line) == 0;
    g_free(clip);
    if (!same)
        return false;

    // A last-line cut starts with the preceding break: strip it, then
    // guarantee a trailing break so the insert lands as a full line
    const char *body = g_cut_line;
    if (body[0] == '\n')
        body++;
    char *insert = (body[0] && body[strlen(body) - 1] == '\n') ? g_strdup(body)
                                                               : g_strconcat(body, "\n", NULL);

    int caret = cursor_offset(window);
    int line_start, line_end;
    line_bounds(window->buffer, caret, &line_start, &line_end);
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_offset(window->buffer, &at, line_start);
    gtk_text_buffer_begin_user_action(window->buffer);
    gtk_text_buffer_insert(window->buffer, &at, insert, -1);
    gtk_text_buffer_end_user_action(window->buffer);
    int fin = line_start + (int) g_utf8_strlen(insert, -1) + (caret - line_start);
    gtk_text_buffer_get_iter_at_offset(window->buffer, &at, fin);
    gtk_text_buffer_place_cursor(window->buffer, &at);
    scroll_to_cursor(window);
    g_free(insert);
    return true;
}

// ---------------------------------------------------------------------------
// Pasting rich text
// ---------------------------------------------------------------------------

static GdkAtom html_atom(void) {
    return gdk_atom_intern_static_string("text/html");
}

// The copied fragment of a text/html clipboard payload, as UTF-8. X11 sources
// differ in encoding: browsers typically offer UTF-16 with a BOM, others
// UTF-8 (with or without a BOM), so sniff it. NULL when nothing usable.
static char *html_payload_to_fragment(const guchar *bytes, gint length) {
    if (!bytes || length < 2)
        return NULL;
    const char *from = NULL;
    gint skip = 0;
    if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
        from = "UTF-16LE";
        skip = 2;
    } else if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
        from = "UTF-16BE";
        skip = 2;
    } else if (bytes[0] != 0 && bytes[1] == 0) { // '<' then NUL: UTF-16LE without a BOM
        from = "UTF-16LE";
    } else if (bytes[0] == 0 && bytes[1] != 0) {
        from = "UTF-16BE";
    }

    char *utf8;
    if (from) {
        utf8 = g_convert((const gchar *) bytes + skip, (length - skip) & ~1, "UTF-8", from, NULL,
                         NULL, NULL);
    } else {
        skip = (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
        utf8 = g_utf8_make_valid((const gchar *) bytes + skip, length - skip);
    }
    if (!utf8)
        return NULL;
    char *fragment = html_cf_extract_fragment(utf8);
    g_free(utf8);
    return fragment;
}

// The clipboard's text/html as a UTF-8 fragment, or NULL
static char *read_clipboard_html(Window *window) {
    GtkClipboard *clipboard = clipboard_for(window);
    if (!gtk_clipboard_wait_is_target_available(clipboard, html_atom()))
        return NULL;
    GtkSelectionData *data = gtk_clipboard_wait_for_contents(clipboard, html_atom());
    if (!data)
        return NULL;
    char *fragment = html_payload_to_fragment(gtk_selection_data_get_data(data),
                                              gtk_selection_data_get_length(data));
    gtk_selection_data_free(data);
    return fragment;
}

// The bullet for pasted list items, in the user's indent style (a "- "
// fallback for the whitespace formats). Caller frees.
static char *paste_resolve_marker(void) {
    int fmt = settings_get_int("list_default_indent_format", 0);
    if (fmt < 0 || fmt >= LIST_INDENT_FORMATS)
        fmt = 0;
    char *custom = get_custom_indent();
    char *marker = list_bullet_marker((ListIndentFormat) fmt, custom);
    free(custom);
    return marker;
}

// mode: 0 = plain, 1 = lists, 2 = full Markdown. Returns false when the paste
// is a plain one (the caller does it); true when handled here. Rich modes
// convert the clipboard's HTML when it has any, and otherwise fall back to a
// normal paste.
static bool do_paste(Window *window, int mode) {
    if (paste_line_above(window))
        return true;
    g_free(g_cut_line);
    g_cut_line = NULL;
    if (mode == 0)
        return false;

    char *html = read_clipboard_html(window);
    if (!html)
        return false;
    char *marker = paste_resolve_marker();
    char *md = html_to_markdown(html, mode == 2 ? HTML_MD_FULL : HTML_MD_LISTS, marker);
    free(html);
    free(marker);
    bool done = false;
    if (md && md[0]) {
        char *text = npad_gtk_normalize_text(md); // Guard against a stray CR (&#13;)
        if (text) {
            npad_gtk_replace_selection(window, text); // One undo step
            g_free(text);
            done = true;
        }
    }
    free(md);
    return done;
}

bool npad_gtk_lists_paste(Window *window, NpadPasteKind kind) {
    if (!window || !list_tools_enabled())
        return false;
    int mode = 0;
    switch (kind) {
        case NPAD_PASTE_PRIMARY: // Ctrl+V: the configured primary mode, lists by default
            mode = settings_get_int("markdown_paste_primary", 1);
            if (mode < 0 || mode > 2)
                mode = 1; // A corrupt / imported value: the default
            break;
        case NPAD_PASTE_ALT: // Ctrl+Shift+V: the configured alternate, plain by default
            mode = settings_get_int("markdown_paste_alt", 0);
            if (mode < 0 || mode > 2)
                mode = 0;
            break;
        case NPAD_PASTE_MARKDOWN: // The menu item is always a full conversion
            mode = 2;
            break;
    }
    return do_paste(window, mode);
}

// ---------------------------------------------------------------------------
// Commands and their greying
// ---------------------------------------------------------------------------

static bool is_list_command(NpadCommand command) {
    return (command >= NPAD_CMD_LIST_SORT_ASC && command <= NPAD_CMD_LIST_INDENT_FORMAT_LAST) ||
           command == NPAD_CMD_EDIT_PASTE_MARKDOWN;
}

bool npad_gtk_lists_command_enabled(Window *window, NpadCommand command) {
    if (!window)
        return false;
    switch (command) {
        case NPAD_CMD_LIST_SORT_CASE:
            return true; // A setting, not an operation on the text
        case NPAD_CMD_EDIT_PASTE_MARKDOWN: {
            // It can also convert an HTML-only clipboard, which carries no
            // plain text, so it is enabled slightly more often than Paste
            GtkClipboard *clipboard = clipboard_for(window);
            return gtk_clipboard_wait_is_text_available(clipboard) ||
                   gtk_clipboard_wait_is_target_available(clipboard, html_atom());
        }
        default: // They work on lines, so they need a non-empty document
            return char_count(window) > 0;
    }
}

// Run a list command. Returns false for a command that is not one of ours.
bool npad_gtk_lists_command(Window *window, NpadCommand command) {
    if (!window)
        return false;
    if (command == NPAD_CMD_EDIT_PASTE_ALT) {
        if (!npad_gtk_lists_paste(window, NPAD_PASTE_ALT))
            npad_gtk_paste_plain(window);
        return true;
    }
    if (!is_list_command(command))
        return false;
    if (!list_tools_enabled())
        return true; // Inert while the tools are off

    if (command >= NPAD_CMD_LIST_INDENT_FORMAT && command <= NPAD_CMD_LIST_INDENT_FORMAT_LAST) {
        int fmt = command - NPAD_CMD_LIST_INDENT_FORMAT;
        // Custom... always prompts (prefilled with the saved prefix), then indents
        if (fmt == LIST_INDENT_CUSTOM && !show_custom_indent_dialog(window))
            return true;
        list_do(window, OP_INDENT, fmt);
        return true;
    }
    switch (command) {
        case NPAD_CMD_LIST_SORT_ASC:
            list_do(window, OP_SORT, 0);
            break;
        case NPAD_CMD_LIST_SORT_DESC:
            list_do(window, OP_SORT, 1);
            break;
        case NPAD_CMD_LIST_SORT_CASE:
            settings_set_bool("list_sort_case_sensitive",
                              !settings_get_bool("list_sort_case_sensitive", false));
            settings_save();
            ui_notify_settings_changed();
            break;
        case NPAD_CMD_LIST_UNIQUE:
            list_do(window, OP_UNIQUE, 0);
            break;
        case NPAD_CMD_LIST_CONVERT_DELIM:
            show_convert_delim_dialog(window);
            break;
        case NPAD_CMD_LIST_INDENT:
            indent_default(window, false);
            break;
        case NPAD_CMD_LIST_UNINDENT:
            indent_default(window, true);
            break;
        case NPAD_CMD_EDIT_PASTE_MARKDOWN:
            if (!npad_gtk_lists_paste(window, NPAD_PASTE_MARKDOWN))
                npad_gtk_paste_plain(window);
            break;
        default:
            break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The Markdown menu and its context-menu mirror
// ---------------------------------------------------------------------------

static void on_item_activate(GtkMenuItem *item, gpointer data) {
    (void) item;
    Window *window = g_gtk_main_window;
    // Not while code (rather than the user) is flipping a check mark
    if (!window || window->syncing_menu)
        return;
    npad_gtk_handle_command(window, (NpadCommand) GPOINTER_TO_INT(data));
}

// Append one item. The menu bar and the context menu need different
// mnemonics (the popup already spends U R T C P D A on the edit items), hence
// the two labels.
static GtkWidget *add_list_item(Window *window, GtkWidget *menu, GtkWidget *item,
                                NpadCommand command) {
    g_object_set_data(G_OBJECT(item), "npad-cmd", GINT_TO_POINTER(command));
    g_signal_connect(item, "activate", G_CALLBACK(on_item_activate), GINT_TO_POINTER(command));
    gtk_widget_set_sensitive(item, npad_gtk_lists_command_enabled(window, command));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

static GtkWidget *add_text_item(Window *window, GtkWidget *menu, const char *label,
                                NpadCommand command) {
    return add_list_item(window, menu, gtk_menu_item_new_with_mnemonic(label), command);
}

static GtkWidget *add_submenu_item(GtkWidget *menu, const char *label) {
    GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
    GtkWidget *sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return sub;
}

static void add_sep(GtkWidget *menu) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
}

// Show a shortcut beside an item without binding it: Tab and Shift+Tab are
// handled by the text view itself (a Tab accelerator would swallow every tab)
static void show_accel_only(GtkWidget *item, guint key, GdkModifierType mods) {
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (GTK_IS_ACCEL_LABEL(child))
        gtk_accel_label_set_accel(GTK_ACCEL_LABEL(child), key, mods);
}

// Append the Markdown tools to a menu: the Markdown menu on the bar, or the
// tail of the right-click menu
static void populate_list_menu(Window *window, GtkWidget *menu, bool context) {
    // The indent / unindent labels track the active binding. Tab and Shift+Tab
    // are only shown (the text view handles them); Ctrl+] and Ctrl+[ are real
    // accelerators on the menu bar's items (bind_bracket_accels) and only
    // shown in the popup, which has no accelerator group.
    bool brackets = brackets_shortcut();

    GtkWidget *sort = add_submenu_item(menu, "_Sort");
    add_text_item(window, sort, "_Ascending", NPAD_CMD_LIST_SORT_ASC);
    add_text_item(window, sort, "_Descending", NPAD_CMD_LIST_SORT_DESC);
    add_sep(sort);
    GtkWidget *case_item = gtk_check_menu_item_new_with_mnemonic("_Case Sensitive");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(case_item),
                                   settings_get_bool("list_sort_case_sensitive", false));
    add_list_item(window, sort, case_item, NPAD_CMD_LIST_SORT_CASE);
    if (!context)
        g_case_item = case_item;

    add_text_item(window, menu, "Uni_que", NPAD_CMD_LIST_UNIQUE);
    add_sep(menu);
    add_text_item(window, menu, context ? "Convert De_limiters..." : "Convert _Delimiters...",
                  NPAD_CMD_LIST_CONVERT_DELIM);
    add_sep(menu);

    GtkWidget *indent = add_submenu_item(menu, context ? "I_ndent" : "Inden_t");
    GtkWidget *def = add_text_item(window, indent, "_Indent (default)", NPAD_CMD_LIST_INDENT);
    add_sep(indent);
    static const char *const formats[LIST_INDENT_FORMATS] = {
        "_Spaces",
        "_Tab",
        "_Asterisk (\"* \")",
        "_Hyphen (\"- \")",
        "Asterisk, _leading space (\" * \")",
        "Hyphen, l_eading space (\" - \")",
        "_Custom...",
    };
    for (int i = 0; i < LIST_INDENT_FORMATS; i++)
        add_text_item(window, indent, formats[i], NPAD_CMD_LIST_INDENT_FORMAT + i);
    GtkWidget *unindent =
        add_text_item(window, menu, context ? "Un_indent" : "_Unindent", NPAD_CMD_LIST_UNINDENT);
    if (brackets) {
        if (context) {
            show_accel_only(def, GDK_KEY_bracketright, GDK_CONTROL_MASK);
            show_accel_only(unindent, GDK_KEY_bracketleft, GDK_CONTROL_MASK);
        }
    } else {
        show_accel_only(def, GDK_KEY_Tab, 0);
        show_accel_only(unindent, GDK_KEY_Tab, GDK_SHIFT_MASK);
    }

    add_sep(menu);
    // Rich-text paste, fully converted to Markdown (Ctrl+V / Ctrl+Shift+V use
    // the modes chosen in Preferences; this item is always a full conversion)
    add_text_item(window, menu, context ? "Paste as _Markdown" : "_Paste as Markdown",
                  NPAD_CMD_EDIT_PASTE_MARKDOWN);
}

void npad_gtk_lists_populate_context_menu(Window *window, GtkWidget *menu) {
    if (!window || !list_tools_enabled())
        return;
    add_sep(menu);
    populate_list_menu(window, menu, true);
}

// Recolour the greying of every tool in a menu (and its submenus)
static void refresh_states(Window *window, GtkWidget *menu, bool all_on) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *item = l->data;
        gpointer cmd = g_object_get_data(G_OBJECT(item), "npad-cmd");
        if (cmd)
            gtk_widget_set_sensitive(
                item, all_on || npad_gtk_lists_command_enabled(window,
                                                               (NpadCommand) GPOINTER_TO_INT(cmd)));
        GtkWidget *sub =
            GTK_IS_MENU_ITEM(item) ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) : NULL;
        if (sub)
            refresh_states(window, sub, all_on);
    }
    g_list_free(children);
}

static void on_list_menu_show(GtkWidget *menu, gpointer data) {
    (void) data;
    Window *window = g_gtk_main_window;
    if (!window)
        return;
    refresh_states(window, menu, false);
    if (g_case_item) { // The setting may have changed in another window
        window->syncing_menu = true;
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_case_item),
                                       settings_get_bool("list_sort_case_sensitive", false));
        window->syncing_menu = false;
    }
}

// An insensitive item's accelerator is dead, so the greying only lasts while
// the menu is open (like the Edit menu)
static void on_list_menu_hide(GtkWidget *menu, gpointer data) {
    (void) data;
    if (g_gtk_main_window)
        refresh_states(g_gtk_main_window, menu, true);
}

// Ctrl+] / Ctrl+[ for indent and unindent, when that binding is chosen
static void bind_bracket_accels(Window *window, GtkWidget *menu) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(l->data));
        NpadCommand cmd =
            (NpadCommand) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(l->data), "npad-cmd"));
        if (cmd == NPAD_CMD_LIST_UNINDENT)
            gtk_widget_add_accelerator(l->data, "activate", window->accel_group,
                                       GDK_KEY_bracketleft, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
        if (sub)
            bind_bracket_accels(window, sub);
        if (cmd == NPAD_CMD_LIST_INDENT)
            gtk_widget_add_accelerator(l->data, "activate", window->accel_group,
                                       GDK_KEY_bracketright, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    g_list_free(children);
}

// Insert, remove or rebuild the Markdown menu to match the settings. It is
// rebuilt only when something it shows changed: a settings broadcast from
// another window must not destroy a menu the user has open.
static void apply_menu(Window *window) {
    bool enabled = list_tools_enabled();
    int signature = enabled ? (1 | (brackets_shortcut() ? 2 : 0)) : 0;
    if (signature == g_menu_signature)
        return;
    g_menu_signature = signature;

    if (g_menu_item) {
        gtk_widget_destroy(g_menu_item);
        g_menu_item = NULL;
        g_case_item = NULL;
    }
    if (!enabled)
        return;

    GtkWidget *top = gtk_menu_item_new_with_mnemonic("_Markdown");
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), menu);
    populate_list_menu(window, menu, false);
    if (brackets_shortcut())
        bind_bracket_accels(window, menu);
    g_signal_connect(menu, "show", G_CALLBACK(on_list_menu_show), NULL);
    g_signal_connect(menu, "hide", G_CALLBACK(on_list_menu_hide), NULL);
    // After Format, before View (Win32 puts it at the same position)
    gtk_menu_shell_insert(GTK_MENU_SHELL(window->menubar), top, 3);
    gtk_widget_show_all(top);
    g_menu_item = top;
}

void npad_gtk_lists_reload_settings(Window *window) {
    if (window)
        apply_menu(window);
}

void npad_gtk_lists_init(Window *window) {
    g_signal_connect(window->text_view, "key-press-event", G_CALLBACK(on_key_press), window);
    apply_menu(window);
}
