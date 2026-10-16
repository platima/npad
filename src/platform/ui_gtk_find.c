/*
 * npad - GTK3 UI: Find, Replace and Go To
 *
 * NOT YET PORTED. These entry points exist so the rest of the GTK layer links
 * and so the commands have one obvious home; the menu items stay greyed out
 * (see command_available in ui_gtk.c) until this file implements them.
 *
 * The Win32 behaviour to reproduce lives in ui_win32.c (find_next,
 * find_replace_proc, replace_all, refresh_highlights, show_goto_dialog) and
 * in DOCUMENTATION.md. Notes for the port:
 *  - Search in GTK character offsets with gtk_text_iter_forward_search /
 *    backward_search (GTK_TEXT_SEARCH_TEXT_ONLY, plus
 *    GTK_TEXT_SEARCH_CASE_INSENSITIVE when Match case is off). Whole word
 *    needs a word-boundary check around each hit.
 *  - "Interpret escapes" reuses list_unescape() from core/list_ops.
 *  - Replace All must be one undo step: bracket it with
 *    gtk_text_buffer_begin_user_action / end_user_action, which ui_gtk.c turns
 *    into an undo group.
 *  - Highlight All is a GtkTextTag over every match, not a painted overlay.
 *  - Find options persist under the same settings keys as Win32
 *    (find_match_case, find_whole_word, find_search_down, find_wrap_around,
 *    find_interpret_escapes, find_highlight_all) and must be re-read in
 *    npad_gtk_find_reload_settings, which a settings broadcast calls.
 *  - Go To counts logical lines (GtkTextIter lines are already logical, even
 *    with word wrap on - the RichEdit display-line trap in LESSONS.md does not
 *    apply here).
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "ui_gtk.h"

void ui_platform_show_find_dialog(Window *parent) {
    npad_gtk_set_status_message(parent, "Find is not available on Linux yet");
}

void ui_platform_show_replace_dialog(Window *parent) {
    npad_gtk_set_status_message(parent, "Replace is not available on Linux yet");
}

bool npad_gtk_find_has_term(void) {
    return false;
}

void npad_gtk_find_next(Window *window, bool down) {
    (void) down;
    ui_platform_show_find_dialog(window);
}

void npad_gtk_show_goto_dialog(Window *window) {
    npad_gtk_set_status_message(window, "Go To is not available on Linux yet");
}

void npad_gtk_find_reload_settings(void) {
}

void npad_gtk_find_cleanup(void) {
}
