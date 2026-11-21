// dialog-preview: capture npad's Win32 dialog templates as images, for
// checking a layout change without running npad.
//
// It links npad's compiled resources (see `make dialog-preview`), builds the
// dialogs from those templates off-screen, captures each with PrintWindow and
// exits by itself. None of npad's code runs: no settings are read or written,
// %APPDATA% is never touched, and the windows are created hidden and disabled,
// placed far off-screen and shown without activation. Creating a property
// sheet still activates it, so for the few seconds it runs the tool also holds
// the foreground lock (LockSetForegroundWindow): the window you are working in
// keeps the keyboard. The lock is released on exit, or by any click or Alt.
//
// Usage: dialog-preview [--dialogs] [--bmp] [--caption TEXT] <outdir> <id>...
//   <id>       dialog template IDs from src/platform/resource.h
//   default    the IDs are pages of one property sheet, like Preferences
//              (210 218 211 214 213 212 216 217 215 is npad's own order);
//              each page is written as <outdir>\page-<id>.png
//   --dialogs  each ID is a standalone dialog (200 Find, 201 Replace,
//              202 Go To, ...), written as <outdir>\dialog-<id>.png
//   --bmp      write 24-bit BMP instead of PNG
//   --caption  the property sheet's title (default "Preferences")
//
// What it cannot show: anything npad's dialog procedures do at run time, such
// as filling combo boxes, setting values from settings, or dark mode. The
// first radio button of each group is ticked so groups look as they do in npad.
//
// Exit status: 0 all written, 1 a capture or write failed, 2 usage or an
// unknown template ID, 3 a window could not be created.

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define OFFSCREEN_POS (-30000)
#define MAX_TEMPLATES 64

static void usage(void) {
    fwprintf(stderr, L"Usage: dialog-preview [--dialogs] [--bmp] [--caption TEXT] <outdir> "
                     L"<id>...\n"
                     L"  Preferences pages: dialog-preview out 210 218 211 214 213 212 216 217 "
                     L"215\n"
                     L"  Standalone dialogs: dialog-preview --dialogs out 200 201 202 203 204\n");
}

// Tick the first radio button of every group, as npad always has one selected
static void tick_first_radios(HWND dlg) {
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[32];
        GetClassNameW(c, cls, (int) (sizeof(cls) / sizeof(cls[0])));
        LONG style = GetWindowLongW(c, GWL_STYLE);
        if (_wcsicmp(cls, L"Button") == 0 && (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON &&
            (style & WS_GROUP))
            SendMessageW(c, BM_SETCHECK, BST_CHECKED, 0);
    }
}

static INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM w, LPARAM l) {
    (void) w;
    (void) l;
    if (msg == WM_INITDIALOG) {
        tick_first_radios(dlg);
        return TRUE;
    }
    return FALSE;
}

// Make a template create its window hidden, without a taskbar button and
// unable to take activation: the property sheet sets focus whenever it changes
// page, and focus activates the window (and, with the foreground rights a
// process started from the active window inherits, takes the foreground). A
// disabled top-level window cannot be focused or activated, and its controls
// still draw as enabled. Works on a DLGTEMPLATE or a DLGTEMPLATEEX.
static void quiet_template(void *tmpl) {
    BYTE *p = (BYTE *) tmpl;
    WORD signature;
    DWORD style, ex_style;
    size_t style_at, ex_style_at;
    memcpy(&signature, p + 2, sizeof(signature));
    if (signature == 0xFFFF) {
        // DLGTEMPLATEEX: dlgVer, signature, helpID, exStyle, style
        ex_style_at = 8;
        style_at = 12;
    } else {
        // DLGTEMPLATE: style, dwExtendedStyle
        style_at = 0;
        ex_style_at = 4;
    }
    memcpy(&style, p + style_at, sizeof(style));
    memcpy(&ex_style, p + ex_style_at, sizeof(ex_style));
    style = (style & ~(DWORD) (WS_VISIBLE | DS_SETFOREGROUND)) | WS_DISABLED;
    ex_style |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    memcpy(p + style_at, &style, sizeof(style));
    memcpy(p + ex_style_at, &ex_style, sizeof(ex_style));
}

static void place_offscreen(HWND hwnd) {
    SetWindowPos(hwnd, NULL, OFFSCREEN_POS, OFFSCREEN_POS, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static int CALLBACK sheet_callback(HWND hwnd, UINT msg, LPARAM l) {
    if (msg == PSCB_PRECREATE && l)
        quiet_template((void *) l);
    else if (msg == PSCB_INITIALIZED)
        place_offscreen(hwnd);
    return 0;
}

// Let the windows paint before they are captured
static void pump(int ms) {
    DWORD end = GetTickCount() + (DWORD) ms;
    MSG m;
    while ((LONG) (end - GetTickCount()) > 0) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
}

static int write_bmp(const wchar_t *path, const BYTE *bits, int w, int h, int stride) {
    BITMAPINFOHEADER bi = { sizeof(bi), w, -h, 1, 24, BI_RGB, 0, 0, 0, 0, 0 };
    BITMAPFILEHEADER bf = { 0x4D42, (DWORD) (sizeof(bf) + sizeof(bi) + (size_t) stride * h), 0, 0,
                            (DWORD) (sizeof(bf) + sizeof(bi)) };
    FILE *f = _wfopen(path, L"wb");
    if (!f)
        return 0;
    int ok = fwrite(&bf, sizeof(bf), 1, f) == 1 && fwrite(&bi, sizeof(bi), 1, f) == 1 &&
             fwrite(bits, (size_t) stride, (size_t) h, f) == (size_t) h;
    return fclose(f) == 0 && ok;
}

static int write_png(const wchar_t *path, BYTE *bits, int w, int h, int stride) {
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **) &factory);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateStream(factory, &stream);
    if (SUCCEEDED(hr))
        hr = IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_Initialize(encoder, (IStream *) stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &props);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_Initialize(frame, props);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_SetSize(frame, (UINT) w, (UINT) h);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (SUCCEEDED(hr) && !IsEqualGUID(&format, &GUID_WICPixelFormat24bppBGR))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_WritePixels(frame, (UINT) h, (UINT) stride,
                                               (UINT) stride * (UINT) h, bits);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_Commit(encoder);
    if (props)
        IPropertyBag2_Release(props);
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (stream)
        IWICStream_Release(stream);
    if (factory)
        IWICImagingFactory_Release(factory);
    return SUCCEEDED(hr);
}

// Capture a window with PrintWindow, cropped to its visible frame (the window
// rect also holds the invisible resize borders DWM draws shadows in)
static int capture(HWND hwnd, const wchar_t *path, int as_bmp) {
    RECT wr, fr;
    GetWindowRect(hwnd, &wr);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0)
        return 0;
    RECT crop = { 0, 0, w, h };
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr))) &&
        fr.left >= wr.left && fr.top >= wr.top && fr.right <= wr.right && fr.bottom <= wr.bottom &&
        fr.right > fr.left && fr.bottom > fr.top) {
        crop.left = fr.left - wr.left;
        crop.top = fr.top - wr.top;
        crop.right = fr.right - wr.left;
        crop.bottom = fr.bottom - wr.top;
    }

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    int ok = PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT) != 0;
    SelectObject(mem, old);

    BITMAPINFOHEADER bi = { sizeof(bi), w, -h, 1, 24, BI_RGB, 0, 0, 0, 0, 0 };
    int stride = (w * 3 + 3) & ~3;
    BYTE *bits = (BYTE *) malloc((size_t) stride * (size_t) h);
    if (ok && bits)
        ok = GetDIBits(mem, bmp, 0, (UINT) h, bits, (BITMAPINFO *) &bi, DIB_RGB_COLORS) == h;
    else
        ok = 0;
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    int cw = crop.right - crop.left, ch = crop.bottom - crop.top;
    int cstride = (cw * 3 + 3) & ~3;
    BYTE *out = ok ? (BYTE *) calloc((size_t) cstride, (size_t) ch) : NULL;
    if (out) {
        for (int y = 0; y < ch; y++)
            memcpy(out + (size_t) y * cstride,
                   bits + (size_t) (y + crop.top) * stride + (size_t) crop.left * 3,
                   (size_t) cw * 3);
        ok = as_bmp ? write_bmp(path, out, cw, ch, cstride) : write_png(path, out, cw, ch, cstride);
    } else {
        ok = 0;
    }
    free(out);
    free(bits);
    return ok;
}

static int finish(HWND hwnd, const wchar_t *path, int as_bmp) {
    DWORD fg_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
    if (fg_pid == GetCurrentProcessId())
        fwprintf(stderr, L"warning: a preview window took the foreground\n");
    if (capture(hwnd, path, as_bmp)) {
        wprintf(L"%ls\n", path);
        return 1;
    }
    fwprintf(stderr, L"error: could not capture or write %ls\n", path);
    return 0;
}

static int preview_sheet(const wchar_t *outdir, const WORD *ids, int n, const wchar_t *caption,
                         int as_bmp) {
    PROPSHEETPAGEW pages[MAX_TEMPLATES];
    ZeroMemory(pages, sizeof(pages));
    for (int i = 0; i < n; i++) {
        pages[i].dwSize = sizeof(PROPSHEETPAGEW);
        pages[i].hInstance = GetModuleHandleW(NULL);
        pages[i].pszTemplate = MAKEINTRESOURCEW(ids[i]);
        pages[i].pfnDlgProc = dialog_proc;
    }
    PROPSHEETHEADERW psh;
    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_MODELESS | PSH_USECALLBACK;
    psh.hInstance = GetModuleHandleW(NULL);
    psh.pszCaption = caption;
    psh.nPages = (UINT) n;
    psh.ppsp = pages;
    psh.pfnCallback = sheet_callback;
    HWND sheet = (HWND) PropertySheetW(&psh);
    if ((INT_PTR) sheet <= 0) {
        fwprintf(stderr, L"error: the property sheet could not be created\n");
        return 3;
    }
    place_offscreen(sheet);
    ShowWindow(sheet, SW_SHOWNOACTIVATE);
    pump(400);

    int failed = 0;
    for (int i = 0; i < n; i++) {
        wchar_t path[MAX_PATH];
        SendMessageW(sheet, PSM_SETCURSEL, (WPARAM) i, 0);
        pump(250);
        _snwprintf(path, MAX_PATH, L"%ls\\page-%u.%ls", outdir, (unsigned) ids[i],
                   as_bmp ? L"bmp" : L"png");
        path[MAX_PATH - 1] = L'\0';
        if (!finish(sheet, path, as_bmp))
            failed = 1;
    }
    DestroyWindow(sheet);
    return failed;
}

static int preview_dialogs(const wchar_t *outdir, const WORD *ids, int n, int as_bmp) {
    int failed = 0;
    for (int i = 0; i < n; i++) {
        HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(ids[i]), (LPCWSTR) RT_DIALOG);
        HGLOBAL handle = res ? LoadResource(NULL, res) : NULL;
        const void *data = handle ? LockResource(handle) : NULL;
        DWORD size = res ? SizeofResource(NULL, res) : 0;
        void *copy = (data && size) ? malloc(size) : NULL;
        if (!copy) {
            fwprintf(stderr, L"error: could not load dialog %u\n", (unsigned) ids[i]);
            return 3;
        }
        memcpy(copy, data, size);
        quiet_template(copy);
        HWND dlg = CreateDialogIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW) copy, NULL,
                                              dialog_proc, 0);
        free(copy);
        if (!dlg) {
            fwprintf(stderr, L"error: dialog %u could not be created\n", (unsigned) ids[i]);
            return 3;
        }
        place_offscreen(dlg);
        ShowWindow(dlg, SW_SHOWNOACTIVATE);
        pump(300);

        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%ls\\dialog-%u.%ls", outdir, (unsigned) ids[i],
                   as_bmp ? L"bmp" : L"png");
        path[MAX_PATH - 1] = L'\0';
        if (!finish(dlg, path, as_bmp))
            failed = 1;
        DestroyWindow(dlg);
    }
    return failed;
}

int wmain(int argc, wchar_t **argv) {
    int as_bmp = 0, dialogs = 0, a = 1;
    const wchar_t *caption = L"Preferences";
    for (; a < argc && wcsncmp(argv[a], L"--", 2) == 0; a++) {
        if (wcscmp(argv[a], L"--bmp") == 0) {
            as_bmp = 1;
        } else if (wcscmp(argv[a], L"--dialogs") == 0) {
            dialogs = 1;
        } else if (wcscmp(argv[a], L"--caption") == 0 && a + 1 < argc) {
            caption = argv[++a];
        } else {
            usage();
            return 2;
        }
    }
    if (argc - a < 2) {
        usage();
        return 2;
    }
    const wchar_t *outdir = argv[a++];
    int n = argc - a;
    if (n > MAX_TEMPLATES) {
        fwprintf(stderr, L"error: at most %d templates at a time\n", MAX_TEMPLATES);
        return 2;
    }

    WORD ids[MAX_TEMPLATES];
    for (int i = 0; i < n; i++) {
        wchar_t *end = NULL;
        long id = wcstol(argv[a + i], &end, 10);
        if (!end || *end || id <= 0 || id > 0xFFFF ||
            !FindResourceW(NULL, MAKEINTRESOURCEW((WORD) id), (LPCWSTR) RT_DIALOG)) {
            fwprintf(stderr, L"error: %ls is not a dialog template in npad's resources\n",
                     argv[a + i]);
            return 2;
        }
        ids[i] = (WORD) id;
    }
    if (!CreateDirectoryW(outdir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fwprintf(stderr, L"error: cannot create %ls\n", outdir);
        return 1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    int com = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    // Fails only when this process may not change the foreground, in which
    // case its windows cannot take it either
    BOOL locked = LockSetForegroundWindow(LSFW_LOCK);

    int rc = dialogs ? preview_dialogs(outdir, ids, n, as_bmp)
                     : preview_sheet(outdir, ids, n, caption, as_bmp);
    if (locked)
        LockSetForegroundWindow(LSFW_UNLOCK);
    if (com)
        CoUninitialize();
    return rc;
}
