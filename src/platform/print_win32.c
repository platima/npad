/*
 * npad - Printing and Print Preview (Win32)
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
#include <wchar.h>

// notepad.exe's defaults, in thousandths of an inch. Stored in that unit
// rather than the locale-dependent one PAGESETUPDLG uses, so a settings file
// means the same thing on a metric and an imperial machine.
#define MARGIN_DEFAULT_SIDE 750
#define MARGIN_DEFAULT_TOPBOTTOM 1000

// Same codes notepad.exe uses, so muscle memory and pasted strings both work:
//   &f file   &p page   &d date   &t time   &l left   &c centre   &r right
#define HEADER_DEFAULT L"&f"
#define FOOTER_DEFAULT L"Page &p"

// Tab stops every 8 characters, as notepad prints them
#define TAB_COLUMNS 8

// Longest run handed to GetTextExtentExPointW at once. The call is linear in
// the length passed, so a single multi-megabyte line without a break would
// otherwise be measured repeatedly; whatever fits is emitted and the remainder
// continues on the same output line.
#define MEASURE_CHUNK 4096

typedef struct {
    int left, right, top, bottom; // Thousandths of an inch, from the sheet edge
    short orientation;            // DMORIENT_PORTRAIT / _LANDSCAPE, 0 = printer default
} PageSetup;

// One drawn segment of text: a wrapped line, or the piece between two tab
// stops. Coordinates are printer device units relative to the printer's own
// origin (the top-left of the printable area, not of the sheet).
typedef struct {
    int page; // 1-based
    int x, y;
    int offset; // Index into PrintLayout::text
    int len;
} PrintRun;

struct PrintLayout {
    wchar_t *text;  // Owned copy - the preview outlives the caller's buffer
    wchar_t *title; // Owned; fills &f and names the print job
    wchar_t *header;
    wchar_t *footer;
    HFONT font;

    // Device geometry, in printer device units
    int dpi_x, dpi_y;
    int phys_w, phys_h; // The whole sheet
    int off_x, off_y;   // Hardware margin: sheet corner to printable corner
    int horz_res, vert_res;

    RECT body;      // Page area inside the user's margins
    RECT text_area; // body, less the header and footer bands
    int line_h;
    int lines_per_page;
    int wrap_w;
    int tab_w;

    PrintRun *runs;
    int run_count;
    int run_cap;
    int page_count;
    bool has_printer;
};

// The printer to measure against, resolved without showing any dialog
typedef struct {
    wchar_t *driver;
    wchar_t *device;
    DEVMODEW *devmode;
    bool present;
} PrintDevice;

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

static wchar_t *wide_dup(const wchar_t *s) {
    if (!s)
        s = L"";
    size_t n = wcslen(s) + 1;
    wchar_t *copy = malloc(n * sizeof(wchar_t));
    if (copy)
        memcpy(copy, s, n * sizeof(wchar_t));
    return copy;
}

// Header/footer strings are stored as UTF-8 in settings; caller frees
static wchar_t *load_hf(const char *key, const wchar_t *fallback) {
    char *utf8 = settings_get_string(key, NULL);
    if (!utf8)
        return wide_dup(fallback);
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

// ---------------------------------------------------------------------------
// Header and footer
// ---------------------------------------------------------------------------

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
// Text before any marker is left-aligned, matching notepad.exe. DT_SINGLELINE
// means DrawTextW only aligns here and never re-wraps, so it behaves the same
// on the preview's scaled DC as on the printer.
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

// ---------------------------------------------------------------------------
// Resolving the default printer without a dialog
// ---------------------------------------------------------------------------

static void device_free(PrintDevice *dev) {
    free(dev->driver);
    free(dev->device);
    free(dev->devmode);
    dev->driver = NULL;
    dev->device = NULL;
    dev->devmode = NULL;
    dev->present = false;
}

// PD_RETURNDEFAULT fills in the default printer's DEVNAMES and DEVMODE without
// showing anything. comdlg32 is already delay-loaded for the two print
// dialogs, so this costs no new import - winspool.drv (GetDefaultPrinterW,
// OpenPrinterW) is deliberately not linked.
static bool device_acquire_default(PrintDevice *dev) {
    ZeroMemory(dev, sizeof(*dev));

    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.Flags = PD_RETURNDEFAULT; // hDevMode/hDevNames must be NULL on entry
    if (!PrintDlgW(&pd))
        return false; // No default printer, or no spooler

    const DEVNAMES *dn = pd.hDevNames ? GlobalLock(pd.hDevNames) : NULL;
    if (dn) {
        // DEVNAMES offsets are in characters from the start of the structure
        const wchar_t *base = (const wchar_t *) dn;
        dev->driver = wide_dup(base + dn->wDriverOffset);
        dev->device = wide_dup(base + dn->wDeviceOffset);
        GlobalUnlock(pd.hDevNames);
    }

    const DEVMODEW *dm = pd.hDevMode ? GlobalLock(pd.hDevMode) : NULL;
    if (dm) {
        // The driver-private tail past dmSize carries the real settings;
        // copying only sizeof(DEVMODEW) yields a structure that looks valid
        // and quietly prints with default paper and orientation.
        size_t bytes = (size_t) dm->dmSize + dm->dmDriverExtra;
        dev->devmode = malloc(bytes);
        if (dev->devmode)
            memcpy(dev->devmode, dm, bytes);
        GlobalUnlock(pd.hDevMode);
    }

    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);

    dev->present = (dev->device != NULL && dev->device[0] != L'\0');
    if (!dev->present)
        device_free(dev);
    return dev->present;
}

// Force the orientation chosen in Page Setup into the DEVMODE. A field is
// honoured only when its bit is set in dmFields.
static void device_apply_orientation(PrintDevice *dev, short orientation) {
    if (!dev->devmode || orientation == 0)
        return;
    dev->devmode->dmFields |= DM_ORIENTATION;
    dev->devmode->dmOrientation = orientation;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static bool layout_add_run(PrintLayout *l, int page, int x, int y, int offset, int len) {
    if (len <= 0)
        return true;
    if (l->run_count == l->run_cap) {
        int cap = l->run_cap ? l->run_cap * 2 : 256;
        PrintRun *grown = realloc(l->runs, (size_t) cap * sizeof(PrintRun));
        if (!grown)
            return false;
        l->runs = grown;
        l->run_cap = cap;
    }
    PrintRun *r = &l->runs[l->run_count++];
    r->page = page;
    r->x = x;
    r->y = y;
    r->offset = offset;
    r->len = len;
    return true;
}

// Read the device geometry, defending against drivers that report nothing
// useful on an information context.
static void layout_read_metrics(PrintLayout *l, HDC dc) {
    l->dpi_x = GetDeviceCaps(dc, LOGPIXELSX);
    l->dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
    l->horz_res = GetDeviceCaps(dc, HORZRES);
    l->vert_res = GetDeviceCaps(dc, VERTRES);
    l->phys_w = GetDeviceCaps(dc, PHYSICALWIDTH);
    l->phys_h = GetDeviceCaps(dc, PHYSICALHEIGHT);
    l->off_x = GetDeviceCaps(dc, PHYSICALOFFSETX);
    l->off_y = GetDeviceCaps(dc, PHYSICALOFFSETY);

    if (l->dpi_x <= 0)
        l->dpi_x = 300;
    if (l->dpi_y <= 0)
        l->dpi_y = l->dpi_x;
    if (l->horz_res <= 0)
        l->horz_res = l->dpi_x * 8;
    if (l->vert_res <= 0)
        l->vert_res = l->dpi_y * 10;
    // Some drivers report no sheet size at all on an information context.
    // Assume the printable area IS the sheet rather than inventing a margin.
    if (l->off_x < 0)
        l->off_x = 0;
    if (l->off_y < 0)
        l->off_y = 0;
    if (l->phys_w < l->horz_res + l->off_x) {
        l->phys_w = l->horz_res;
        l->off_x = 0;
    }
    if (l->phys_h < l->vert_res + l->off_y) {
        l->phys_h = l->vert_res;
        l->off_y = 0;
    }
}

// The printable box for the user's margins. Margins are measured from the
// SHEET edge, as notepad's are, then expressed in printer coordinates (whose
// origin is the printable corner) and clipped to what the hardware can reach.
static void layout_compute_body(PrintLayout *l, const PageSetup *ps) {
    int left = MulDiv(ps->left, l->dpi_x, 1000) - l->off_x;
    int top = MulDiv(ps->top, l->dpi_y, 1000) - l->off_y;
    int right = l->phys_w - MulDiv(ps->right, l->dpi_x, 1000) - l->off_x;
    int bottom = l->phys_h - MulDiv(ps->bottom, l->dpi_y, 1000) - l->off_y;

    l->body.left = left > 0 ? left : 0;
    l->body.top = top > 0 ? top : 0;
    l->body.right = right < l->horz_res ? right : l->horz_res;
    l->body.bottom = bottom < l->vert_res ? bottom : l->vert_res;

    if (l->body.right <= l->body.left || l->body.bottom <= l->body.top) {
        // Margins larger than the paper - fall back to the whole printable
        // area rather than printing nothing at all
        l->body.left = 0;
        l->body.top = 0;
        l->body.right = l->horz_res;
        l->body.bottom = l->vert_res;
    }
}

// Where the next output line goes, and how the page fills up
typedef struct {
    int page;
    int line_on_page;
} Cursor;

static int cursor_y(const PrintLayout *l, const Cursor *c) {
    return l->text_area.top + c->line_on_page * l->line_h;
}

static void cursor_break(const PrintLayout *l, Cursor *c) {
    c->line_on_page++;
    if (c->line_on_page >= l->lines_per_page) {
        c->page++;
        c->line_on_page = 0;
    }
}

// Paginate the whole document against dc, which must be the printer's DC or
// information context with the print font already selected.
static bool layout_paginate(PrintLayout *l, HDC dc) {
    const wchar_t *text = l->text;
    size_t total = wcslen(text);
    Cursor c = { 1, 0 };
    INT *dx = NULL;
    int dx_cap = 0;
    bool ok = true;

    size_t pos = 0;
    for (;;) {
        size_t eol = pos;
        while (eol < total && text[eol] != L'\r' && text[eol] != L'\n')
            eol++;

        size_t i = pos;
        int x = 0;
        while (i < eol && ok) {
            if (text[i] == L'\t') {
                // Tabs advance to the next stop rather than drawing a glyph
                int next = ((x / l->tab_w) + 1) * l->tab_w;
                if (next < l->wrap_w) {
                    x = next;
                } else if (x > 0) {
                    cursor_break(l, &c); // The stop is past the column: wrap
                    x = 0;
                }
                // Already at the margin with no stop that fits: stay put, or
                // every tab would burn a blank line of its own
                i++;
                continue;
            }

            size_t seg_end = i;
            while (seg_end < eol && text[seg_end] != L'\t')
                seg_end++;
            int seg_len = (int) (seg_end - i);
            int chunk = seg_len > MEASURE_CHUNK ? MEASURE_CHUNK : seg_len;

            int avail = l->wrap_w - x;
            if (avail <= 0) {
                cursor_break(l, &c);
                x = 0;
                avail = l->wrap_w;
            }

            if (chunk > dx_cap) {
                INT *grown = realloc(dx, (size_t) chunk * sizeof(INT));
                if (!grown) {
                    ok = false;
                    break;
                }
                dx = grown;
                dx_cap = chunk;
            }

            int fit = chunk;
            SIZE sz;
            // The only API that reports how many characters fit a width.
            // Note the returned SIZE covers the whole string, not the prefix -
            // the fitted width is the last cumulative entry in dx.
            bool measured = (GetTextExtentExPointW(dc, text + i, chunk, avail, &fit, dx, &sz) != 0);
            if (!measured)
                fit = chunk; // dx is then undefined; the width is re-measured below
            bool wrapped = (fit < chunk);
            if (fit < 1) {
                if (x > 0) {
                    // Nothing fits in what is left of the line: start a new one
                    // and measure again from the margin
                    cursor_break(l, &c);
                    x = 0;
                    continue;
                }
                fit = 1; // A single character wider than the column: emit it
                wrapped = true;
            }
            if (wrapped) {
                // Break at a space rather than mid-word, when there is one
                int brk = fit;
                while (brk > 0 && text[i + brk] != L' ' && text[i + brk] != L'\t')
                    brk--;
                if (brk > 0)
                    fit = brk + 1;
            }

            if (!layout_add_run(l, c.page, l->text_area.left + x, cursor_y(l, &c), (int) i, fit)) {
                ok = false;
                break;
            }
            i += (size_t) fit;

            if (wrapped) {
                // The rest of this segment goes on a new line, so the pen
                // position is about to be reset - do not bother measuring it.
                // (dx is only guaranteed filled for the characters that FIT,
                // and the space back-scan above can push fit one past that.)
                if (i < eol) {
                    cursor_break(l, &c);
                    x = 0;
                }
                // If nothing is left, the end-of-logical-line break below
                // covers it. Breaking here as well would insert a blank line.
            } else if (measured && fit <= dx_cap) {
                x += dx[fit - 1];
            } else {
                SIZE run_size = { 0, 0 };
                if (GetTextExtentPoint32W(dc, text + i - fit, fit, &run_size))
                    x += (int) run_size.cx;
                else
                    x = l->wrap_w; // Unmeasurable: force a break rather than overlap
            }
        }

        if (!ok)
            break;
        cursor_break(l, &c); // The logical line ends here

        if (eol >= total)
            break;
        pos = eol;
        if (text[pos] == L'\r')
            pos++;
        if (pos < total && text[pos] == L'\n')
            pos++;
        if (pos >= total)
            break; // A trailing break leaves one empty line and nothing after
    }

    free(dx);

    // Pages past the last drawn line are blank, so they are not pages
    l->page_count = 1;
    for (int r = 0; r < l->run_count; r++) {
        if (l->runs[r].page > l->page_count)
            l->page_count = l->runs[r].page;
    }
    return ok;
}

// Select the print font on dc and derive every metric pagination needs.
// read_metrics is false only for the synthetic no-printer page, whose geometry
// is already filled in and must not be replaced by the measuring DC's.
static bool layout_prepare(PrintLayout *l, HDC dc, const wchar_t *face, int point_size,
                           bool read_metrics) {
    PageSetup ps;
    page_setup_load(&ps);

    if (read_metrics)
        layout_read_metrics(l, dc);
    layout_compute_body(l, &ps);

    wchar_t face_buf[LF_FACESIZE];
    face_buf[0] = L'\0';
    if (face && face[0]) {
        wcsncpy(face_buf, face, LF_FACESIZE - 1);
        face_buf[LF_FACESIZE - 1] = L'\0';
    }
    int pt = point_size;
    if (pt < 6)
        pt = 6;
    if (pt > 72)
        pt = 72;

    // OUT_TT_ONLY_PRECIS stops a driver substituting a printer-resident font
    // with different metrics, which would make the preview and the printed
    // page disagree about where lines wrap.
    l->font =
        CreateFontW(-MulDiv(pt, l->dpi_y, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, face_buf[0] ? face_buf : L"Consolas");
    if (!l->font)
        return false;

    HFONT old = (HFONT) SelectObject(dc, l->font);

    TEXTMETRICW tm;
    ZeroMemory(&tm, sizeof(tm)); // A failed GetTextMetricsW would leave garbage
    if (!GetTextMetricsW(dc, &tm))
        ZeroMemory(&tm, sizeof(tm));
    l->line_h = tm.tmHeight + tm.tmExternalLeading;
    if (l->line_h <= 0)
        l->line_h = MulDiv(pt, l->dpi_y, 72);
    if (l->line_h <= 0)
        l->line_h = 1; // Everything below divides by this

    SIZE space;
    l->tab_w = 0;
    if (GetTextExtentPoint32W(dc, L" ", 1, &space))
        l->tab_w = (int) space.cx * TAB_COLUMNS;
    if (l->tab_w <= 0)
        l->tab_w = tm.tmAveCharWidth * TAB_COLUMNS;
    if (l->tab_w <= 0)
        l->tab_w = l->line_h;

    // One line of header and one of footer, each with a blank line of padding
    int hf_h = l->line_h * 2;
    l->text_area = l->body;
    if (l->header && l->header[0])
        l->text_area.top += hf_h;
    if (l->footer && l->footer[0])
        l->text_area.bottom -= hf_h;

    l->lines_per_page = (l->text_area.bottom - l->text_area.top) / l->line_h;
    if (l->lines_per_page < 1)
        l->lines_per_page = 1;
    l->wrap_w = l->text_area.right - l->text_area.left;
    if (l->wrap_w < 1)
        l->wrap_w = 1;

    bool ok = layout_paginate(l, dc);

    if (old)
        SelectObject(dc, old);
    return ok;
}

// Geometry for a machine with no printer at all: Microsoft Print to PDF can be
// removed by policy, and Server Core has no spooler. Preview still works.
static void layout_synthesise_paper(PrintLayout *l) {
    // LOCALE_IPAPERSIZE uses the DMPAPER_* numbering: 1 Letter, 5 Legal, 9 A4
    wchar_t buf[16] = { 0 };
    long paper = 1; // DMPAPER_LETTER
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IPAPERSIZE, buf, 16) > 0)
        paper = wcstol(buf, NULL, 10);

    const int dpi = 600;
    l->dpi_x = dpi;
    l->dpi_y = dpi;
    if (paper == 9) { // DMPAPER_A4, 210 x 297 mm
        l->phys_w = MulDiv(2100, dpi, 254);
        l->phys_h = MulDiv(2970, dpi, 254);
    } else { // Letter, 8.5 x 11 in
        l->phys_w = MulDiv(85, dpi, 10);
        l->phys_h = 11 * dpi;
    }
    if (settings_get_int("print_orientation", 0) == DMORIENT_LANDSCAPE) {
        int swap = l->phys_w;
        l->phys_w = l->phys_h;
        l->phys_h = swap;
    }
    l->off_x = 0;
    l->off_y = 0;
    l->horz_res = l->phys_w;
    l->vert_res = l->phys_h;
}

PrintLayout *print_layout_create(const wchar_t *text, const wchar_t *doc_title, const wchar_t *face,
                                 int point_size) {
    PrintLayout *l = calloc(1, sizeof(*l));
    if (!l)
        return NULL;

    l->text = wide_dup(text ? text : L"");
    l->title = wide_dup(doc_title ? doc_title : L"Untitled");
    l->header = load_hf("print_header", HEADER_DEFAULT);
    l->footer = load_hf("print_footer", FOOTER_DEFAULT);
    if (!l->text || !l->title) {
        print_layout_free(l);
        return NULL;
    }

    PageSetup ps;
    page_setup_load(&ps);

    PrintDevice dev;
    HDC dc = NULL;

    if (device_acquire_default(&dev)) {
        device_apply_orientation(&dev, ps.orientation);
        dc = CreateICW(dev.driver && dev.driver[0] ? dev.driver : L"WINSPOOL", dev.device, NULL,
                       dev.devmode);
    } else {
        ZeroMemory(&dev, sizeof(dev));
    }

    bool ok;
    if (dc) {
        l->has_printer = true;
        ok = layout_prepare(l, dc, face, point_size, true);
    } else {
        // Font metrics depend on the realised ppem, not on the destination
        // surface, so a screen-compatible memory DC measures a synthetic
        // 600 dpi device faithfully.
        layout_synthesise_paper(l);
        dc = CreateCompatibleDC(NULL);
        ok = (dc != NULL) && layout_prepare(l, dc, face, point_size, false);
    }

    if (dc)
        DeleteDC(dc);
    device_free(&dev);

    if (!ok) {
        print_layout_free(l);
        return NULL;
    }
    return l;
}

void print_layout_free(PrintLayout *l) {
    if (!l)
        return;
    if (l->font)
        DeleteObject(l->font);
    free(l->runs);
    free(l->header);
    free(l->footer);
    free(l->title);
    free(l->text);
    free(l);
}

int print_layout_page_count(const PrintLayout *l) {
    return l ? l->page_count : 0;
}

bool print_layout_has_printer(const PrintLayout *l) {
    return l ? l->has_printer : false;
}

void print_layout_paper(const PrintLayout *l, int *width, int *height, int *dpi_x, int *dpi_y) {
    if (!l)
        return;
    if (width)
        *width = l->phys_w;
    if (height)
        *height = l->phys_h;
    if (dpi_x)
        *dpi_x = l->dpi_x;
    if (dpi_y)
        *dpi_y = l->dpi_y;
}

int print_layout_begin_scaled(const PrintLayout *l, HDC hdc, const RECT *dest) {
    if (!l || !dest)
        return 0;
    int w = dest->right - dest->left;
    int h = dest->bottom - dest->top;
    if (w <= 0 || h <= 0 || l->phys_w <= 0 || l->phys_h <= 0)
        return 0;

    int saved = SaveDC(hdc);
    if (!saved)
        return 0;

    // GDI maps Dx = (Lx - WOx) * VEx/WEx + VOx. With the window origin at
    // -PHYSICALOFFSET the sheet corner lands on dest's corner and logical
    // (0,0) - the printer's own origin - lands inset by the hardware margin,
    // so the preview can replay print coordinates literally.
    SetMapMode(hdc, MM_ANISOTROPIC);
    SetWindowExtEx(hdc, l->phys_w, l->phys_h, NULL);
    SetViewportExtEx(hdc, w, h, NULL);
    SetWindowOrgEx(hdc, -l->off_x, -l->off_y, NULL);
    SetViewportOrgEx(hdc, dest->left, dest->top, NULL);
    // Hide whatever the printer's hardware margin would swallow
    IntersectClipRect(hdc, 0, 0, l->horz_res, l->vert_res);
    return saved;
}

void print_layout_end_scaled(HDC hdc, int saved) {
    if (saved)
        RestoreDC(hdc, saved);
}

void print_layout_draw_page(const PrintLayout *l, HDC hdc, int page) {
    if (!l || page < 1)
        return;

    HFONT old = l->font ? (HFONT) SelectObject(hdc, l->font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);
    UINT old_align = SetTextAlign(hdc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    COLORREF old_color = SetTextColor(hdc, RGB(0, 0, 0));

    if (l->header && l->header[0]) {
        RECT hr = l->body;
        hr.bottom = l->body.top + l->line_h;
        draw_hf(hdc, l->header, &hr, page, l->title, false);
    }

    for (int i = 0; i < l->run_count; i++) {
        const PrintRun *r = &l->runs[i];
        if (r->page != page)
            continue;
        TextOutW(hdc, r->x, r->y, l->text + r->offset, r->len);
    }

    if (l->footer && l->footer[0]) {
        RECT fr = l->body;
        fr.top = l->body.bottom - l->line_h;
        draw_hf(hdc, l->footer, &fr, page, l->title, true);
    }

    SetTextColor(hdc, old_color);
    SetTextAlign(hdc, old_align);
    SetBkMode(hdc, old_bk);
    if (old)
        SelectObject(hdc, old);
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

PrintResult print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                           const wchar_t *face, int point_size) {
    if (!text)
        return PRINT_RESULT_FAILED;

    PageSetup ps;
    page_setup_load(&ps);

    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    pd.nCopies = 1;

    if (!PrintDlgW(&pd))
        return PRINT_RESULT_CANCELLED;

    HDC hdc = pd.hDC;
    if (!hdc) {
        if (pd.hDevMode)
            GlobalFree(pd.hDevMode);
        if (pd.hDevNames)
            GlobalFree(pd.hDevNames);
        return PRINT_RESULT_FAILED;
    }

    // Apply the orientation chosen in Page Setup before any metric is read
    if (ps.orientation != 0 && pd.hDevMode) {
        DEVMODEW *dm = GlobalLock(pd.hDevMode);
        if (dm) {
            dm->dmFields |= DM_ORIENTATION;
            dm->dmOrientation = ps.orientation;
            ResetDCW(hdc, dm);
            GlobalUnlock(pd.hDevMode);
        }
    }

    // Lay out against the printer the user actually chose, which need not be
    // the default one a preview measured
    PrintLayout *l = calloc(1, sizeof(*l));
    bool ok = (l != NULL);
    if (ok) {
        l->text = wide_dup(text);
        l->title = wide_dup(doc_title ? doc_title : L"Untitled");
        l->header = load_hf("print_header", HEADER_DEFAULT);
        l->footer = load_hf("print_footer", FOOTER_DEFAULT);
        l->has_printer = true;
        ok =
            (l->text != NULL && l->title != NULL) && layout_prepare(l, hdc, face, point_size, true);
    }

    if (ok) {
        DOCINFOW di;
        ZeroMemory(&di, sizeof(di));
        di.cbSize = sizeof(di);
        di.lpszDocName = l->title;

        ok = (StartDocW(hdc, &di) > 0);
        for (int page = 1; ok && page <= l->page_count; page++) {
            if (StartPage(hdc) <= 0) {
                ok = false;
                break;
            }
            print_layout_draw_page(l, hdc, page);
            if (EndPage(hdc) <= 0) {
                ok = false;
                break;
            }
        }
        if (ok)
            EndDoc(hdc);
        else
            AbortDoc(hdc);
    }

    print_layout_free(l);
    DeleteDC(hdc);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);
    return ok ? PRINT_RESULT_PRINTED : PRINT_RESULT_FAILED;
}
