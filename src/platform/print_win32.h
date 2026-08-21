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
 * where the printer does not.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef PRINT_WIN32_H
#define PRINT_WIN32_H

#include <windows.h>
#include <stdbool.h>

// Page Setup dialog. Margins and orientation are persisted to settings.
void print_show_page_setup(HWND owner);

// A paginated document. Opaque: the coordinates inside are printer device
// units and are only meaningful to the two replay paths below.
typedef struct PrintLayout PrintLayout;

// Paginate text for the default printer. face/point_size are the font the
// window is showing, doc_title fills the &f code and names the print job.
// When no printer is installed this still succeeds against a synthetic page
// (the locale's paper size at 600 dpi), so Print Preview keeps working -
// print_layout_has_printer reports which happened. Returns NULL only on
// allocation failure.
PrintLayout *print_layout_create(const wchar_t *text, const wchar_t *doc_title, const wchar_t *face,
                                 int point_size);
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

typedef enum {
    PRINT_RESULT_PRINTED,
    PRINT_RESULT_CANCELLED, // The user dismissed the print dialog
    PRINT_RESULT_FAILED,
} PrintResult;

// Print dialog, then render. Cancelling is distinct from failing so a caller
// can leave the user where they were rather than treating it as done.
PrintResult print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                           const wchar_t *face, int point_size);

#endif // PRINT_WIN32_H
