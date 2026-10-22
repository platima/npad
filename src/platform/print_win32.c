/*
 * npad - Printing and Print Preview (Win32)
 *
 * Author: Platima
 * https://npad.dev
 */

#include "print_win32.h"
#include "../core/print_core.h"
#include "../core/settings.h"
#include "../ui_interface.h"
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// The pagination in print_core works on UTF-16 units, which is what wchar_t
// is on Windows
typedef char print_wchar_is_utf16[sizeof(wchar_t) == sizeof(uint16_t) ? 1 : -1];

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

// Page Setup, the print dialog and a spooling job all pump messages; this
// counts how many are under way so the UI layer can hold back a close or a
// prompt until they are done (print_is_busy)
static int g_busy = 0;
static void (*g_idle_hook)(void) = NULL;
static void (*g_window_hook)(HWND) = NULL;

static void busy_begin(void) {
    g_busy++;
}

static void busy_end(void) {
    if (g_busy > 0)
        g_busy--;
    if (g_busy == 0 && g_idle_hook)
        g_idle_hook();
}

bool print_is_busy(void) {
    return g_busy > 0;
}

void print_set_idle_hook(void (*hook)(void)) {
    g_idle_hook = hook;
}

void print_set_window_hook(void (*hook)(HWND hwnd)) {
    g_window_hook = hook;
}

typedef struct {
    int left, right, top, bottom; // Thousandths of an inch, from the sheet edge
    short orientation;            // DMORIENT_PORTRAIT / _LANDSCAPE, 0 = printer default
    short paper_size;             // DMPAPER_*, 0 = printer default
    short paper_width;            // Tenths of a millimetre, only for DMPAPER_USER
    short paper_length;
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

static bool device_acquire_default(PrintDevice *dev);
static void device_free(PrintDevice *dev);

static void page_setup_load(PageSetup *ps) {
    ps->left = settings_get_int("print_margin_left", MARGIN_DEFAULT_SIDE);
    ps->right = settings_get_int("print_margin_right", MARGIN_DEFAULT_SIDE);
    ps->top = settings_get_int("print_margin_top", MARGIN_DEFAULT_TOPBOTTOM);
    ps->bottom = settings_get_int("print_margin_bottom", MARGIN_DEFAULT_TOPBOTTOM);
    ps->orientation = (short) settings_get_int("print_orientation", 0);
    ps->paper_size = (short) settings_get_int("print_paper_size", 0);
    ps->paper_width = (short) settings_get_int("print_paper_width", 0);
    ps->paper_length = (short) settings_get_int("print_paper_length", 0);
}

// Force the choices made in Page Setup into a DEVMODE. A field is honoured
// only when its bit is set in dmFields; a zero means "leave the printer's own".
// Every path that measures or prints must go through this, or the preview,
// the printout and the Page Setup dialog each end up describing a different
// sheet - which is exactly how "the preview fits more per line than the PDF"
// happened when only the orientation was carried across.
static void page_setup_apply(const PageSetup *ps, DEVMODEW *dm) {
    if (!dm)
        return;
    if (ps->orientation != 0) {
        dm->dmFields |= DM_ORIENTATION;
        dm->dmOrientation = ps->orientation;
    }
    if (ps->paper_size != 0) {
        dm->dmFields |= DM_PAPERSIZE;
        dm->dmPaperSize = ps->paper_size;
        if (ps->paper_size == DMPAPER_USER && ps->paper_width > 0 && ps->paper_length > 0) {
            dm->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH;
            dm->dmPaperWidth = ps->paper_width;
            dm->dmPaperLength = ps->paper_length;
        } else {
            // A named size and an explicit width/length are mutually exclusive
            // to most drivers; carrying both makes some ignore the name
            dm->dmFields &= ~(DWORD) (DM_PAPERWIDTH | DM_PAPERLENGTH);
        }
    }
}

// Read those same choices back out of a DEVMODE the dialog returned
static void page_setup_read(PageSetup *ps, const DEVMODEW *dm) {
    if (!dm)
        return;
    if (dm->dmFields & DM_ORIENTATION)
        ps->orientation = dm->dmOrientation;
    if (dm->dmFields & DM_PAPERSIZE) {
        ps->paper_size = dm->dmPaperSize;
        ps->paper_width = 0;
        ps->paper_length = 0;
        if ((dm->dmFields & (DM_PAPERWIDTH | DM_PAPERLENGTH)) == (DM_PAPERWIDTH | DM_PAPERLENGTH)) {
            ps->paper_width = dm->dmPaperWidth;
            ps->paper_length = dm->dmPaperLength;
        }
    }
}

static void page_setup_save(const PageSetup *ps) {
    settings_set_int("print_margin_left", ps->left);
    settings_set_int("print_margin_right", ps->right);
    settings_set_int("print_margin_top", ps->top);
    settings_set_int("print_margin_bottom", ps->bottom);
    settings_set_int("print_orientation", ps->orientation);
    settings_set_int("print_paper_size", ps->paper_size);
    settings_set_int("print_paper_width", ps->paper_width);
    settings_set_int("print_paper_length", ps->paper_length);
    // Broadcast as well as save: otherwise every other window keeps printing
    // with the old setup and writes it back over this one when it exits
    if (settings_save())
        ui_notify_settings_changed();
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

// LOCALE_IMEASURE: "0" metric, "1" U.S.
static bool locale_is_metric(void) {
    wchar_t buf[4] = { 0 };
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IMEASURE, buf, 4) > 0)
        return buf[0] == L'0';
    return false;
}

// A stored margin (thousandths of an inch) in the unit the dialog shows
static int margin_to_dialog(int stored, bool metric) {
    return metric ? print_margin_to_mm100(stored) : stored;
}

// What the dialog returned, back to thousandths of an inch. A field the user
// left alone keeps its stored value exactly, so accepting the dialog in
// millimetres never nudges a margin by the rounding between the two units.
static int margin_from_dialog(int returned, int shown, int stored, bool metric) {
    if (returned == shown)
        return stored;
    return metric ? print_margin_from_mm100(returned) : returned;
}

void print_show_page_setup(HWND owner) {
    PageSetup ps;
    page_setup_load(&ps);

    PAGESETUPDLGW psd;
    ZeroMemory(&psd, sizeof(psd));
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = owner;
    // Shown in the locale's unit, as notepad.exe and the GTK build do; stored
    // in thousandths of an inch whatever the machine's measurement system, so
    // a settings file means the same thing everywhere. Forcing inches made a
    // metric user type 0.79 for a 20 mm margin.
    bool metric = locale_is_metric();
    psd.Flags = (metric ? PSD_INHUNDREDTHSOFMILLIMETERS : PSD_INTHOUSANDTHSOFINCHES) | PSD_MARGINS;
    RECT shown;
    shown.left = margin_to_dialog(ps.left, metric);
    shown.right = margin_to_dialog(ps.right, metric);
    shown.top = margin_to_dialog(ps.top, metric);
    shown.bottom = margin_to_dialog(ps.bottom, metric);
    psd.rtMargin = shown;

    // Seed the dialog with the default printer's own DEVMODE (the full one,
    // driver-private tail included) with our persisted choices applied on
    // top. A fabricated sizeof(DEVMODEW) structure carrying only an
    // orientation made the dialog show the printer's paper rather than the
    // one previously chosen, and lost the choice again on OK.
    PrintDevice dev;
    if (device_acquire_default(&dev) && dev.devmode) {
        size_t bytes = (size_t) dev.devmode->dmSize + dev.devmode->dmDriverExtra;
        psd.hDevMode = GlobalAlloc(GHND, bytes);
        if (psd.hDevMode) {
            DEVMODEW *dm = GlobalLock(psd.hDevMode);
            if (dm) {
                memcpy(dm, dev.devmode, bytes);
                page_setup_apply(&ps, dm);
                GlobalUnlock(psd.hDevMode);
            }
        }
    }
    device_free(&dev);

    busy_begin();
    BOOL accepted = PageSetupDlgW(&psd);
    if (accepted) {
        ps.left = margin_from_dialog(psd.rtMargin.left, shown.left, ps.left, metric);
        ps.right = margin_from_dialog(psd.rtMargin.right, shown.right, ps.right, metric);
        ps.top = margin_from_dialog(psd.rtMargin.top, shown.top, ps.top, metric);
        ps.bottom = margin_from_dialog(psd.rtMargin.bottom, shown.bottom, ps.bottom, metric);
        if (psd.hDevMode) {
            const DEVMODEW *dm = GlobalLock(psd.hDevMode);
            page_setup_read(&ps, dm);
            if (dm)
                GlobalUnlock(psd.hDevMode);
        }
        page_setup_save(&ps);
    }

    if (psd.hDevMode)
        GlobalFree(psd.hDevMode);
    if (psd.hDevNames)
        GlobalFree(psd.hDevNames);
    busy_end();
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
                _snwprintf(buf, 127, L"%ls", title ? title : L"Untitled");
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
    // hDevMode/hDevNames must be NULL on entry. PD_NOWARNING: no message box
    // when there is no printer - the callers handle that themselves.
    pd.Flags = PD_RETURNDEFAULT | PD_NOWARNING;
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

// What pagination measures with and reports to: the printer DC, the layout
// the runs go into, and the progress window that can cancel a long job
typedef struct PrintProgress PrintProgress;
static bool progress_keep_going(const PrintProgress *p);

typedef struct {
    PrintLayout *layout;
    HDC dc;
    PrintProgress *progress; // NULL for a preview, which has no Cancel
} MeasureCtx;

static bool measure_fit(void *ctx, const uint16_t *s, int len, int max_extent, int *fit, int *dx) {
    const MeasureCtx *m = ctx;
    SIZE sz;
    // The only API that reports how many characters fit a width. Note the
    // returned SIZE covers the whole string, not the prefix - the fitted
    // width is the last cumulative entry in dx.
    return GetTextExtentExPointW(m->dc, (const wchar_t *) s, len, max_extent, fit, dx, &sz) != 0;
}

static bool measure_extent(void *ctx, const uint16_t *s, int len, int *width) {
    const MeasureCtx *m = ctx;
    SIZE sz = { 0, 0 };
    if (!GetTextExtentPoint32W(m->dc, (const wchar_t *) s, len, &sz))
        return false;
    *width = (int) sz.cx;
    return true;
}

static bool measure_emit(void *ctx, int page, int line, int x, size_t offset, int len) {
    const MeasureCtx *m = ctx;
    PrintLayout *l = m->layout;
    return layout_add_run(l, page, l->text_area.left + x, l->text_area.top + line * l->line_h,
                          (int) offset, len);
}

static bool measure_keep_going(void *ctx) {
    const MeasureCtx *m = ctx;
    return progress_keep_going(m->progress);
}

// Paginate the whole document against dc, which must be the printer's DC or
// information context with the print font already selected
static PrintPaginateResult layout_paginate(PrintLayout *l, HDC dc, int avg_char_width,
                                           PrintProgress *progress) {
    MeasureCtx ctx = { l, dc, progress };
    PrintPaginateOps ops = { measure_fit, measure_extent, measure_emit,
                             progress ? measure_keep_going : NULL, &ctx };
    PrintPaginateGeometry geometry = { l->wrap_w, l->tab_w, l->lines_per_page, avg_char_width };
    int pages = 1;
    PrintPaginateResult result =
        print_paginate((const uint16_t *) l->text, wcslen(l->text), &geometry, &ops, &pages);
    // Pages past the last drawn line are blank, so they are not pages
    l->page_count = pages;
    return result;
}

// Select the print font on dc and derive every metric pagination needs.
// read_metrics is false only for the synthetic no-printer page, whose geometry
// is already filled in and must not be replaced by the measuring DC's.
static PrintPaginateResult layout_prepare(PrintLayout *l, HDC dc, const PrintFont *font,
                                          bool read_metrics, PrintProgress *progress) {
    PageSetup ps;
    page_setup_load(&ps);

    if (read_metrics)
        layout_read_metrics(l, dc);
    layout_compute_body(l, &ps);

    wchar_t face_buf[LF_FACESIZE];
    face_buf[0] = L'\0';
    if (font && font->face && font->face[0]) {
        wcsncpy(face_buf, font->face, LF_FACESIZE - 1);
        face_buf[LF_FACESIZE - 1] = L'\0';
    }
    int pt = font ? font->point_size : 11;
    if (pt < 6)
        pt = 6;
    if (pt > 72)
        pt = 72;
    // Bold and italic as the editor shows them: the GTK build always carried
    // them through, and Win32 printed every document in regular weight
    int weight = font ? font->weight : FW_NORMAL;
    if (weight <= 0)
        weight = FW_NORMAL;
    if (weight > FW_HEAVY)
        weight = FW_HEAVY;
    BOOL italic = (font && font->italic) ? TRUE : FALSE;

    // OUT_TT_ONLY_PRECIS stops a driver substituting a printer-resident font
    // with different metrics, which would make the preview and the printed
    // page disagree about where lines wrap.
    l->font =
        CreateFontW(-MulDiv(pt, l->dpi_y, 72), 0, 0, 0, weight, italic, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, face_buf[0] ? face_buf : L"Consolas");
    if (!l->font)
        return PRINT_PAGINATE_NO_MEMORY;

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

    PrintPaginateResult result = layout_paginate(l, dc, tm.tmAveCharWidth, progress);

    if (old)
        SelectObject(dc, old);
    return result;
}

// Geometry for a machine with no printer at all: Microsoft Print to PDF can be
// removed by policy, and Server Core has no spooler. Preview still works.
static void layout_synthesise_paper(PrintLayout *l) {
    PageSetup ps;
    page_setup_load(&ps);

    // LOCALE_IPAPERSIZE uses the DMPAPER_* numbering: 1 Letter, 5 Legal, 9 A4
    long paper = ps.paper_size;
    if (paper == 0) {
        wchar_t buf[16] = { 0 };
        paper = DMPAPER_LETTER;
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IPAPERSIZE, buf, 16) > 0)
            paper = wcstol(buf, NULL, 10);
    }

    const int dpi = 600;
    l->dpi_x = dpi;
    l->dpi_y = dpi;
    if (paper == DMPAPER_USER && ps.paper_width > 0 && ps.paper_length > 0) {
        l->phys_w = MulDiv(ps.paper_width, dpi, 254); // Tenths of a millimetre
        l->phys_h = MulDiv(ps.paper_length, dpi, 254);
    } else if (paper == DMPAPER_A4) { // 210 x 297 mm
        l->phys_w = MulDiv(2100, dpi, 254);
        l->phys_h = MulDiv(2970, dpi, 254);
    } else if (paper == DMPAPER_LEGAL) { // 8.5 x 14 in
        l->phys_w = MulDiv(85, dpi, 10);
        l->phys_h = 14 * dpi;
    } else { // Letter, 8.5 x 11 in - and anything else we cannot name
        l->phys_w = MulDiv(85, dpi, 10);
        l->phys_h = 11 * dpi;
    }
    if (ps.orientation == DMORIENT_LANDSCAPE) {
        int swap = l->phys_w;
        l->phys_w = l->phys_h;
        l->phys_h = swap;
    }
    l->off_x = 0;
    l->off_y = 0;
    l->horz_res = l->phys_w;
    l->vert_res = l->phys_h;
}

PrintLayout *print_layout_create(const wchar_t *text, const wchar_t *doc_title,
                                 const PrintFont *font) {
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
        page_setup_apply(&ps, dev.devmode);
        dc = CreateICW(dev.driver && dev.driver[0] ? dev.driver : L"WINSPOOL", dev.device, NULL,
                       dev.devmode);
    } else {
        ZeroMemory(&dev, sizeof(dev));
    }

    bool ok;
    if (dc) {
        l->has_printer = true;
        ok = layout_prepare(l, dc, font, true, NULL) == PRINT_PAGINATE_OK;
    } else {
        // Font metrics depend on the realised ppem, not on the destination
        // surface, so a screen-compatible memory DC measures a synthetic
        // 600 dpi device faithfully.
        layout_synthesise_paper(l);
        dc = CreateCompatibleDC(NULL);
        ok = (dc != NULL) && layout_prepare(l, dc, font, false, NULL) == PRINT_PAGINATE_OK;
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

    // Runs are stored in page order, so a page's runs are found by bisection.
    // Scanning every run for every page made printing (and each preview paint
    // of a late page) cost the whole document per page.
    int lo = 0, hi = l->run_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (l->runs[mid].page < page)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (int i = lo; i < l->run_count && l->runs[i].page == page; i++) {
        const PrintRun *r = &l->runs[i];
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
// Progress and Cancel
//
// Spooling a long document took the UI thread for as long as it lasted, with
// nothing on screen and no way to stop it. A small modeless window now shows
// the page being sent and has Cancel; SetAbortProc and the pagination's own
// keep_going pump messages so it stays live.
// ---------------------------------------------------------------------------

#define PROGRESS_ID_TITLE 100
#define PROGRESS_ID_STATUS 101

struct PrintProgress {
    HWND dlg;
    HWND owner;
    bool owner_disabled; // We disabled it, so we re-enable it
    bool cancelled;
};

// SetAbortProc's callback is handed only the DC, so the job in flight lives
// here. There is only ever one: print_document refuses to nest.
static PrintProgress *g_progress = NULL;
static bool g_in_print = false;

// A DLGTEMPLATEEX, written field by field: DS_SHELLFONT (Segoe UI rather than
// MS Sans Serif) needs the extended form, which has no struct in the headers,
// and a template in memory keeps the resource script out of it
#define PROGRESS_TEMPLATE_DWORDS 256

static void tmpl_word(WORD **p, WORD v) {
    *(*p)++ = v;
}

static void tmpl_dword(WORD **p, DWORD v) {
    tmpl_word(p, LOWORD(v));
    tmpl_word(p, HIWORD(v));
}

static void tmpl_string(WORD **p, const wchar_t *s) {
    do {
        tmpl_word(p, (WORD) *s);
    } while (*s++);
}

static void tmpl_align(WORD **p) {
    *p = (WORD *) (((ULONG_PTR) *p + 3) & ~(ULONG_PTR) 3);
}

static void tmpl_item(WORD **p, DWORD style, short x, short y, short cx, short cy, DWORD id,
                      WORD cls, const wchar_t *text) {
    tmpl_align(p);
    tmpl_dword(p, 0); // helpID
    tmpl_dword(p, 0); // exStyle
    tmpl_dword(p, style | WS_CHILD | WS_VISIBLE);
    tmpl_word(p, (WORD) x);
    tmpl_word(p, (WORD) y);
    tmpl_word(p, (WORD) cx);
    tmpl_word(p, (WORD) cy);
    tmpl_dword(p, id);
    tmpl_word(p, 0xFFFF); // Predefined class by ordinal
    tmpl_word(p, cls);
    tmpl_string(p, text);
    tmpl_word(p, 0); // No creation data
}

static const DLGTEMPLATE *progress_template(DWORD *buf) {
    WORD *p = (WORD *) buf;
    tmpl_word(&p, 1);      // dlgVer
    tmpl_word(&p, 0xFFFF); // signature: DLGTEMPLATEEX
    tmpl_dword(&p, 0);     // helpID
    tmpl_dword(&p, 0);     // exStyle
    tmpl_dword(&p, DS_SHELLFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    tmpl_word(&p, 3); // Controls
    tmpl_word(&p, 0);
    tmpl_word(&p, 0);
    tmpl_word(&p, 210); // Width and height in dialog units
    tmpl_word(&p, 62);
    tmpl_word(&p, 0); // No menu
    tmpl_word(&p, 0); // Default dialog class
    tmpl_string(&p, L"npad");
    tmpl_word(&p, 8); // Point size
    tmpl_word(&p, FW_NORMAL);
    tmpl_word(&p, MAKEWORD(FALSE, DEFAULT_CHARSET)); // italic, charset
    tmpl_string(&p, L"MS Shell Dlg");

    tmpl_item(&p, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 10, 9, 190, 9, PROGRESS_ID_TITLE, 0x0082,
              L"");
    tmpl_item(&p, SS_LEFT | SS_NOPREFIX, 10, 22, 190, 9, PROGRESS_ID_STATUS, 0x0082,
              L"Preparing pages...");
    tmpl_item(&p, BS_DEFPUSHBUTTON | WS_TABSTOP, 150, 40, 50, 14, IDCANCEL, 0x0080, L"Cancel");
    return (const DLGTEMPLATE *) buf;
}

static void progress_cancel(HWND dlg) {
    if (g_progress)
        g_progress->cancelled = true;
    SetDlgItemTextW(dlg, PROGRESS_ID_STATUS, L"Cancelling...");
    EnableWindow(GetDlgItem(dlg, IDCANCEL), FALSE);
}

static INT_PTR CALLBACK progress_proc(HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void) lparam;
    switch (msg) {
        case WM_INITDIALOG:
            if (g_window_hook)
                g_window_hook(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDCANCEL) {
                progress_cancel(dlg);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            progress_cancel(dlg);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// Keep the progress window (and everything else) responsive. A WM_QUIT seen
// here is put back for the main loop and pumping stops for now: the job runs
// to its end and the application quits after it.
static void progress_pump(const PrintProgress *p) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int) msg.wParam);
            break;
        }
        // Only for messages bound for the progress window: the dialog manager
        // must not see keys meant for anything else (see LESSONS.md)
        if (p && p->dlg && (msg.hwnd == p->dlg || IsChild(p->dlg, msg.hwnd)) &&
            IsDialogMessageW(p->dlg, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool progress_keep_going(const PrintProgress *p) {
    if (!p)
        return true;
    progress_pump(p);
    return !p->cancelled;
}

static BOOL CALLBACK print_abort_proc(HDC hdc, int code) {
    (void) hdc;
    (void) code;
    return progress_keep_going(g_progress) ? TRUE : FALSE;
}

// Works without a window too (no Cancel then, but the job still prints)
static PrintProgress *progress_begin(HWND owner, const wchar_t *title) {
    PrintProgress *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->owner = owner;
    g_progress = p;

    DWORD buf[PROGRESS_TEMPLATE_DWORDS];
    ZeroMemory(buf, sizeof(buf));
    p->dlg = CreateDialogIndirectParamW(GetModuleHandleW(NULL), progress_template(buf), owner,
                                        progress_proc, 0);
    if (p->dlg) {
        wchar_t line[300];
        _snwprintf(line, 299, L"Printing %ls", title ? title : L"Untitled");
        line[299] = L'\0';
        SetDlgItemTextW(p->dlg, PROGRESS_ID_TITLE, line);
        // Modal to its owner while the job runs: messages are pumped, and the
        // window must not start a second print or edit what is being printed
        if (owner && IsWindowEnabled(owner)) {
            EnableWindow(owner, FALSE);
            p->owner_disabled = true;
        }
        ShowWindow(p->dlg, SW_SHOW);
        UpdateWindow(p->dlg);
    }
    return p;
}

static void progress_set_page(const PrintProgress *p, int page, int count) {
    if (!p || !p->dlg || p->cancelled)
        return;
    wchar_t line[64];
    _snwprintf(line, 63, L"Sending page %d of %d", page, count);
    line[63] = L'\0';
    SetDlgItemTextW(p->dlg, PROGRESS_ID_STATUS, line);
}

static void progress_end(PrintProgress *p) {
    if (!p)
        return;
    // Before DestroyWindow, not after: re-enabling the owner only once the
    // progress window is gone lets Windows activate another application
    if (p->owner_disabled)
        EnableWindow(p->owner, TRUE);
    if (p->dlg)
        DestroyWindow(p->dlg);
    if (g_progress == p)
        g_progress = NULL;
    free(p);
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

// Exists to make comdlg32 use the classic dialog - see the PD_ENABLEPRINTHOOK
// note in print_document. It must still answer WM_INITDIALOG with TRUE: 0
// there means "the hook set the focus itself", so the dialog skipped focusing
// its default button. Opened from the editor that was masked by activation;
// opened from Print Preview - whose owner is disabled on purpose - the dialog
// came up unfocused and Enter went nowhere. Bring it forward as well, for the
// same reason.
static UINT_PTR CALLBACK print_dialog_hook(HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void) wparam;
    (void) lparam;
    if (msg == WM_INITDIALOG) {
        SetForegroundWindow(dlg);
        return 1; // Let the dialog set focus to its default control
    }
    return 0;
}

// Cancel on a prompt the spooler or driver shows - Microsoft Print to PDF's
// and Print to file's "save as" - surfaces as a failed StartDoc with one of
// these. It is the user's choice, not something that went wrong.
static bool is_cancel_error(DWORD error) {
    return error == ERROR_CANCELLED || error == ERROR_PRINT_CANCELLED;
}

// Apply the Page Setup choices (orientation AND paper) before any metric is
// read. The dialog may have handed back a DEVMODE describing a different
// sheet - Windows 11's modern dialog does so unless the user ticks "let the
// app change my printing preferences" - and the preview was measured against
// ours.
static void apply_page_setup_to_dc(HDC hdc, HGLOBAL devmode, const PageSetup *ps) {
    if ((ps->orientation == 0 && ps->paper_size == 0) || !devmode)
        return;
    DEVMODEW *dm = GlobalLock(devmode);
    if (dm) {
        page_setup_apply(ps, dm);
        ResetDCW(hdc, dm);
        GlobalUnlock(devmode);
    }
}

// Lay out against hdc and send every page. output is DOCINFO's lpszOutput:
// NULL for the printer's own port, "FILE:" for Print to file.
static PrintResult print_render(HWND owner, HDC hdc, const wchar_t *output, const wchar_t *text,
                                const wchar_t *doc_title, const PrintFont *font) {
    // Lay out against the printer the user actually chose, which need not be
    // the default one a preview measured
    PrintLayout *l = calloc(1, sizeof(*l));
    if (!l)
        return PRINT_RESULT_FAILED;
    l->text = wide_dup(text);
    l->title = wide_dup(doc_title ? doc_title : L"Untitled");
    l->header = load_hf("print_header", HEADER_DEFAULT);
    l->footer = load_hf("print_footer", FOOTER_DEFAULT);
    l->has_printer = true;
    if (!l->text || !l->title) {
        print_layout_free(l);
        return PRINT_RESULT_FAILED;
    }

    PrintProgress *progress = progress_begin(owner, l->title);
    PrintResult result = PRINT_RESULT_PRINTED;
    PrintPaginateResult paginated = layout_prepare(l, hdc, font, true, progress);
    if (paginated == PRINT_PAGINATE_CANCELLED || (progress && progress->cancelled))
        result = PRINT_RESULT_CANCELLED;
    else if (paginated != PRINT_PAGINATE_OK)
        result = PRINT_RESULT_FAILED;

    if (result == PRINT_RESULT_PRINTED) {
        SetAbortProc(hdc, print_abort_proc);

        DOCINFOW di;
        ZeroMemory(&di, sizeof(di));
        di.cbSize = sizeof(di);
        di.lpszDocName = l->title;
        di.lpszOutput = output;

        if (StartDocW(hdc, &di) <= 0) {
            DWORD error = GetLastError();
            result = (is_cancel_error(error) || (progress && progress->cancelled))
                         ? PRINT_RESULT_CANCELLED
                         : PRINT_RESULT_FAILED;
        } else {
            for (int page = 1; page <= l->page_count; page++) {
                progress_set_page(progress, page, l->page_count);
                if (!progress_keep_going(progress)) {
                    result = PRINT_RESULT_CANCELLED;
                    break;
                }
                if (StartPage(hdc) <= 0) {
                    DWORD error = GetLastError();
                    result = is_cancel_error(error) ? PRINT_RESULT_CANCELLED : PRINT_RESULT_FAILED;
                    break;
                }
                print_layout_draw_page(l, hdc, page);
                if (EndPage(hdc) <= 0) {
                    // SP_APPABORT when the abort procedure said stop
                    DWORD error = GetLastError();
                    result = (is_cancel_error(error) || (progress && progress->cancelled))
                                 ? PRINT_RESULT_CANCELLED
                                 : PRINT_RESULT_FAILED;
                    break;
                }
            }
            if (result == PRINT_RESULT_PRINTED) {
                if (EndDoc(hdc) <= 0) {
                    DWORD error = GetLastError();
                    result = is_cancel_error(error) ? PRINT_RESULT_CANCELLED : PRINT_RESULT_FAILED;
                }
            } else {
                AbortDoc(hdc);
            }
        }
    }

    progress_end(progress);
    print_layout_free(l);
    return result;
}

static PrintResult print_with_dialog(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                                     const PrintFont *font) {
    PageSetup ps;
    page_setup_load(&ps);

    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    // PD_ENABLEPRINTHOOK with a hook that does nothing: Windows 11's modern
    // print dialog ignores the DEVMODE an application passes in - orientation
    // and paper included - whatever "let the app change my printing
    // preferences" says, so it showed "Portrait" for a landscape job. Giving
    // the dialog a hook makes comdlg32 fall back to the classic dialog, which
    // honours the DEVMODE. That dialog also has no preview pane, so the
    // "This app doesn't support print preview" message goes with it.
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_ENABLEPRINTHOOK;
    pd.lpfnPrintHook = print_dialog_hook;
    pd.nCopies = 1;

    // Seed the dialog with the default printer's DEVMODE plus the Page Setup
    // choices, so it SHOWS the orientation and paper that will be used. Opened
    // with no DEVMODE it displayed the printer's own defaults ("Portrait")
    // while the page, correctly, came out landscape - the choices were only
    // applied after it closed. Page Setup remains the source of truth below,
    // as in notepad.exe; the dialog's copy is what it reports back.
    {
        PrintDevice dev;
        if (device_acquire_default(&dev) && dev.devmode) {
            size_t bytes = (size_t) dev.devmode->dmSize + dev.devmode->dmDriverExtra;
            pd.hDevMode = GlobalAlloc(GHND, bytes);
            if (pd.hDevMode) {
                DEVMODEW *dm = GlobalLock(pd.hDevMode);
                if (dm) {
                    memcpy(dm, dev.devmode, bytes);
                    page_setup_apply(&ps, dm);
                    GlobalUnlock(pd.hDevMode);
                }
            }
        }
        device_free(&dev);
    }

    PrintResult result;
    if (!PrintDlgW(&pd)) {
        result = PRINT_RESULT_CANCELLED;
    } else if (!pd.hDC) {
        result = PRINT_RESULT_FAILED;
    } else {
        apply_page_setup_to_dc(pd.hDC, pd.hDevMode, &ps);
        // The classic dialog has a "Print to file" box. Ticked, the job has to
        // be pointed at the FILE: port, which makes the spooler ask for the
        // file name; left at NULL it went to the printer regardless. (Copies
        // need nothing here: since Vista the DC carries them.)
        const wchar_t *output = (pd.Flags & PD_PRINTTOFILE) ? L"FILE:" : NULL;
        result = print_render(owner, pd.hDC, output, text, doc_title, font);
    }

    if (pd.hDC)
        DeleteDC(pd.hDC);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);
    return result;
}

PrintResult print_document(HWND owner, const wchar_t *text, const wchar_t *doc_title,
                           const PrintFont *font) {
    if (!text)
        return PRINT_RESULT_FAILED;
    // Messages are pumped while a job spools; nothing reachable should start
    // a second one, and if something did it must not run inside the first
    if (g_in_print)
        return PRINT_RESULT_CANCELLED;
    g_in_print = true;
    busy_begin();
    PrintResult result = print_with_dialog(owner, text, doc_title, font);
    g_in_print = false;
    busy_end();
    return result;
}

PrintResult print_document_default(const wchar_t *text, const wchar_t *doc_title,
                                   const PrintFont *font) {
    if (!text)
        return PRINT_RESULT_FAILED;
    if (g_in_print)
        return PRINT_RESULT_CANCELLED;
    g_in_print = true;
    busy_begin();

    PageSetup ps;
    page_setup_load(&ps);

    // The default printer's DC, with no dialog; PD_NOWARNING because the
    // caller reports a missing printer itself
    PRINTDLGW pd;
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.Flags = PD_RETURNDEFAULT | PD_RETURNDC | PD_NOWARNING;

    PrintResult result;
    if (!PrintDlgW(&pd)) {
        result = PRINT_RESULT_NO_PRINTER;
    } else if (!pd.hDC) {
        result = PRINT_RESULT_FAILED;
    } else {
        apply_page_setup_to_dc(pd.hDC, pd.hDevMode, &ps);
        result = print_render(NULL, pd.hDC, NULL, text, doc_title, font);
    }

    if (pd.hDC)
        DeleteDC(pd.hDC);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);

    g_in_print = false;
    busy_end();
    return result;
}
