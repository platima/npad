/*
 * npad - GTK3 UI: Preferences and Format > Font
 *
 * The Preferences dialog is a GtkNotebook with the same pages, wording, keys
 * and defaults as the Win32 property sheet (prefs_*_proc in ui_win32.c is the
 * reference): General, Appearance, Defaults, Markdown, Backup, and the hidden
 * Debug page. Windows-only pages are replaced rather than ported: there is no
 * Updates page (Linux updates through apt / dnf) and Associations becomes a
 * single "Make npad the default text editor" action.
 *
 * There is no Apply button. Every control saves the moment it changes, writes
 * only its own key, and tells the other instances; so a page that was merely
 * looked at can never write its whole state back (the property sheet's
 * PSN_APPLY lesson in LESSONS.md). Because the dialog's own controls are the
 * only thing that changed, the local window re-reads settings.json through
 * npad_gtk_reload_and_apply_settings, the same path a broadcast from another
 * instance takes.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "ui_gtk.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "../core/editor.h"
#include "../core/file_ops.h"
#include "../core/find_seed.h"
#include "../core/session.h"
#include "../core/settings.h"
#include "../core/startup_prof.h"
#include "../main.h"

// The desktop file `xdg-mime default` needs (must match the app id)
#define NPAD_DESKTOP_FILE NPAD_APP_ID ".desktop"

// Colour scheme setting values, indexed by the Appearance combo
static const char *const SCHEME_KEYS[] = { "system", "light", "dark", "solarized-light",
                                           "solarized-dark" };
static const char *const SCHEME_LABELS[] = { "Follow system", "Light", "Dark", "Solarized Light",
                                             "Solarized Dark" };
#define SCHEME_COUNT ((int) (sizeof(SCHEME_KEYS) / sizeof(SCHEME_KEYS[0])))

// Markdown default indent formats (list_default_indent_format indexes this)
static const char *const INDENT_FORMAT_LABELS[] = {
    "Spaces",
    "Tab",
    "Asterisk (\"* \")",
    "Hyphen (\"- \")",
    "Asterisk, leading space (\" * \")",
    "Hyphen, leading space (\" - \")",
    "Custom...",
};
#define INDENT_FORMAT_COUNT ((int) (sizeof(INDENT_FORMAT_LABELS) / sizeof(INDENT_FORMAT_LABELS[0])))
#define INDENT_FORMAT_CUSTOM 6

#define MAX_RECENT_PREF 10

enum {
    PAGE_GENERAL,
    PAGE_APPEARANCE,
    PAGE_DEFAULTS,
    PAGE_MARKDOWN,
    PAGE_BACKUP,
    PAGE_ASSOC,
    PAGE_DEBUG
};

// Everything the open dialog needs to refresh its controls. One instance: the
// dialog is modal and there is one document window per process.
static struct {
    GtkWidget *dialog;
    Window *owner;
    bool loading;    // Controls are being filled from settings: not a user change
    bool reset_done; // Settings were just reset: the controls are stale, write nothing

    GtkWidget *bools[16]; // Plain checkboxes bound to a bool key (see add_check)
    int bool_count;

    // General
    GtkWidget *autosave_check, *autosave_spin, *large_spin, *recent_spin;
    GtkWidget *session_check, *session_spin;
    // Appearance
    GtkWidget *scheme_combo, *dyslexic_check, *sync_check;
    // Defaults
    GtkWidget *enc_combo, *eol_combo, *fonttype_combo, *zoom_spin;
    GtkWidget *header_entry, *footer_entry;
    // Markdown
    GtkWidget *indent_combo, *custom_entry, *brackets_check;
    GtkWidget *paste_primary[3], *paste_alt[3];
    // Associations
    GtkWidget *assoc_status;
    // Debug
    GtkWidget *debug_view;
} P;

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

// A setting changed: persist it, tell the other instances and re-apply it
// here. Every handler calls this after writing its one key.
static void commit(void) {
    if (!settings_save())
        return; // Reloading would throw the unsaved change away
    ui_notify_settings_changed();
    npad_gtk_reload_and_apply_settings(P.owner);
}

static void show_message(GtkWindow *parent, GtkMessageType type, const char *text) {
    GtkWidget *dialog =
        gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type,
                               GTK_BUTTONS_OK, "%s", text);
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// ---------------------------------------------------------------------------
// Widget builders
// ---------------------------------------------------------------------------

static GtkWidget *new_page(GtkWidget *notebook, const char *title) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, gtk_label_new(title));
    return box;
}

static GtkWidget *add_note(GtkWidget *box, const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    return label;
}

// "Label: [control]" on one line
static GtkWidget *add_row(GtkWidget *box, const char *mnemonic, GtkWidget *control) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new_with_mnemonic(mnemonic);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), control);
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(row), control, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    return row;
}

static GtkWidget *add_button(GtkWidget *box, const char *mnemonic, GCallback handler) {
    GtkWidget *button = gtk_button_new_with_mnemonic(mnemonic);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    g_signal_connect(button, "clicked", handler, NULL);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    return button;
}

// The generic handlers find their key on the widget itself
static void bind_key(GtkWidget *widget, const char *key, int fallback) {
    g_object_set_data(G_OBJECT(widget), "npad-key", (gpointer) key);
    g_object_set_data(G_OBJECT(widget), "npad-default", GINT_TO_POINTER(fallback));
}

static const char *bound_key(GtkWidget *widget) {
    return g_object_get_data(G_OBJECT(widget), "npad-key");
}

static int bound_default(GtkWidget *widget) {
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "npad-default"));
}

static void sync_sensitivity(void);

static void on_bool_toggled(GtkToggleButton *button, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    settings_set_bool(bound_key(GTK_WIDGET(button)), gtk_toggle_button_get_active(button));
    commit();
    sync_sensitivity();
}

// A checkbox bound straight to a bool setting
static GtkWidget *add_check(GtkWidget *box, const char *mnemonic, const char *key, bool fallback) {
    GtkWidget *check = gtk_check_button_new_with_mnemonic(mnemonic);
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(check));
    if (GTK_IS_LABEL(label)) {
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    }
    bind_key(check, key, fallback ? 1 : 0);
    g_signal_connect(check, "toggled", G_CALLBACK(on_bool_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
    if (P.bool_count < (int) (sizeof(P.bools) / sizeof(P.bools[0])))
        P.bools[P.bool_count++] = check;
    return check;
}

static void on_int_changed(GtkSpinButton *spin, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    settings_set_int(bound_key(GTK_WIDGET(spin)), gtk_spin_button_get_value_as_int(spin));
    commit();
}

static GtkWidget *add_spin(GtkWidget *box, const char *mnemonic, const char *key, int fallback,
                           int min, int max) {
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    bind_key(spin, key, fallback);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_int_changed), NULL);
    add_row(box, mnemonic, spin);
    return spin;
}

static void on_int_combo_changed(GtkComboBox *combo, gpointer data) {
    (void) data;
    int index = gtk_combo_box_get_active(combo);
    if (P.loading || index < 0)
        return;
    settings_set_int(bound_key(GTK_WIDGET(combo)), index);
    commit();
    sync_sensitivity();
}

static GtkWidget *new_combo(GtkWidget *box, const char *mnemonic, const char *key, int fallback,
                            const char *const *labels, int count) {
    GtkWidget *combo = gtk_combo_box_text_new();
    for (int i = 0; i < count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), labels[i]);
    if (key) {
        bind_key(combo, key, fallback);
        g_signal_connect(combo, "changed", G_CALLBACK(on_int_combo_changed), NULL);
    }
    add_row(box, mnemonic, combo);
    return combo;
}

// Text entries commit on Enter and when focus leaves, not on every keystroke
// (a save and a broadcast per character would be silly); closing the dialog
// commits the one that still has focus.
static void commit_entry(GtkWidget *entry) {
    if (P.loading)
        return;
    const char *key = bound_key(entry);
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    // Compared with what the setting reads as, default included: an unset key
    // showing its default is not a change, and must not be written
    char *current =
        settings_get_string(key, g_object_get_data(G_OBJECT(entry), "npad-default-str"));
    bool changed = !current || strcmp(current, text) != 0;
    free(current);
    if (!changed)
        return;
    settings_set_string(key, text);
    commit();
}

static void on_entry_activate(GtkEntry *entry, gpointer data) {
    (void) data;
    commit_entry(GTK_WIDGET(entry));
}

static gboolean on_entry_focus_out(GtkWidget *entry, GdkEvent *event, gpointer data) {
    (void) event;
    (void) data;
    commit_entry(entry);
    return FALSE;
}

static GtkWidget *add_entry(GtkWidget *box, const char *mnemonic, const char *key,
                            const char *fallback, int max_len) {
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), max_len);
    bind_key(entry, key, 0);
    g_object_set_data(G_OBJECT(entry), "npad-default-str", (gpointer) fallback);
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), NULL);
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_entry_focus_out), NULL);
    add_row(box, mnemonic, entry);
    return entry;
}

// ---------------------------------------------------------------------------
// Loading the controls from settings
// ---------------------------------------------------------------------------

static void sync_sensitivity(void) {
    gtk_widget_set_sensitive(P.autosave_spin,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(P.autosave_check)));
    gtk_widget_set_sensitive(P.session_spin,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(P.session_check)));

    bool md_on = settings_get_bool("list_tools_enabled", false);
    gtk_widget_set_sensitive(P.custom_entry, gtk_combo_box_get_active(GTK_COMBO_BOX(
                                                 P.indent_combo)) == INDENT_FORMAT_CUSTOM);
    gtk_widget_set_sensitive(P.brackets_check, md_on);
    // The paste modes only take effect when Markdown support is on
    for (int i = 0; i < 3; i++) {
        gtk_widget_set_sensitive(P.paste_primary[i], md_on);
        gtk_widget_set_sensitive(P.paste_alt[i], md_on);
    }
}

static void set_entry(GtkWidget *entry, const char *key, const char *fallback) {
    char *text = settings_get_string(key, fallback);
    gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
    free(text);
}

static void load_widgets(void) {
    P.loading = true;

    for (int i = 0; i < P.bool_count; i++) {
        GtkWidget *check = P.bools[i];
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(check),
            settings_get_bool(bound_key(check), bound_default(check) != 0));
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.autosave_check),
                                 editor_is_auto_save_enabled());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(P.autosave_spin), editor_get_auto_save_interval());
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(P.large_spin),
        settings_get_int("large_file_warning_mb", editor_default_large_file_mb()));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(P.recent_spin),
                              settings_get_int("recent_files_max", MAX_RECENT_PREF));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.session_check),
                                 editor_is_session_resume_enabled());
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(P.session_spin),
                              settings_get_int("session_interval", 30));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.dyslexic_check),
                                 settings_get_bool("opendyslexic_enabled", false));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.sync_check),
                                 settings_get_bool("sync_view_state", false));

    char *theme = settings_get_string("theme", "light");
    int scheme = 1; // Light
    for (int i = 0; i < SCHEME_COUNT; i++) {
        if (theme && strcmp(theme, SCHEME_KEYS[i]) == 0)
            scheme = i;
    }
    free(theme);
    gtk_combo_box_set_active(GTK_COMBO_BOX(P.scheme_combo), scheme);

    int enc = settings_get_int("default_encoding", 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(P.enc_combo),
                             (enc >= 0 && enc <= (int) NPAD_ENC_ANSI) ? enc : 0);
    int eol = settings_get_int("default_line_ending", (int) NPAD_DEFAULT_LINE_ENDING);
    gtk_combo_box_set_active(GTK_COMBO_BOX(P.eol_combo), (eol >= 0 && eol <= (int) NPAD_EOL_CR)
                                                             ? eol
                                                             : (int) NPAD_DEFAULT_LINE_ENDING);
    // Falls back to the pre-0.7 monospace_enabled so the shown default equals
    // the effective one (see the Win32 page)
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(P.fonttype_combo),
        settings_get_bool("default_font_mono", settings_get_bool("monospace_enabled", true)) ? 0
                                                                                             : 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(P.zoom_spin), settings_get_int("default_zoom", 100));
    set_entry(P.header_entry, "print_header", "&f");
    set_entry(P.footer_entry, "print_footer", "Page &p");

    int indent = settings_get_int("list_default_indent_format", 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(P.indent_combo),
                             (indent >= 0 && indent < INDENT_FORMAT_COUNT) ? indent : 0);
    set_entry(P.custom_entry, "list_custom_indent", "");
    int primary = settings_get_int("markdown_paste_primary", 1);
    int alt = settings_get_int("markdown_paste_alt", 0);
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(P.paste_primary[(primary < 0 || primary > 2) ? 1 : primary]), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(P.paste_alt[(alt < 0 || alt > 2) ? 0 : alt]),
                                 TRUE);

    P.loading = false;
    sync_sensitivity();
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

// Show the font chooser and save the selection to font_key (face) plus the
// shared size / weight / italic keys, exactly what apply_view_css reads
static void choose_font(GtkWindow *parent, const char *font_key, const char *default_face) {
    char *face = settings_get_string(font_key, default_face);
    int size = settings_get_int("font_size", 11);
    int weight = settings_get_int("font_weight", 400);
    bool italic = settings_get_bool("font_italic", false);

    PangoFontDescription *initial = pango_font_description_new();
    pango_font_description_set_family(initial, face ? face : default_face);
    pango_font_description_set_size(initial, size * PANGO_SCALE);
    pango_font_description_set_weight(initial, (PangoWeight) weight);
    pango_font_description_set_style(initial, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    free(face);

    GtkWidget *dialog = gtk_font_chooser_dialog_new("Font", parent);
    gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(dialog), initial);
    pango_font_description_free(initial);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        PangoFontDescription *chosen = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
        if (chosen) {
            // A family list ("A, B") cannot be matched as one CSS name
            char *family = g_strdup(pango_font_description_get_family(chosen));
            if (family) {
                char *comma = strchr(family, ',');
                if (comma)
                    *comma = '\0';
                g_strstrip(family);
                if (family[0])
                    settings_set_string(font_key, family);
                g_free(family);
            }
            int points = (pango_font_description_get_size(chosen) + PANGO_SCALE / 2) / PANGO_SCALE;
            settings_set_int("font_size", points < 6 ? 6 : (points > 72 ? 72 : points));
            int new_weight = (int) pango_font_description_get_weight(chosen);
            settings_set_int("font_weight",
                             new_weight < 100 ? 100 : (new_weight > 900 ? 900 : new_weight));
            PangoStyle style = pango_font_description_get_style(chosen);
            settings_set_bool("font_italic",
                              style == PANGO_STYLE_ITALIC || style == PANGO_STYLE_OBLIQUE);
            pango_font_description_free(chosen);

            P.owner = g_gtk_main_window;
            commit();
        }
    }
    gtk_widget_destroy(dialog);
}

void npad_gtk_show_font_dialog(Window *window) {
    if (!window)
        return;
    P.owner = window;
    const char *default_face = NPAD_GTK_DEFAULT_MONO_FONT;
    const char *key = npad_gtk_active_font_key(window, &default_face);
    choose_font(GTK_WINDOW(window->window), key, default_face);
}

static void on_mono_font_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    choose_font(GTK_WINDOW(P.dialog), "monospace_font", NPAD_GTK_DEFAULT_MONO_FONT);
}

static void on_prop_font_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    choose_font(GTK_WINDOW(P.dialog), "proportional_font", NPAD_GTK_DEFAULT_PROP_FONT);
}

// ---------------------------------------------------------------------------
// General page
// ---------------------------------------------------------------------------

static void on_autosave_toggled(GtkToggleButton *button, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    // The editor owns the timer as well as the key
    editor_enable_auto_save(gtk_toggle_button_get_active(button));
    commit();
    sync_sensitivity();
}

static void on_session_toggled(GtkToggleButton *button, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    // Turning it off also drops the current snapshot
    editor_enable_session_resume(gtk_toggle_button_get_active(button));
    commit();
    sync_sensitivity();
}

static void on_clear_recent_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    settings_clear_recent_files();
    commit(); // The File > Recent Files menu rebuilds every time File opens
}

static void build_general(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "General");

    P.autosave_check = gtk_check_button_new_with_mnemonic("Enable _auto-save");
    g_signal_connect(P.autosave_check, "toggled", G_CALLBACK(on_autosave_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(page), P.autosave_check, FALSE, FALSE, 0);
    P.autosave_spin =
        add_spin(page, "Auto-save _interval (seconds):", "auto_save_interval", 300, 10, 86400);

    P.large_spin = add_spin(page, "_Large file warning threshold (MB):", "large_file_warning_mb",
                            100, 0, 1048576);
    P.recent_spin = add_spin(page, "Ma_x recent files (0-10):", "recent_files_max", MAX_RECENT_PREF,
                             0, MAX_RECENT_PREF);
    add_button(page, "_Clear Recent Files Now", G_CALLBACK(on_clear_recent_clicked));

    P.session_check = gtk_check_button_new_with_mnemonic("Enable _session resume / crash recovery");
    g_signal_connect(P.session_check, "toggled", G_CALLBACK(on_session_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(page), P.session_check, FALSE, FALSE, 0);
    P.session_spin =
        add_spin(page, "Snapshot i_nterval (seconds):", "session_interval", 30, 5, 86400);

    add_check(page, "Ctrl+N opens a new win_dow (Ctrl+Shift+N clears the current one)",
              "ctrl_n_new_window", false);
    add_check(page, "Tell me when an open file is changed by another program", "watch_file_changes",
              false);
    add_check(page, "_Fill Find with the selected text", FIND_SEED_KEY, true);
}

// ---------------------------------------------------------------------------
// Appearance page
// ---------------------------------------------------------------------------

static void on_scheme_changed(GtkComboBox *combo, gpointer data) {
    (void) data;
    int index = gtk_combo_box_get_active(combo);
    if (P.loading || index < 0 || index >= SCHEME_COUNT)
        return;
    settings_set_string("theme", SCHEME_KEYS[index]);
    commit();
}

static void on_dyslexic_toggled(GtkToggleButton *button, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    bool on = gtk_toggle_button_get_active(button);
    if (on && !npad_gtk_font_is_installed("OpenDyslexic")) {
        // Cannot enable a font that is not installed: say so and put the
        // checkbox back
        show_message(GTK_WINDOW(P.dialog), GTK_MESSAGE_INFO,
                     "The OpenDyslexic font is not installed.\n"
                     "Download it from https://opendyslexic.org and install it, "
                     "then enable this option.");
        P.loading = true;
        gtk_toggle_button_set_active(button, FALSE);
        P.loading = false;
        return;
    }
    settings_set_bool("opendyslexic_enabled", on);
    commit();
}

static void on_sync_toggled(GtkToggleButton *button, gpointer data) {
    (void) data;
    if (P.loading)
        return;
    bool on = gtk_toggle_button_get_active(button);
    settings_set_bool("sync_view_state", on);
    commit();
    // Enabling sync brings the other windows into line with this one right
    // away, not only on the next zoom or font change. They turn sync on from
    // the settings broadcast above, then adopt the view state sent here.
    if (on && P.owner)
        npad_gtk_instance_broadcast_view_state(P.owner->zoom, P.owner->monospace_current);
}

static void build_appearance(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Appearance");

    P.scheme_combo = gtk_combo_box_text_new();
    for (int i = 0; i < SCHEME_COUNT; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(P.scheme_combo), SCHEME_LABELS[i]);
    g_signal_connect(P.scheme_combo, "changed", G_CALLBACK(on_scheme_changed), NULL);
    add_row(page, "Color _scheme:", P.scheme_combo);

    GtkWidget *fonts = gtk_frame_new("Fonts");
    GtkWidget *fbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(fbox), 8);
    gtk_container_add(GTK_CONTAINER(fonts), fbox);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *mono = gtk_button_new_with_mnemonic("_Monospace Font...");
    GtkWidget *prop = gtk_button_new_with_mnemonic("_Proportional Font...");
    g_signal_connect(mono, "clicked", G_CALLBACK(on_mono_font_clicked), NULL);
    g_signal_connect(prop, "clicked", G_CALLBACK(on_prop_font_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(buttons), mono, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), prop, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fbox), buttons, FALSE, FALSE, 0);
    P.dyslexic_check =
        gtk_check_button_new_with_mnemonic("Enable Open_Dyslexic font (reading assistance)");
    g_signal_connect(P.dyslexic_check, "toggled", G_CALLBACK(on_dyslexic_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(fbox), P.dyslexic_check, FALSE, FALSE, 0);
    add_note(fbox, "Toggle monospace with Ctrl+M or the status bar.");
    gtk_box_pack_start(GTK_BOX(page), fonts, FALSE, FALSE, 0);

    add_check(page, "Show s_tatus bar", "status_bar_visible", true);
    P.sync_check =
        gtk_check_button_new_with_mnemonic("S_ync view (font type and zoom) across all instances");
    g_signal_connect(P.sync_check, "toggled", G_CALLBACK(on_sync_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(page), P.sync_check, FALSE, FALSE, 0);
    add_check(page, "Show _word / character / line counts in the status bar", "status_show_counts",
              false);
}

// ---------------------------------------------------------------------------
// Defaults page
// ---------------------------------------------------------------------------

static void on_fonttype_changed(GtkComboBox *combo, gpointer data) {
    (void) data;
    int index = gtk_combo_box_get_active(combo);
    if (P.loading || index < 0)
        return;
    settings_set_bool("default_font_mono", index == 0);
    commit();
}

static void on_use_current_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    // Copy this window's font type and zoom into the fields (which saves them)
    if (!P.owner)
        return;
    gtk_combo_box_set_active(GTK_COMBO_BOX(P.fonttype_combo), P.owner->monospace_current ? 0 : 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(P.zoom_spin), P.owner->zoom);
}

static void build_defaults(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Defaults");

    char *enc_labels[NPAD_ENC_ANSI + 1];
    for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++)
        enc_labels[i] = g_strdup(file_encoding_name((TextEncoding) i));
    P.enc_combo = new_combo(page, "Default _encoding for new files:", "default_encoding", 0,
                            (const char *const *) enc_labels, (int) NPAD_ENC_ANSI + 1);
    char *eol_labels[NPAD_EOL_CR + 1];
    for (int i = 0; i <= (int) NPAD_EOL_CR; i++)
        eol_labels[i] = g_strdup(file_line_ending_name((LineEnding) i));
    P.eol_combo = new_combo(page, "Default _line endings for new files:", "default_line_ending",
                            (int) NPAD_DEFAULT_LINE_ENDING, (const char *const *) eol_labels,
                            (int) NPAD_EOL_CR + 1);
    for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++)
        g_free(enc_labels[i]);
    for (int i = 0; i <= (int) NPAD_EOL_CR; i++)
        g_free(eol_labels[i]);

    static const char *const font_types[] = { "Monospace", "Proportional" };
    P.fonttype_combo = new_combo(page, "Default _font type:", NULL, 0, font_types, 2);
    g_signal_connect(P.fonttype_combo, "changed", G_CALLBACK(on_fonttype_changed), NULL);
    P.zoom_spin = add_spin(page, "Default _zoom (10-500%):", "default_zoom", 100, 10, 500);

    GtkWidget *current = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *use = gtk_button_new_with_mnemonic("_Use Current");
    g_signal_connect(use, "clicked", G_CALLBACK(on_use_current_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(current), use, FALSE, FALSE, 0);
    add_note(current, "Copies this window's font type and zoom into the fields above.");
    gtk_box_pack_start(GTK_BOX(page), current, FALSE, FALSE, 0);

    add_check(page, "_Auto-update defaults in real time (view changes become the new defaults)",
              "auto_update_defaults", false);
    add_check(page, "Find and _Replace wrap around by default", "find_wrap_around", false);

    // Header/footer codes match notepad.exe; see DOCUMENTATION.md
    P.header_entry = add_entry(page, "Print _header:", "print_header", "&f", 255);
    P.footer_entry = add_entry(page, "Print f_ooter:", "print_footer", "Page &p", 255);
}

// ---------------------------------------------------------------------------
// Markdown page
// ---------------------------------------------------------------------------

static void on_paste_toggled(GtkToggleButton *button, gpointer data) {
    // The radio being switched off also signals; only the new choice counts
    if (P.loading || !gtk_toggle_button_get_active(button))
        return;
    settings_set_int(bound_key(GTK_WIDGET(button)), GPOINTER_TO_INT(data));
    commit();
}

// "Ctrl+V pastes: (o) Plain ( ) Lists ( ) Markdown"
static void add_paste_row(GtkWidget *box, const char *label_text, const char *key,
                          GtkWidget *radios[3]) {
    static const char *const names[3] = { "Plain", "Lists", "Markdown" };
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
    GtkWidget *first = NULL;
    for (int i = 0; i < 3; i++) {
        radios[i] =
            first ? gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first), names[i])
                  : gtk_radio_button_new_with_label(NULL, names[i]);
        if (!first)
            first = radios[i];
        bind_key(radios[i], key, 0);
        g_signal_connect(radios[i], "toggled", G_CALLBACK(on_paste_toggled), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(row), radios[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
}

static void build_markdown(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Markdown");

    add_check(page, "Basic _Markdown support", "list_tools_enabled", false);
    add_note(page, "Adds a Markdown menu and right-click tools (off by default for classic Notepad "
                   "compatibility):\n"
                   "-  Lists: formatting, continuation, sorting, indentation, unique filtering\n"
                   "-  Escape interpretation (\\n \\t \\uXXXX) in Find / Replace and list tools\n"
                   "-  Delimiter replacement (Convert Delimiters)\n"
                   "-  Pasting: convert rich text to lists or markdown (see Paste below)");

    P.indent_combo = new_combo(page, "Default _indent format:", "list_default_indent_format", 0,
                               INDENT_FORMAT_LABELS, INDENT_FORMAT_COUNT);
    P.custom_entry = add_entry(page, "Custom _prefix:", "list_custom_indent", "", 127);
    add_note(page, "Escapes: \\t tab  \\\\ backslash  \\uXXXX");
    add_note(page,
             "Markers include a trailing space (e.g. \"- \"). Indenting an already-marked line "
             "adds two spaces (markdown-style nesting); unindent removes one level.");
    P.brackets_check = add_check(page, "Use Ctrl+] / Ctrl+[ for indent instead of _Tab / Shift+Tab",
                                 "list_indent_shortcut_brackets", false);

    GtkWidget *paste = gtk_frame_new("Paste (rich text, when enabled)");
    GtkWidget *pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(pbox), 8);
    gtk_container_add(GTK_CONTAINER(paste), pbox);
    add_paste_row(pbox, "Ctrl+V pastes:", "markdown_paste_primary", P.paste_primary);
    add_paste_row(pbox, "Ctrl+Shift+V:", "markdown_paste_alt", P.paste_alt);
    gtk_box_pack_start(GTK_BOX(page), paste, FALSE, FALSE, 0);
}

// ---------------------------------------------------------------------------
// Backup page: export, import, reset
// ---------------------------------------------------------------------------

static void add_json_filters(GtkFileChooser *chooser) {
    GtkFileFilter *json = gtk_file_filter_new();
    gtk_file_filter_set_name(json, "JSON Files (*.json)");
    gtk_file_filter_add_pattern(json, "*.json");
    gtk_file_chooser_add_filter(chooser, json);
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All Files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(chooser, all);
}

// Copy the current settings.json to a user-chosen file
static void on_export_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    settings_save(); // Flush current in-memory settings to disk first
    const char *src = settings_get_file_path();
    if (!src)
        return;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Export Settings", GTK_WINDOW(P.dialog), GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(native);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "npad-settings.json");
    add_json_filters(chooser);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        gchar *dest = gtk_file_chooser_get_filename(chooser);
        bool ok = dest && file_copy(src, dest);
        g_free(dest);
        show_message(GTK_WINDOW(P.dialog), ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
                     ok ? "Settings exported." : "Failed to export settings.");
    }
    g_object_unref(native);
}

// Does the file look like a settings file (a JSON object)? Importing anything
// else would silently reset every preference.
static bool looks_like_settings(const char *path) {
    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL))
        return false;
    const char *p = contents;
    if (g_str_has_prefix(p, "\xEF\xBB\xBF"))
        p += 3;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    bool ok = *p == '{' && g_utf8_validate(contents, (gssize) length, NULL);
    g_free(contents);
    return ok;
}

// Replace the current settings with a user-chosen file and reload them
static void on_import_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    const char *dest = settings_get_file_path();
    if (!dest)
        return;

    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        "Import Settings", GTK_WINDOW(P.dialog), GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    add_json_filters(GTK_FILE_CHOOSER(native));
    gchar *src = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT)
        src = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
    g_object_unref(native);
    if (!src)
        return;

    bool ok = looks_like_settings(src) && file_copy(src, dest);
    g_free(src);
    if (!ok) {
        show_message(GTK_WINDOW(P.dialog), GTK_MESSAGE_WARNING, "Failed to import settings.");
        return;
    }

    // Re-read and re-apply what can change live, tell the other instances,
    // then show the imported values
    npad_gtk_reload_and_apply_settings(P.owner);
    ui_notify_settings_changed();
    load_widgets();
    show_message(GTK_WINDOW(P.dialog), GTK_MESSAGE_INFO,
                 "Settings imported. A few options (window size, default encoding) "
                 "take effect on restart.");
}

// Reset every preference to its default. Rather than an allowlist of keys to
// remove (which silently drifts every time a setting is added), this resets
// EVERYTHING except a small, stable set of preserved categories: recent files,
// window geometry, and Find/Replace state (options + history). Any new
// preference key is therefore reset by default. This is the list
// reset_all_preferences keeps on Win32.
static void on_reset_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    static const char *const keep_prefixes[] = {
        "recent_file_", // recent files list (recent_files_max is a pref -> reset)
        "window_",      // window geometry
        "find_",        // find options + find history
        "replace_",     // replace history
    };

    GtkWidget *ask = gtk_message_dialog_new(
        GTK_WINDOW(P.dialog), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s",
        "Reset all preferences to their defaults?\n\n"
        "Recent files, window position and Find/Replace options and history are kept.\n"
        "The Preferences window will close.");
    gtk_window_set_title(GTK_WINDOW(ask), "npad");
    gtk_dialog_set_default_response(GTK_DIALOG(ask), GTK_RESPONSE_NO);
    gint answer = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);
    if (answer != GTK_RESPONSE_YES)
        return;

    settings_reset_except_prefixes(keep_prefixes,
                                   (int) (sizeof(keep_prefixes) / sizeof(keep_prefixes[0])));
    // The one find_-prefixed key that IS a preference (Preferences > General),
    // not search state: the prefix keep above would otherwise preserve it
    settings_remove_key(FIND_SEED_KEY);
    commit();

    // The controls still show pre-reset values; close rather than leave stale
    // state on screen (and do not let closing write those values back)
    P.reset_done = true;
    gtk_dialog_response(GTK_DIALOG(P.dialog), GTK_RESPONSE_CLOSE);
}

static void build_backup(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Backup");

    GtkWidget *backup = gtk_frame_new("Settings backup");
    GtkWidget *bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(bbox), 8);
    gtk_container_add(GTK_CONTAINER(backup), bbox);
    GtkWidget *export = gtk_button_new_with_mnemonic("E_xport Settings...");
    GtkWidget *import = gtk_button_new_with_mnemonic("_Import Settings...");
    g_signal_connect(export, "clicked", G_CALLBACK(on_export_clicked), NULL);
    g_signal_connect(import, "clicked", G_CALLBACK(on_import_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bbox), export, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bbox), import, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), backup, FALSE, FALSE, 0);
    add_note(page,
             "Exports or replaces the settings.json file. Import applies immediately to all open "
             "windows.");

    GtkWidget *reset = gtk_frame_new("Reset");
    GtkWidget *rbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(rbox), 8);
    gtk_container_add(GTK_CONTAINER(reset), rbox);
    add_button(rbox, "_Reset All Preferences...", G_CALLBACK(on_reset_clicked));
    gtk_box_pack_start(GTK_BOX(page), reset, FALSE, FALSE, 0);
    add_note(page, "Restores every preference to its default. Recent files, window position and "
                   "Find/Replace options and history are kept.");
}

// ---------------------------------------------------------------------------
// Associations page: the Linux stand-in is one explicit action
// ---------------------------------------------------------------------------

// g_spawn_sync's status is a wait status on Unix. (g_spawn_check_exit_status is
// deprecated and its replacement needs GLib 2.70, newer than some targets.)
static bool exited_cleanly(gint status) {
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// The desktop file id currently handling text/plain, or NULL (g_free)
static char *query_default_text_editor(void) {
    char *xdg = g_find_program_in_path("xdg-mime");
    if (!xdg)
        return NULL;
    char *argv[] = { xdg, "query", "default", "text/plain", NULL };
    char *out = NULL;
    gint status = 0;
    bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL,
                           &status, NULL) &&
              exited_cleanly(status);
    g_free(xdg);
    if (ok && out)
        g_strstrip(out);
    if (!ok || !out || !out[0]) {
        g_free(out);
        return NULL;
    }
    return out;
}

// Show who handles text/plain now; asked = the user just clicked the button
static void refresh_assoc_status(bool asked, bool accepted) {
    char *current = query_default_text_editor();
    const char *lead = "";
    if (asked && !accepted)
        lead = "Could not change the default text editor.\n";
    else if (asked && current && strcmp(current, NPAD_DESKTOP_FILE) == 0)
        lead = "npad is now the default text editor.\n";
    else if (asked)
        lead = "Requested. Your desktop may need a moment, or a new login, to notice.\n";
    char *text = g_strdup_printf("%sCurrent default for plain text: %s", lead,
                                 current ? current : "unknown");
    gtk_label_set_text(GTK_LABEL(P.assoc_status), text);
    g_free(text);
    g_free(current);
}

static void on_make_default_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    char *xdg = g_find_program_in_path("xdg-mime");
    if (!xdg) {
        gtk_label_set_text(
            GTK_LABEL(P.assoc_status),
            "xdg-mime was not found, so npad cannot change the default. "
            "Install xdg-utils, or use your desktop's Default Applications settings.");
        return;
    }
    char *argv[] = { xdg, "default", NPAD_DESKTOP_FILE, "text/plain", NULL };
    gint status = 0;
    GError *error = NULL;
    bool ok =
        g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                     NULL, NULL, NULL, NULL, &status, &error) &&
        exited_cleanly(status);
    g_free(xdg);
    g_clear_error(&error);
    refresh_assoc_status(true, ok);
}

static void build_associations(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Associations");

    add_note(page, "npad's launcher already offers it for text files in \"Open With\". It is never "
                   "made the default unless you ask: this runs\n"
                   "xdg-mime default " NPAD_DESKTOP_FILE " text/plain\n"
                   "for your account. Most file managers use that for other text files too.");
    add_button(page, "Make npad the default text _editor", G_CALLBACK(on_make_default_clicked));
    P.assoc_status = add_note(page, "");
    refresh_assoc_status(false, false);
}

// ---------------------------------------------------------------------------
// Debug page (hidden: Ctrl+Shift+. or Shift+click on Preferences)
// ---------------------------------------------------------------------------

static char *build_diagnostics_text(void) {
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "npad %s\n", NPAD_VERSION);

    char *exe = g_file_read_link("/proc/self/exe", NULL);
    g_string_append_printf(out, "Executable: %s\n", exe ? exe : "?");
    g_free(exe);

    g_string_append_printf(out, "GTK: %u.%u.%u (built against %d.%d.%d)\n", gtk_get_major_version(),
                           gtk_get_minor_version(), gtk_get_micro_version(), GTK_MAJOR_VERSION,
                           GTK_MINOR_VERSION, GTK_MICRO_VERSION);
    GdkDisplay *display = gdk_display_get_default();
    g_string_append_printf(out, "Display backend: %s (%s)\n",
                           display ? G_OBJECT_TYPE_NAME(display) : "none",
                           display ? gdk_display_get_name(display) : "?");
    const char *session = g_getenv("XDG_SESSION_TYPE");
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    g_string_append_printf(out, "Session: %s, desktop %s\n", session ? session : "?",
                           desktop ? desktop : "?");
    gchar *theme = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, NULL);
    g_string_append_printf(out, "GTK theme: %s\n", theme ? theme : "?");
    g_free(theme);
    char *colours = npad_gtk_theme_diagnostics();
    g_string_append(out, colours);
    g_free(colours);
    const char *locale = setlocale(LC_ALL, NULL);
    g_string_append_printf(out, "Locale: %s\n", locale ? locale : "?");

    const char *settings_path = settings_get_file_path();
    if (settings_path)
        g_string_append_printf(out, "Settings: %s (%d entries)\n", settings_path, settings_count());

    char *recovery_dir = editor_session_dir();
    if (recovery_dir) {
        int slot_count = 0;
        char **slots = session_list_slots(recovery_dir, &slot_count);
        session_free_slots(slots, slot_count);
        g_string_append_printf(out, "Recovery: %s (%d slots)\n", recovery_dir, slot_count);
        free(recovery_dir);
    }

    g_string_append_printf(out, "Open npad windows: %d\n", npad_gtk_instance_count_others() + 1);

    g_string_append(out, "\nStartup profile (from the first mark):\n");
    for (int i = 0; i < startup_prof_count(); i++) {
        double at = startup_prof_ms(i);
        double delta = (i > 0) ? at - startup_prof_ms(i - 1) : at;
        const char *phase = startup_prof_name(i);
        g_string_append_printf(out, "  %8.1f ms  (+%6.1f)  %s%s\n", at, delta, phase ? phase : "?",
                               (phase && strcmp(phase, "deferred tasks") == 0)
                                   ? "   <- fires on a 50 ms timer; not startup work"
                                   : "");
    }
    return g_string_free(out, FALSE);
}

static void on_copy_diagnostics_clicked(GtkButton *button, gpointer data) {
    (void) button;
    (void) data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(P.debug_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    g_free(text);
}

static void build_debug(GtkWidget *notebook) {
    GtkWidget *page = new_page(notebook, "Debug");

    P.debug_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(P.debug_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(P.debug_view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(P.debug_view), FALSE);
    char *text = build_diagnostics_text();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(P.debug_view)), text, -1);
    g_free(text);

    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroller, -1, 280);
    gtk_container_add(GTK_CONTAINER(scroller), P.debug_view);
    gtk_box_pack_start(GTK_BOX(page), scroller, TRUE, TRUE, 0);
    add_button(page, "_Copy Diagnostics", G_CALLBACK(on_copy_diagnostics_clicked));
}

// ---------------------------------------------------------------------------
// The dialog
// ---------------------------------------------------------------------------

// Fields still holding a half-typed value commit as the dialog closes: a spin
// button only reports a number when it is read, and an entry when it loses
// focus or Enter is pressed
static void commit_pending(void) {
    GtkWidget *spins[] = { P.autosave_spin, P.large_spin, P.recent_spin, P.session_spin,
                           P.zoom_spin };
    for (size_t i = 0; i < sizeof(spins) / sizeof(spins[0]); i++)
        gtk_spin_button_update(GTK_SPIN_BUTTON(spins[i]));
    commit_entry(P.header_entry);
    commit_entry(P.footer_entry);
    commit_entry(P.custom_entry);
}

void npad_gtk_show_preferences(Window *window, bool show_debug) {
    if (!window)
        return;
    if (P.dialog) {
        gtk_window_present(GTK_WINDOW(P.dialog));
        return;
    }
    memset(&P, 0, sizeof(P));
    P.owner = window;

    P.dialog = gtk_dialog_new_with_buttons("Preferences", GTK_WINDOW(window->window),
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(P.dialog), GTK_RESPONSE_CLOSE);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 6);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(P.dialog))), notebook, TRUE,
                       TRUE, 0);

    // Built while loading is set so no control writes a setting on the way up
    P.loading = true;
    build_general(notebook);
    build_appearance(notebook);
    build_defaults(notebook);
    build_markdown(notebook);
    build_backup(notebook);
    build_associations(notebook);
    if (show_debug)
        build_debug(notebook);
    P.loading = false;

    load_widgets();

    gtk_widget_show_all(P.dialog);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), show_debug ? PAGE_DEBUG : PAGE_GENERAL);

    gtk_dialog_run(GTK_DIALOG(P.dialog));
    if (!P.reset_done)
        commit_pending();
    gtk_widget_destroy(P.dialog);
    memset(&P, 0, sizeof(P));
}
