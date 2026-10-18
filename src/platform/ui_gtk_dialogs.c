/*
 * npad - GTK3 UI: file choosers, message boxes, prompts and About
 *
 * File choosers are GtkFileChooserNative, so a desktop that routes them
 * through xdg-desktop-portal gets its own dialog, and the GTK dialog appears
 * everywhere else. The Save As encoding picker is a file chooser "choice",
 * which portals support too.
 *
 * Author: Platima
 * https://npad.dev
 */

#define _DEFAULT_SOURCE // sysconf(_SC_PHYS_PAGES)

#include "ui_gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../core/file_ops.h"
#include "../main.h"

static GtkWindow *parent_window(Window *parent) {
    if (parent && parent->window)
        return GTK_WINDOW(parent->window);
    if (g_gtk_main_window && g_gtk_main_window->window)
        return GTK_WINDOW(g_gtk_main_window->window);
    return NULL;
}

// Notepad's "Text Documents (*.txt)" filter, translated: on Linux most text
// files have no extension, so it matches by type. text/plain is the parent
// of every text format in shared-mime-info, so source code, config files and
// logs all qualify while images and archives do not.
static void add_filters(GtkFileChooser *chooser) {
    GtkFileFilter *text = gtk_file_filter_new();
    gtk_file_filter_set_name(text, "Text Documents");
    gtk_file_filter_add_mime_type(text, "text/plain");
    gtk_file_filter_add_pattern(text, "*.txt");
    gtk_file_chooser_add_filter(chooser, text);

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All Files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(chooser, all);
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        (params && params->title) ? params->title : "Open", parent_window(parent),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    add_filters(GTK_FILE_CHOOSER(native));

    char *result = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        if (path) {
            result = strdup(path);
            g_free(path);
        }
    }
    g_object_unref(native);
    return result;
}

char *ui_platform_show_save_dialog(Window *parent, FileDialogParams *params) {
    GtkFileChooserNative *native = gtk_file_chooser_native_new(
        (params && params->title) ? params->title : "Save As", parent_window(parent),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(native);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    add_filters(chooser);

    // Pre-fill the current file's folder and name, as the Win32 dialog does,
    // so Save As of an open document starts from that document
    const char *initial = (params && params->default_filename && params->default_filename[0])
                              ? params->default_filename
                              : "Untitled.txt";
    if (strchr(initial, '/')) {
        gchar *folder = g_path_get_dirname(initial);
        gchar *base = g_path_get_basename(initial);
        gtk_file_chooser_set_current_folder(chooser, folder);
        gtk_file_chooser_set_current_name(chooser, base);
        g_free(folder);
        g_free(base);
    } else {
        gtk_file_chooser_set_current_name(chooser, initial);
    }

    // Encoding picker. Choice ids are the TextEncoding values as strings.
    const char *ids[NPAD_ENC_ANSI + 2];
    const char *labels[NPAD_ENC_ANSI + 2];
    char id_text[NPAD_ENC_ANSI + 1][4];
    for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++) {
        snprintf(id_text[i], sizeof(id_text[i]), "%d", i);
        ids[i] = id_text[i];
        labels[i] = file_encoding_name((TextEncoding) i);
    }
    ids[NPAD_ENC_ANSI + 1] = NULL;
    labels[NPAD_ENC_ANSI + 1] = NULL;
    gtk_file_chooser_add_choice(chooser, "encoding", "Encoding:", ids, labels);
    int initial_encoding = params ? (int) params->encoding : (int) NPAD_ENC_UTF8;
    if (initial_encoding < 0 || initial_encoding > (int) NPAD_ENC_ANSI)
        initial_encoding = (int) NPAD_ENC_UTF8;
    gtk_file_chooser_set_choice(chooser, "encoding", id_text[initial_encoding]);

    char *result = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
        const char *choice = gtk_file_chooser_get_choice(chooser, "encoding");
        if (params && choice) {
            int chosen = atoi(choice);
            if (chosen >= 0 && chosen <= (int) NPAD_ENC_ANSI)
                params->encoding = (TextEncoding) chosen;
        }
        gchar *path = gtk_file_chooser_get_filename(chooser);
        if (path) {
            result = strdup(path);
            g_free(path);
        }
    }
    g_object_unref(native);
    return result;
}

bool ui_platform_show_message_box(Window *parent, const char *title, const char *message,
                                  bool is_question) {
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        is_question ? GTK_MESSAGE_QUESTION : GTK_MESSAGE_INFO,
        is_question ? GTK_BUTTONS_YES_NO : GTK_BUTTONS_OK, "%s", message ? message : "");
    gtk_window_set_title(GTK_WINDOW(dialog), title ? title : "npad");
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return is_question ? (response == GTK_RESPONSE_YES) : true;
}

// Response ids for the custom buttons (positive, so they never collide with
// the negative GTK_RESPONSE_* values)
enum { RESPONSE_FIRST = 1, RESPONSE_SECOND = 2 };

SavePromptResult ui_platform_show_save_prompt(Window *parent, const char *filename) {
    // Matches notepad.exe: no icon, the question as the text, and Save /
    // Don't Save / Cancel with Save as the default
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_OTHER,
        GTK_BUTTONS_NONE, "Do you want to save changes to %s?", filename ? filename : "Untitled");
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Save", RESPONSE_FIRST, "Do_n't Save",
                           RESPONSE_SECOND, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), RESPONSE_FIRST);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    // Esc, the close box and any failure cancel: aborting the close is the
    // safe direction, never silently discarding the buffer
    switch (response) {
        case RESPONSE_FIRST:
            return UI_SAVE_PROMPT_SAVE;
        case RESPONSE_SECOND:
            return UI_SAVE_PROMPT_DISCARD;
        default:
            return UI_SAVE_PROMPT_CANCEL;
    }
}

UiOpenChoice ui_platform_prompt_binary_open(Window *parent, const char *filename) {
    char *name = file_get_filename(filename);
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "This file looks like a binary file");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "\"%s\" does not look like a text file.\n\n"
        "Opening it in npad may show unreadable characters, and saving it from npad "
        "could corrupt it.",
        name ? name : (filename ? filename : ""));
    free(name);
    gtk_window_set_title(GTK_WINDOW(dialog), "npad");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Open in npad", RESPONSE_FIRST,
                           "Open with the _default app", RESPONSE_SECOND, "_Cancel",
                           GTK_RESPONSE_CANCEL, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == RESPONSE_FIRST)
        return UI_OPEN_IN_NPAD;
    if (response == RESPONSE_SECOND)
        return UI_OPEN_WITH_DEFAULT;
    return UI_OPEN_CANCEL;
}

void ui_platform_open_with_default_app(const char *filename) {
    if (!filename)
        return;
    GFile *file = g_file_new_for_path(filename);
    gchar *uri = g_file_get_uri(file);
    GError *error = NULL;
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
        char message[512];
        snprintf(message, sizeof(message), "Could not open %.300s:\n%.150s", filename,
                 error ? error->message : "no application is registered for it");
        ui_platform_show_message_box(g_gtk_main_window, "npad", message, false);
        g_clear_error(&error);
    }
    g_free(uri);
    g_object_unref(file);
}

size_t ui_platform_system_memory_mb(void) {
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0)
        return 0;
    return (size_t) ((unsigned long long) pages * (unsigned long long) page_size /
                     (1024ULL * 1024ULL));
}

static void on_about_response(GtkDialog *dialog, gint response, gpointer data) {
    (void) response;
    (void) data;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

// A fresh dialog every time rather than gtk_show_about_dialog(), which caches
// the first one on its parent and re-presents it unchanged: after a colour
// scheme change it would keep showing the logo of the old scheme
// The same text as the Windows About box. No "authors" list: GTK turns that
// into a Credits button, which Windows has no counterpart for.
void ui_platform_show_about_dialog(Window *parent) {
    GtkWindow *owner = parent_window(parent);
    GtkWidget *dialog = gtk_about_dialog_new();
    g_object_set(dialog, "program-name", "npad", "version", NPAD_VERSION, "comments",
                 "A lightweight, cross-platform text editor\n"
                 "inspired by classic Windows Notepad.\n\n"
                 "Author: Platima",
                 "website", "https://npad.dev", "website-label", "npad.dev", "logo-icon-name",
                 npad_gtk_app_icon_name(), "title", "About npad", NULL);
    if (owner) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), owner);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    }
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(on_about_response), NULL);
    gtk_widget_show(dialog);
}
