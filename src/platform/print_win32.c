/*
 * npad - Printing (Win32)
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "print_win32.h"
#include "../core/settings.h"
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// notepad.exe's defaults, in thousandths of an inch. Stored in that unit
// rather than the locale-dependent one PAGESETUPDLG uses, so a settings file
// means the same thing on a metric and an imperial machine.
#define MARGIN_DEFAULT_SIDE 750
#define MARGIN_DEFAULT_TOPBOTTOM 1000

// Same codes notepad.exe uses, so muscle memory and pasted strings both work:
//   &f file   &p page   &d date   &t time   &l left   &c centre   &r right
#define HEADER_DEFAULT L"&f"
#define FOOTER_DEFAULT L"Page &p"

typedef struct {
    int left, right, top, bottom; // Thousandths of an inch
    short orientation;            // DMORIENT_PORTRAIT / _LANDSCAPE, 0 = printer default
} PageSetup;

static void page_setup_load(PageSetup *ps) {
    ps->left = settings_get_int("print_margin_left", MARGIN_DEFAULT_SIDE);
    ps->right = settings_get_int("print_margin_right", MARGIN_DEFAULT_SIDE);
    ps->top = settings_get_int("print_margin_top", MARGIN_DEFAULT_TOPBOTTOM);
    ps->bottom = settings_get_int("print_margin_bottom", MARGIN_DEFAULT_TOPBOTTOM);
    ps->orientation = (short) settings_get_int("print_orientation", 0);
}

static void page_setup_save(const PageSetup *ps) {
    settings_set_int("print_margin_left", ps->left);
    settings_set_int("print_margin_right", ps->right);
    settings_set_int("print_margin_top", ps->top);
    settings_set_int("print_margin_bottom", ps->bottom);
    settings_set_int("print_orientation", ps->orientation);
    settings_save();
}

// Header/footer strings are stored as UTF-8 in settings; caller frees.
static wchar_t *load_hf(const char *key, const wchar_t *fallback) {
    char *utf8 = settings_get_string(key, NULL);
    if (!utf8) {
        size_t n = wcslen(fallback) + 1;
        wchar_t *copy = malloc(n * sizeof(wchar_t));
        if (copy)
            wcscpy(copy, fallback);
        return copy;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *wide = (len > 0) ? malloc((size_t) len * sizeof(wchar_t)) : NULL;
    if (wide)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    free(utf8);
    return wide;
}

void print_show_page_setup(HWND owner) {
    PageSetup ps;
    page_setup_load(&ps);

    PAGESETUPDLGW psd;
    ZeroMemory(&psd, sizeof(psd));
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = owner;
    // Work in thousandths of an inch throughout, so the value stored in
    // settings does not depend on the machine's measurement system
    psd.Flags = PSD_INTHOUSANDTHSOFINCHES | PSD_MARGINS;
    psd.rtMargin.left = ps.left;
    psd.rtMargin.right = ps.right;
    psd.rtMargin.top = ps.top;
    psd.rtMargin.bottom = ps.bottom;

    if (ps.orientation != 0) {
        psd.hDevMode = GlobalAlloc(GHND, sizeof(DEVMODEW));
        if (psd.hDevMode) {
            DEVMODEW *dm = GlobalLock(psd.hDevMode);
            if (dm) {
                dm->dmSize = sizeof(DEVMODEW);
                dm->dmFields = DM_ORIENTATION;
                dm->dmOrientation = ps.orientation;
                GlobalUnlock(psd.hDevMode);
            }
        }
    }

    if (PageSetupDlgW(&psd)) {
        ps.left = psd.rtMargin.left;
        ps.right = psd.rtMargin.right;
        ps.top = psd.rtMargin.top;
        ps.bottom = psd.rtMargin.bottom;
        if (psd.hDevMode) {
            const DEVMODEW *dm = GlobalLock(psd.hDevMode);
            if (dm && (dm->dmFields & DM_ORIENTATION)) {
                ps.orientation = dm->dmOrientation;
            }
            if (dm)
                GlobalUnlock(psd.hDevMode);
        }
        page_setup_save(&ps);
    }

    if (psd.hDevMode)
        GlobalFree(psd.hDevMode);
    if (psd.hDevNames)
        GlobalFree(psd.hDevNames);
}

// Expand notepad's header/footer codes into out. Sections are split on &l/&c/&r
// by the caller; this handles only the value substitutions.
static void expand_codes(const wchar_t *src, wchar_t *out, size_t out_cap, int page,
                         const wchar_t *title) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    size_t o = 0;

    for (const wchar_t *p = src; *p && o + 1 < out_cap; p++) {
        if (*p != L'&') {
            out[o++] = *p;
            continue;
        }
        p++;
        if (!*p)
            break;
        wchar_t buf[128];
        buf[0] = L'\0';
        switch (*p) {
            case L'f':
            case L'F':
                _snwprintf(buf, 127, L"%s", title ? title : L"Untitled");
                break;
            case L'p':
            case L'P':
                _snwprintf(buf, 127, L"%d", page);
                break;
            case L'd':
            case L'D':
                GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buf, 127);
                break;
            case L't':
            case L'T':
                GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, buf, 127);
                break;
            case L'&':
                wcscpy(buf, L"&"); // "&&" prints a literal ampersand
                break;
            default:
                break; // &l/&c/&r are section markers, handled by the caller
        }
        buf[127] = L'\0';
        for (const wchar_t *b = buf; *b && o + 1 < out_cap; b++) {
            out[o++] = *b;
        }
    }
    out[o] = L'\0';
}

// Draw one header or footer line, honouring the &l / &c / &r section markers.
// Text before any marker is left-aligned, matching notepad.exe.
static void draw_hf(HDC hdc, const wchar_t *spec, const RECT *area, int page, const wchar_t *title,
                    bool bottom) {
    if (!spec || !*spec)
        return;

    wchar_t sections[3][256] = { { 0 }, { 0 }, { 0 } };
    int current = 0; // 0 left, 1 centre, 2 right
    size_t n[3] = { 0, 0, 0 };

    for (const wchar_t *p = spec; *p; p++) {
        if (*p == L'&' && p[1]) {
            wchar_t c = p[1];
            if (c == L'l' || c == L'L') {
                current = 0;
                p++;
                continue;
            }
            if (c == L'c' || c == L'C') {
                current = 1;
                p++;
                continue;
            }
            if (c == L'r' || c == L'R') {
                current = 2;
                p++;
                continue;
            }
            if (n[current] + 2 < 255) {
                sections[current][n[current]++] = *p;
                sections[current][n[current]++] = c;
            }
            p++;
            continue;
        }
        if (n[current] + 1 < 255)
            sections[current][n[current]++] = *p;
    }
    for (int i = 0; i < 3; i++)
        sections[i][n[i]] = L'\0';

    const UINT align[3] = { DT_LEFT, DT_CENTER, DT_RIGHT };
    for (int i = 0; i < 3; i++) {
        if (!sections[i][0])
            continue;
        wchar_t text[512];
        expand_codes(sections[i], text, 512, page, title);
        RECT r = *area;
        DrawTextW(hdc, text, -1, &r,
                  align[i] | DT_SINGLELINE | DT_NOPREFIX | (bottom ? DT_BOTTOM : DT_TOP));
    }
}

bool print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                    const wchar_t *face_in, int point_size) {
    if (!text)
        return false;

    PageSetup ps;
    page_setup_load(&ps);

    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    pd.nCopies = 1;

    if (!PrintDlgW(&pd))
        return true; // Cancelled: not a failure

    HDC hdc = pd.hDC;
    if (!hdc) {
        if (pd.hDevMode)
            GlobalFree(pd.hDevMode);
        if (pd.hDevNames)
            GlobalFree(pd.hDevNames);
        return false;
    }

    // Apply the orientation chosen in Page Setup, unless the print dialog's
    // own settings already specify one
    if (ps.orientation != 0 && pd.hDevMode) {
        DEVMODEW *dm = GlobalLock(pd.hDevMode);
        if (dm) {
            dm->dmFields |= DM_ORIENTATION;
            dm->dmOrientation = ps.orientation;
            ResetDCW(hdc, dm);
            GlobalUnlock(pd.hDevMode);
        }
    }

    int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
    int page_w = GetDeviceCaps(hdc, HORZRES);
    int page_h = GetDeviceCaps(hdc, VERTRES);

    // Printable area, in device units, after the configured margins. The
    // printer's own unprintable border is already excluded from HORZRES/VERTRES.
    RECT body;
    body.left = MulDiv(ps.left, dpi_x, 1000);
    body.right = page_w - MulDiv(ps.right, dpi_x, 1000);
    body.top = MulDiv(ps.top, dpi_y, 1000);
    body.bottom = page_h - MulDiv(ps.bottom, dpi_y, 1000);
    if (body.right <= body.left || body.bottom <= body.top) {
        // Margins larger than the paper - fall back to the whole page rather
        // than printing nothing at all
        body.left = body.top = 0;
        body.right = page_w;
        body.bottom = page_h;
    }

    // Print in the editor's current font at the printer's resolution, so the
    // output matches what is on screen rather than a default face
    wchar_t face[LF_FACESIZE];
    face[0] = L'\0';
    if (face_in && face_in[0]) {
        wcsncpy(face, face_in, LF_FACESIZE - 1);
        face[LF_FACESIZE - 1] = L'\0';
    }
    int pt = point_size;
    if (pt < 6)
        pt = 6;
    if (pt > 72)
        pt = 72;

    HFONT font = CreateFontW(-MulDiv(pt, dpi_y, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, face[0] ? face : L"Consolas");
    HFONT old_font = font ? (HFONT) SelectObject(hdc, font) : NULL;

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    int line_h = tm.tmHeight + tm.tmExternalLeading;
    if (line_h <= 0)
        line_h = MulDiv(pt, dpi_y, 72);

    // One line of header and one of footer, each with a blank line of padding
    int hf_h = line_h * 2;
    wchar_t *header = load_hf("print_header", HEADER_DEFAULT);
    wchar_t *footer = load_hf("print_footer", FOOTER_DEFAULT);
    RECT text_area = body;
    if (header && header[0])
        text_area.top += hf_h;
    if (footer && footer[0])
        text_area.bottom -= hf_h;

    int lines_per_page = (text_area.bottom - text_area.top) / line_h;
    if (lines_per_page < 1)
        lines_per_page = 1;
    int wrap_w = text_area.right - text_area.left;

    DOCINFOW di;
    ZeroMemory(&di, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = doc_title ? doc_title : L"npad";

    bool ok = (StartDocW(hdc, &di) > 0);
    int page = 1;
    int line_on_page = 0;

    if (ok && StartPage(hdc) <= 0)
        ok = false;
    if (ok && header && header[0]) {
        RECT hr = body;
        hr.bottom = body.top + line_h;
        draw_hf(hdc, header, &hr, page, di.lpszDocName, false);
    }

    const wchar_t *p = text;
    while (ok && *p) {
        // One logical line, up to the break
        const wchar_t *eol = p;
        while (*eol && *eol != L'\r' && *eol != L'\n')
            eol++;
        int remaining = (int) (eol - p);
        const wchar_t *seg = p;

        do {
            int fit = remaining;
            if (remaining > 0) {
                SIZE sz;
                // How many characters fit the printable width? This is the one
                // API that answers that directly - DrawText cannot report it.
                if (!GetTextExtentExPointW(hdc, seg, remaining, wrap_w, &fit, NULL, &sz))
                    fit = remaining;
                if (fit < 1)
                    fit = 1;
                if (fit < remaining) {
                    // Break at a space rather than mid-word, when there is one
                    int brk = fit;
                    while (brk > 0 && seg[brk] != L' ' && seg[brk] != L'\t')
                        brk--;
                    if (brk > 0)
                        fit = brk + 1;
                }
            }

            if (line_on_page >= lines_per_page) {
                if (footer && footer[0]) {
                    RECT fr = body;
                    fr.top = body.bottom - line_h;
                    draw_hf(hdc, footer, &fr, page, di.lpszDocName, true);
                }
                if (EndPage(hdc) <= 0) {
                    ok = false;
                    break;
                }
                page++;
                line_on_page = 0;
                if (StartPage(hdc) <= 0) {
                    ok = false;
                    break;
                }
                if (header && header[0]) {
                    RECT hr = body;
                    hr.bottom = body.top + line_h;
                    draw_hf(hdc, header, &hr, page, di.lpszDocName, false);
                }
            }

            if (fit > 0) {
                TextOutW(hdc, text_area.left, text_area.top + line_on_page * line_h, seg, fit);
            }
            line_on_page++;
            seg += fit;
            remaining -= fit;
        } while (ok && remaining > 0);

        // Step past the break, treating CRLF as one
        p = eol;
        if (*p == L'\r')
            p++;
        if (*p == L'\n')
            p++;
    }

    if (ok) {
        if (footer && footer[0]) {
            RECT fr = body;
            fr.top = body.bottom - line_h;
            draw_hf(hdc, footer, &fr, page, di.lpszDocName, true);
        }
        if (EndPage(hdc) <= 0)
            ok = false;
    }

    if (ok) {
        EndDoc(hdc);
    } else {
        AbortDoc(hdc);
    }

    free(header);
    free(footer);
    if (old_font)
        SelectObject(hdc, old_font);
    if (font)
        DeleteObject(font);
    DeleteDC(hdc);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);
    return ok;
}
