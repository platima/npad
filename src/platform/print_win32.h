/*
 * npad - Printing (Win32)
 *
 * Page Setup and Print, following classic notepad.exe: the same header/footer
 * codes, the same defaults, and the same behaviour of printing exactly what
 * the document contains rather than what the window happens to show.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef PRINT_WIN32_H
#define PRINT_WIN32_H

#include <windows.h>
#include <stdbool.h>

// Page Setup dialog. Margins, orientation and paper are persisted to settings.
void print_show_page_setup(HWND owner);

// Print dialog, then render. text is UTF-16, doc_title names the job in the
// print queue and fills the &f header/footer code, and face/point_size are the
// font the window is currently showing, so the page matches the screen.
// Returns false only if something failed after the user committed - cancelling
// is not a failure.
bool print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title, const wchar_t *face,
                    int point_size);

#endif // PRINT_WIN32_H
