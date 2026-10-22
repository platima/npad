/*
 * npad - Page Setup, Print and Print Preview (GTK 3)
 *
 * Mirrors print_win32.c: prints the document rather than the window, with the
 * Notepad header/footer codes, margins from Page Setup, and a layout measured
 * on the print context (never on screen) so the preview and the printout
 * break lines identically. See LINUX_PORT.md, "Printing".
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef PRINT_GTK_H
#define PRINT_GTK_H

#include <gtk/gtk.h>
#include <stdbool.h>

// What a print job needs from the window. All strings are borrowed; the job
// copies what it keeps.
typedef struct {
    const char *text;        // UTF-8 with '\n' line breaks, as ui_get_text returns it
    const char *title;       // The &f header code and the print job name
    const char *font_family; // The font the window is showing
    double font_size;        // Points; zoom deliberately does not apply to paper
    int font_weight;         // 100..900
    bool font_italic;
} NpadPrintDoc;

// File > Page Setup: paper, orientation and margins, persisted to settings
void npad_gtk_print_page_setup(GtkWindow *parent);

// File > Print (preview == false) and File > Print Preview (preview == true)
void npad_gtk_print_run(GtkWindow *parent, const NpadPrintDoc *doc, bool preview);

// npad --print: print to the default printer with no print dialog, showing
// only GTK's progress window. Returns the exit code: 0 printed, 2 cancelled,
// 1 failed (after telling the user why).
int npad_gtk_print_file(const NpadPrintDoc *doc);

// Release the session's print settings (shutdown)
void npad_gtk_print_cleanup(void);

#endif // PRINT_GTK_H
