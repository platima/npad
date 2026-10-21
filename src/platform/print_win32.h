/*
 * npad - Printing and Print Preview (Win32)
 *
 * Page Setup, Print Preview and Print, following classic notepad.exe: the same
 * header/footer codes, the same defaults, and the same behaviour of printing
 * exactly what the document contains rather than what the window happens to
 * show.
 *
 * The layout is computed ONCE, on the printer's information context, in
 * printer device units. Both the preview and the printer then replay the
 * stored coordinates verbatim. Measuring on a screen DC instead would round
 * every glyph advance to a ~96 dpi pixel, and the preview would wrap lines
 * where the printer does not. The wrapping itself is core/print_core.c's.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef PRINT_WIN32_H
#define PRINT_WIN32_H

#include <windows.h>
#include <stdbool.h>

// Page Setup dialog. Margins and orientation are persisted to settings. The
// margins are shown in the locale's unit (millimetres on a metric machine)
// and stored in thousandths of an inch either way.
void print_show_page_setup(HWND owner);

// The font the window is showing, which is what gets printed. Zoom is view
// state and deliberately does not apply to paper.
typedef struct {
    const wchar_t *face; // NULL or empty falls back to Consolas
    int point_size;      // Clamped to 6..72, as the editor clamps it
    int weight;          // FW_*; 0 or less means FW_NORMAL
    bool italic;
} PrintFont;

// A paginated document. Opaque: the coordinates inside are printer device
// units and are only meaningful to the two replay paths below.
typedef struct PrintLayout PrintLayout;

// Paginate text for the default printer. doc_title fills the &f code and
// names the print job. When no printer is installed this still succeeds
// against a synthetic page (the locale's paper size at 600 dpi), so Print
// Preview keeps working - print_layout_has_printer reports which happened.
// Returns NULL only on allocation failure.
PrintLayout *print_layout_create(const wchar_t *text, const wchar_t *doc_title,
                                 const PrintFont *font);
void print_layout_free(PrintLayout *layout);

int print_layout_page_count(const PrintLayout *layout);
bool print_layout_has_printer(const PrintLayout *layout);

// Sheet size and resolution, in printer device units / dots per inch. The
// preview needs these to size the paper on screen.
void print_layout_paper(const PrintLayout *layout, int *width, int *height, int *dpi_x, int *dpi_y);

// Map hdc so that one logical unit is one printer dot and logical (0,0) is the
// printer's own origin, with the whole sheet filling dest (client pixels).
// Returns a SaveDC cookie for print_layout_end_scaled, or 0 on failure.
int print_layout_begin_scaled(const PrintLayout *layout, HDC hdc, const RECT *dest);
void print_layout_end_scaled(HDC hdc, int saved);

// Draw one 1-based page. hdc must already be mapped: either by
// print_layout_begin_scaled (preview) or as a printer DC in MM_TEXT (print).
void print_layout_draw_page(const PrintLayout *layout, HDC hdc, int page);

// Every result is distinct, and PRINTED is 0: an `if (!print_document(...))`
// call site reads as "if it printed" - see LESSONS.md before testing one
// with `!`.
typedef enum {
    PRINT_RESULT_PRINTED,
    PRINT_RESULT_CANCELLED,  // The user dismissed a dialog or pressed Cancel
    PRINT_RESULT_FAILED,     // Something went wrong; worth telling the user
    PRINT_RESULT_NO_PRINTER, // print_document_default: no default printer
} PrintResult;

// Print dialog, then render. While the job spools a small window shows the
// page being sent and offers Cancel; owner is disabled meanwhile. Cancelling
// is distinct from failing so a caller can leave the user where they were.
PrintResult print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                           const PrintFont *font);

// Print to the default printer with no print dialog, as notepad.exe /p does.
// The progress window (with Cancel) is still shown, unowned.
PrintResult print_document_default(const wchar_t *text, const wchar_t *doc_title,
                                   const PrintFont *font);

// Whether Page Setup, the print dialog or a print job is under way in this
// process. Messages are pumped throughout all three, so callers use this to
// hold back anything that must not happen underneath one - closing the
// window, or a prompt owned by the window the dialog is disabling.
bool print_is_busy(void);

// Called each time print_is_busy() goes back to false, so whatever was held
// back can be posted. Runs inside the print call that is finishing: it should
// only post messages.
void print_set_idle_hook(void (*hook)(void));

// Called with each window the print module creates (the progress window), so
// the UI layer can give it the scheme's title bar and npad's icon.
void print_set_window_hook(void (*hook)(HWND hwnd));

#endif // PRINT_WIN32_H
