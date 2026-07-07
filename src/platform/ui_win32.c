/*
 * npad - Win32 UI Implementation
 * Windows-specific UI implementation using Win32 API
 *
 * Text handling is Unicode throughout: the core passes UTF-8 strings and
 * this layer converts to/from UTF-16 at the Win32 boundary.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>

#include "../ui_interface.h"
#include "../main.h"
#include "../core/error.h"
#include "../core/settings.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants missing from older SDKs
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

// Window class name
#define NPAD_WINDOW_CLASS L"NpadMainWindow"

// Auto-save timer
#define NPAD_AUTO_SAVE_TIMER_ID 1

// Control IDs
#define ID_EDIT_CONTROL 1001
#define ID_STATUS_BAR 1002
#define ID_FILE_NEW 2001
#define ID_FILE_OPEN 2002
#define ID_FILE_SAVE 2003
#define ID_FILE_SAVE_AS 2004
#define ID_FILE_EXIT 2005
#define ID_FILE_RECENT_CLEAR 2006
#define ID_FILE_RECENT_BASE 2010 // 2010..2019 reserved for recent files
#define ID_EDIT_UNDO 2101
#define ID_EDIT_REDO 2102
#define ID_EDIT_CUT 2103
#define ID_EDIT_COPY 2104
#define ID_EDIT_PASTE 2105
#define ID_EDIT_SELECT_ALL 2106
#define ID_EDIT_FIND 2107
#define ID_EDIT_REPLACE 2108
#define ID_EDIT_GOTO_LINE 2109
#define ID_EDIT_DELETE 2110
#define ID_EDIT_FIND_NEXT 2111
#define ID_EDIT_FIND_PREV 2112
#define ID_EDIT_TIME_DATE 2113
#define ID_FORMAT_WORD_WRAP 2201
#define ID_FORMAT_FONT 2202
#define ID_VIEW_STATUS_BAR 2301
#define ID_VIEW_DARK_MODE 2302
#define ID_VIEW_ZOOM_IN 2303
#define ID_VIEW_ZOOM_OUT 2304
#define ID_VIEW_ZOOM_RESET 2305
#define ID_HELP_ABOUT 2401

#define MAX_RECENT_MENU_FILES 10

// Window structure
typedef struct Window {
    HWND hwnd;
    HWND edit_hwnd;
    HWND status_hwnd;
    HMENU hmenu;
    HMENU recent_menu;
    HACCEL haccel;
    bool is_modified;
    bool setting_text_programmatically; // Suppress EN_CHANGE during programmatic changes
    bool word_wrap_enabled;
    bool status_bar_visible;
    char status_encoding[32];    // e.g. "UTF-8" (set by the editor core)
    char status_line_ending[32]; // e.g. "Windows (CRLF)"
} Window;

// Global variables
static HINSTANCE g_hinstance = NULL;
static bool g_dark_mode = false;
static Window *g_main_window = NULL;
static HMODULE g_richedit_lib = NULL;
static const wchar_t *g_richedit_class = MSFTEDIT_CLASS;

// Modeless find/replace dialog (only one can be open at a time, like Notepad)
static HWND g_find_dialog = NULL;

// Last search parameters shared by Find, Replace and F3
static wchar_t g_search_text[256] = L"";
static wchar_t g_replace_text[256] = L"";
static bool g_match_case = false;
static bool g_whole_word = false;
static bool g_search_down = true;

// Optional per-monitor DPI function (Windows 10+)
typedef UINT(WINAPI *GetDpiForWindowFunc)(HWND);
static GetDpiForWindowFunc g_GetDpiForWindow = NULL;

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window *window);
static void handle_command(Window *window, WORD command);
static void update_status_bar(Window *window);
static void resize_controls(Window *window);
static bool register_window_class(void);
static void apply_theme(Window *window);
static void apply_font(Window *window);
static void apply_word_wrap(Window *window);
static void update_menu_states(Window *window);
static void rebuild_recent_menu(Window *window);
static void show_font_dialog(Window *window);
static void show_goto_dialog(Window *window);
static void set_zoom(Window *window, int percent);
static int get_zoom(Window *window);
static bool find_next(Window *window, bool down);

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 helpers
// ---------------------------------------------------------------------------

static wchar_t *utf8_to_wide(const char *utf8) {
    if (!utf8)
        return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0)
        return NULL;
    wchar_t *wide = malloc((size_t) len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}

static char *wide_to_utf8(const wchar_t *wide) {
    if (!wide)
        return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return NULL;
    char *utf8 = malloc((size_t) len);
    if (!utf8)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, len, NULL, NULL);
    return utf8;
}

static UINT get_window_dpi(HWND hwnd) {
    if (hwnd && g_GetDpiForWindow) {
        return g_GetDpiForWindow(hwnd);
    }
    HDC hdc = GetDC(NULL);
    UINT dpi = (UINT) GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

// ---------------------------------------------------------------------------
// Platform lifecycle
// ---------------------------------------------------------------------------

bool ui_platform_init(void) {
    g_hinstance = GetModuleHandleW(NULL);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&icex)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                         "Failed to initialize common controls");
        return false;
    }

    // Prefer RichEdit 4.1 (msftedit.dll); fall back to RichEdit 2.0
    g_richedit_lib = LoadLibraryW(L"msftedit.dll");
    if (g_richedit_lib) {
        g_richedit_class = MSFTEDIT_CLASS;
    } else {
        g_richedit_lib = LoadLibraryW(L"riched20.dll");
        if (g_richedit_lib) {
            g_richedit_class = L"RichEdit20W";
        } else {
            NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                             "Failed to load a Rich Edit library");
            return false;
        }
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        union {
            FARPROC proc;
            GetDpiForWindowFunc func;
        } dpi_fn;
        dpi_fn.proc = GetProcAddress(user32, "GetDpiForWindow");
        g_GetDpiForWindow = dpi_fn.func;
    }

    if (!register_window_class()) {
        return false;
    }

    // Theme: "system" (default) follows the OS setting; "light"/"dark" override
    char *theme = settings_get_string("theme", "system");
    if (theme && strcmp(theme, "dark") == 0) {
        g_dark_mode = true;
    } else if (theme && strcmp(theme, "light") == 0) {
        g_dark_mode = false;
    } else {
        HKEY hkey;
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                          KEY_READ, &hkey) == ERROR_SUCCESS) {
            DWORD type = REG_DWORD;
            if (RegQueryValueExW(hkey, L"AppsUseLightTheme", NULL, &type, (LPBYTE) &value, &size) ==
                    ERROR_SUCCESS &&
                type == REG_DWORD && size == sizeof(DWORD)) {
                g_dark_mode = (value == 0);
            }
            RegCloseKey(hkey);
        }
    }
    free(theme);

    return true;
}

void ui_platform_cleanup(void) {
    if (g_find_dialog) {
        DestroyWindow(g_find_dialog);
        g_find_dialog = NULL;
    }

    if (g_main_window) {
        if (g_main_window->haccel) {
            DestroyAcceleratorTable(g_main_window->haccel);
        }
        if (g_main_window->hwnd) {
            DestroyWindow(g_main_window->hwnd);
        }
        free(g_main_window);
        g_main_window = NULL;
    }

    if (g_richedit_lib) {
        FreeLibrary(g_richedit_lib);
        g_richedit_lib = NULL;
    }
}

int ui_platform_message_loop(void) {
    MSG msg;
    BOOL result;

    while ((result = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (result == -1) {
            NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "message loop",
                             "GetMessage failed");
            return 1;
        }

        // Keyboard navigation inside the modeless find/replace dialog
        if (g_find_dialog && IsDialogMessageW(g_find_dialog, &msg)) {
            continue;
        }

        // Accelerators apply only while the main window is active so they
        // do not steal keystrokes from dialogs
        if (g_main_window && g_main_window->haccel && GetActiveWindow() == g_main_window->hwnd &&
            TranslateAcceleratorW(g_main_window->hwnd, g_main_window->haccel, &msg)) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int) msg.wParam;
}

void ui_platform_quit(void) {
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------

Window *ui_platform_create_main_window(void) {
    Window *window = malloc(sizeof(Window));
    if (!window)
        return NULL;

    memset(window, 0, sizeof(Window));
    snprintf(window->status_encoding, sizeof(window->status_encoding), "UTF-8");
    snprintf(window->status_line_ending, sizeof(window->status_line_ending), "Windows (CRLF)");

    window->word_wrap_enabled = settings_get_bool("word_wrap", false);
    window->status_bar_visible = settings_get_bool("status_bar_visible", true);

    window->hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, NPAD_WINDOW_CLASS, L"npad",
                                   WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                   CW_USEDEFAULT, 800, 600, NULL, NULL, g_hinstance, window);

    if (!window->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Window creation",
                         "Failed to create main window");
        free(window);
        return NULL;
    }

    HICON icon = LoadIconW(g_hinstance, MAKEINTRESOURCEW(IDI_NPAD));
    if (icon) {
        SendMessageW(window->hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
        SendMessageW(window->hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);
    }

    DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                       ES_NOHIDESEL | ES_WANTRETURN;
    if (!window->word_wrap_enabled) {
        edit_style |= WS_HSCROLL | ES_AUTOHSCROLL;
    }

    window->edit_hwnd =
        CreateWindowExW(0, g_richedit_class, L"", edit_style, 0, 0, 0, 0, window->hwnd,
                        (HMENU) (INT_PTR) ID_EDIT_CONTROL, g_hinstance, NULL);

    if (!window->edit_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Edit control creation",
                         "Failed to create edit control");
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    // Plain text mode, effectively unlimited length, small margins
    SendMessageW(window->edit_hwnd, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessageW(window->edit_hwnd, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(window->edit_hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(4, 4));
    SendMessageW(window->edit_hwnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

    apply_font(window);
    apply_word_wrap(window);

    window->status_hwnd =
        CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | SBARS_SIZEGRIP, 0, 0, 0, 0,
                        window->hwnd, (HMENU) (INT_PTR) ID_STATUS_BAR, g_hinstance, NULL);

    if (!window->status_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Status bar creation",
                         "Failed to create status bar");
        DestroyWindow(window->edit_hwnd);
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    if (window->status_bar_visible) {
        ShowWindow(window->status_hwnd, SW_SHOW);
    }

    create_menu(window);

    ACCEL accel[] = { { FCONTROL | FVIRTKEY, 'N', ID_FILE_NEW },
                      { FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN },
                      { FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE },
                      { FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_FILE_SAVE_AS },
                      { FCONTROL | FVIRTKEY, 'Z', ID_EDIT_UNDO },
                      { FCONTROL | FVIRTKEY, 'Y', ID_EDIT_REDO },
                      { FCONTROL | FVIRTKEY, 'X', ID_EDIT_CUT },
                      { FCONTROL | FVIRTKEY, 'C', ID_EDIT_COPY },
                      { FCONTROL | FVIRTKEY, 'V', ID_EDIT_PASTE },
                      { FCONTROL | FVIRTKEY, 'A', ID_EDIT_SELECT_ALL },
                      { FCONTROL | FVIRTKEY, 'F', ID_EDIT_FIND },
                      { FVIRTKEY, VK_F3, ID_EDIT_FIND_NEXT },
                      { FSHIFT | FVIRTKEY, VK_F3, ID_EDIT_FIND_PREV },
                      { FCONTROL | FVIRTKEY, 'H', ID_EDIT_REPLACE },
                      { FCONTROL | FVIRTKEY, 'G', ID_EDIT_GOTO_LINE },
                      { FVIRTKEY, VK_F5, ID_EDIT_TIME_DATE },
                      { FALT | FVIRTKEY, 'Z', ID_FORMAT_WORD_WRAP },
                      { FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_VIEW_ZOOM_IN },
                      { FCONTROL | FVIRTKEY, VK_ADD, ID_VIEW_ZOOM_IN },
                      { FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_VIEW_ZOOM_OUT },
                      { FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_VIEW_ZOOM_OUT },
                      { FCONTROL | FVIRTKEY, '0', ID_VIEW_ZOOM_RESET },
                      { FCONTROL | FVIRTKEY, VK_NUMPAD0, ID_VIEW_ZOOM_RESET } };
    window->haccel = CreateAcceleratorTableW(accel, sizeof(accel) / sizeof(accel[0]));

    if (!window->haccel) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "Accelerator table creation",
                           "Failed to create accelerator table - keyboard shortcuts will not work");
    }

    apply_theme(window);

    g_main_window = window;
    return window;
}

void ui_platform_destroy_window(Window *window) {
    if (!window)
        return;

    if (window->haccel) {
        DestroyAcceleratorTable(window->haccel);
        window->haccel = NULL;
    }

    if (window->hwnd) {
        DestroyWindow(window->hwnd);
        window->hwnd = NULL;
    }

    if (window == g_main_window) {
        g_main_window = NULL;
    }

    free(window);
}

void ui_platform_show_window(Window *window) {
    if (window && window->hwnd) {
        ShowWindow(window->hwnd, SW_SHOW);
        UpdateWindow(window->hwnd);
    }
}

void ui_platform_hide_window(Window *window) {
    if (window && window->hwnd) {
        ShowWindow(window->hwnd, SW_HIDE);
    }
}

void ui_platform_set_window_title(Window *window, const char *title) {
    if (window && window->hwnd && title) {
        wchar_t *wide = utf8_to_wide(title);
        if (wide) {
            SetWindowTextW(window->hwnd, wide);
            free(wide);
        }
    }
}

// Size/position getters use the *restored* geometry (WINDOWPLACEMENT) so a
// maximized window persists its normal size, not the maximized rect.
void ui_platform_get_window_size(Window *window, int *width, int *height) {
    if (window && window->hwnd && width && height) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (GetWindowPlacement(window->hwnd, &wp)) {
            *width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            *height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        } else {
            *width = 0;
            *height = 0;
        }
    }
}

void ui_platform_set_window_size(Window *window, int width, int height) {
    if (window && window->hwnd && width > 0 && height > 0) {
        SetWindowPos(window->hwnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    }
}

void ui_platform_get_window_position(Window *window, int *x, int *y) {
    if (window && window->hwnd && x && y) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (GetWindowPlacement(window->hwnd, &wp)) {
            *x = wp.rcNormalPosition.left;
            *y = wp.rcNormalPosition.top;
        } else {
            *x = 0;
            *y = 0;
        }
    }
}

void ui_platform_set_window_position(Window *window, int x, int y) {
    if (window && window->hwnd) {
        SetWindowPos(window->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // If the restored position is not on any monitor (e.g. a display was
        // disconnected since last run), fall back to the default position
        if (MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONULL) == NULL) {
            SetWindowPos(window->hwnd, NULL, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
}

void ui_platform_set_window_maximized(Window *window, bool maximized) {
    if (window && window->hwnd) {
        ShowWindow(window->hwnd, maximized ? SW_MAXIMIZE : SW_RESTORE);
    }
}

bool ui_platform_is_window_maximized(Window *window) {
    if (!window || !window->hwnd)
        return false;
    return IsZoomed(window->hwnd) != 0;
}

// ---------------------------------------------------------------------------
// Text access
// ---------------------------------------------------------------------------

void ui_platform_set_text(Window *window, const char *text) {
    if (!window || !window->edit_hwnd || !text)
        return;

    wchar_t *wide = utf8_to_wide(text);
    if (!wide)
        return;

    window->setting_text_programmatically = true;
    SetWindowTextW(window->edit_hwnd, wide);
    window->setting_text_programmatically = false;
    free(wide);

    // Loading new content resets modification state, undo history and caret
    SendMessageW(window->edit_hwnd, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(window->edit_hwnd, EM_SETMODIFY, FALSE, 0);
    SendMessageW(window->edit_hwnd, EM_SETSEL, 0, 0);
    window->is_modified = false;
    update_status_bar(window);
}

char *ui_platform_get_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_USECRLF | GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200; // UTF-16
    LRESULT length = SendMessageW(window->edit_hwnd, EM_GETTEXTLENGTHEX, (WPARAM) &gtl, 0);
    if (length < 0) {
        return NULL;
    }

    wchar_t *wide = malloc(((size_t) length + 1) * sizeof(wchar_t));
    if (!wide) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, 0, "Text retrieval",
                         "Failed to allocate memory for text buffer");
        return NULL;
    }

    GETTEXTEX gt;
    memset(&gt, 0, sizeof(gt));
    gt.cb = (DWORD) (((size_t) length + 1) * sizeof(wchar_t));
    gt.flags = GT_USECRLF;
    gt.codepage = 1200;
    SendMessageW(window->edit_hwnd, EM_GETTEXTEX, (WPARAM) &gt, (LPARAM) wide);
    wide[length] = L'\0';

    char *utf8 = wide_to_utf8(wide);
    free(wide);
    return utf8;
}

void ui_platform_clear_text(Window *window) {
    ui_platform_set_text(window, "");
}

bool ui_platform_has_selection(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;

    CHARRANGE range = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &range);
    return range.cpMin != range.cpMax;
}

char *ui_platform_get_selected_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    CHARRANGE range = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &range);
    if (range.cpMin == range.cpMax)
        return NULL;

    LONG length = range.cpMax - range.cpMin;
    wchar_t *wide = malloc(((size_t) length + 1) * sizeof(wchar_t));
    if (!wide) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, 0, "Selected text retrieval",
                         "Failed to allocate memory for selected text");
        return NULL;
    }

    LRESULT copied = SendMessageW(window->edit_hwnd, EM_GETSELTEXT, 0, (LPARAM) wide);
    wide[copied] = L'\0';

    char *utf8 = wide_to_utf8(wide);
    free(wide);
    return utf8;
}

void ui_platform_select_all(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, EM_SETSEL, 0, -1);
    }
}

void ui_platform_set_cursor_position(Window *window, int position) {
    if (window && window->edit_hwnd && position >= 0) {
        CHARRANGE range = { position, position };
        SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &range);
        SendMessageW(window->edit_hwnd, EM_SCROLLCARET, 0, 0);
        update_status_bar(window);
    }
}

int ui_platform_get_cursor_position(Window *window) {
    if (!window || !window->edit_hwnd)
        return 0;

    CHARRANGE range = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &range);
    return (int) range.cpMin;
}

// ---------------------------------------------------------------------------
// Clipboard and undo
// ---------------------------------------------------------------------------

void ui_platform_cut(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, WM_CUT, 0, 0);
    }
}

void ui_platform_copy(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, WM_COPY, 0, 0);
    }
}

void ui_platform_paste(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, WM_PASTE, 0, 0);
    }
}

void ui_platform_undo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, EM_UNDO, 0, 0);
    }
}

void ui_platform_redo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, EM_REDO, 0, 0);
    }
}

bool ui_platform_can_undo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessageW(window->edit_hwnd, EM_CANUNDO, 0, 0) != 0;
}

bool ui_platform_can_redo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessageW(window->edit_hwnd, EM_CANREDO, 0, 0) != 0;
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

// Convert a '|'-separated filter ("Text Files (*.txt)|*.txt|All Files (*.*)|*.*")
// into the double-NUL-terminated form common dialogs expect.
static wchar_t *build_filter(const char *filter) {
    const char *source =
        (filter && filter[0]) ? filter : "Text Files (*.txt)|*.txt|All Files (*.*)|*.*";
    wchar_t *wide = utf8_to_wide(source);
    if (!wide)
        return NULL;

    size_t len = wcslen(wide);
    wchar_t *result = malloc((len + 2) * sizeof(wchar_t));
    if (!result) {
        free(wide);
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        result[i] = (wide[i] == L'|') ? L'\0' : wide[i];
    }
    result[len] = L'\0';
    result[len + 1] = L'\0';
    free(wide);
    return result;
}

static char *show_file_dialog(Window *parent, const FileDialogParams *params, bool save) {
    OPENFILENAMEW ofn;
    wchar_t filename[MAX_PATH] = L"";

    if (save && params && params->default_filename && params->default_filename[0]) {
        wchar_t *wide = utf8_to_wide(params->default_filename);
        if (wide) {
            wcsncpy(filename, wide, MAX_PATH - 1);
            filename[MAX_PATH - 1] = L'\0';
            free(wide);
        }
    }

    wchar_t *filter = build_filter(params ? params->filter : NULL);
    wchar_t *title = (params && params->title) ? utf8_to_wide(params->title) : NULL;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? parent->hwnd : NULL;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = L"txt";
    if (save) {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    } else {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    }

    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);

    free(filter);
    free(title);

    return ok ? wide_to_utf8(filename) : NULL;
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    return show_file_dialog(parent, params, false);
}

char *ui_platform_show_save_dialog(Window *parent, const FileDialogParams *params) {
    return show_file_dialog(parent, params, true);
}

bool ui_platform_show_message_box(Window *parent, const char *title, const char *message,
                                  bool is_question) {
    HWND hwnd = parent ? parent->hwnd : NULL;
    UINT type = is_question ? (MB_YESNO | MB_ICONQUESTION) : (MB_OK | MB_ICONINFORMATION);

    wchar_t *wtitle = utf8_to_wide(title ? title : "npad");
    wchar_t *wmessage = utf8_to_wide(message ? message : "");
    int result = MessageBoxW(hwnd, wmessage ? wmessage : L"", wtitle ? wtitle : L"npad", type);
    free(wtitle);
    free(wmessage);

    return is_question ? (result == IDYES) : true;
}

SavePromptResult ui_platform_show_save_prompt(Window *parent, const char *filename) {
    HWND hwnd = parent ? parent->hwnd : NULL;

    char message[512];
    snprintf(message, sizeof(message), "Do you want to save changes to %.400s?",
             filename ? filename : "Untitled");

    wchar_t *wmessage = utf8_to_wide(message);
    int result = MessageBoxW(hwnd, wmessage ? wmessage : L"Do you want to save changes?", L"npad",
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    free(wmessage);

    switch (result) {
        case IDYES:
            return UI_SAVE_PROMPT_SAVE;
        case IDNO:
            return UI_SAVE_PROMPT_DISCARD;
        default:
            return UI_SAVE_PROMPT_CANCEL;
    }
}

void ui_platform_show_about_dialog(Window *parent) {
    HWND hwnd = parent ? parent->hwnd : NULL;

    char about_utf8[512];
    snprintf(about_utf8, sizeof(about_utf8),
             "npad %s\n\n"
             "A lightweight, cross-platform text editor\n"
             "inspired by classic Windows Notepad.\n\n"
             "Author: Platima\n"
             "https://github.com/platima/npad",
             NPAD_VERSION);
    wchar_t *message = utf8_to_wide(about_utf8);

    MSGBOXPARAMSW mbp;
    ZeroMemory(&mbp, sizeof(mbp));
    mbp.cbSize = sizeof(mbp);
    mbp.hwndOwner = hwnd;
    mbp.hInstance = g_hinstance;
    mbp.lpszText = message ? message : L"npad";
    mbp.lpszCaption = L"About npad";
    mbp.dwStyle = MB_OK | MB_USERICON;
    mbp.lpszIcon = MAKEINTRESOURCEW(IDI_NPAD);

    MessageBoxIndirectW(&mbp);
    free(message);
}

// ---------------------------------------------------------------------------
// Find / Replace
// ---------------------------------------------------------------------------

// Search from the current selection in the given direction, select the
// match, and report failure like Notepad.
static bool find_next(Window *window, bool down) {
    if (!window || !window->edit_hwnd || g_search_text[0] == L'\0')
        return false;

    CHARRANGE sel = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);

    FINDTEXTEXW ft;
    memset(&ft, 0, sizeof(ft));
    ft.lpstrText = g_search_text;
    if (down) {
        ft.chrg.cpMin = sel.cpMax;
        ft.chrg.cpMax = -1;
    } else {
        ft.chrg.cpMin = sel.cpMin;
        ft.chrg.cpMax = 0;
    }

    WPARAM flags = 0;
    if (down)
        flags |= FR_DOWN;
    if (g_match_case)
        flags |= FR_MATCHCASE;
    if (g_whole_word)
        flags |= FR_WHOLEWORD;

    LRESULT pos = SendMessageW(window->edit_hwnd, EM_FINDTEXTEXW, flags, (LPARAM) &ft);
    if (pos == -1) {
        char search_utf8[512] = "";
        char *converted = wide_to_utf8(g_search_text);
        if (converted) {
            snprintf(search_utf8, sizeof(search_utf8), "%.400s", converted);
            free(converted);
        }
        char message[512];
        snprintf(message, sizeof(message), "Cannot find \"%.400s\"", search_utf8);
        wchar_t *wmessage = utf8_to_wide(message);
        MessageBoxW(g_find_dialog ? g_find_dialog : window->hwnd,
                    wmessage ? wmessage : L"Cannot find the search text", L"npad",
                    MB_OK | MB_ICONINFORMATION);
        free(wmessage);
        return false;
    }

    SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &ft.chrgText);
    SendMessageW(window->edit_hwnd, EM_SCROLLCARET, 0, 0);
    return true;
}

// Read search parameters out of the find/replace dialog controls
static void read_search_fields(HWND dialog, bool has_replace) {
    GetDlgItemTextW(dialog, ID_FIND_TEXT, g_search_text,
                    sizeof(g_search_text) / sizeof(g_search_text[0]));
    if (has_replace) {
        GetDlgItemTextW(dialog, ID_REPLACE_WITH, g_replace_text,
                        sizeof(g_replace_text) / sizeof(g_replace_text[0]));
    }
    g_match_case = IsDlgButtonChecked(dialog, ID_FIND_CASE) == BST_CHECKED;
    g_whole_word = IsDlgButtonChecked(dialog, ID_FIND_WHOLE_WORD) == BST_CHECKED;
    g_search_down = IsDlgButtonChecked(dialog, IDC_RADIO_UP) != BST_CHECKED;
}

// Is the current selection exactly the current search text?
static bool selection_matches_search(Window *window) {
    char *selected = ui_platform_get_selected_text(window);
    if (!selected)
        return false;

    wchar_t *wide = utf8_to_wide(selected);
    free(selected);
    if (!wide)
        return false;

    bool matches;
    if (g_match_case) {
        matches = wcscmp(wide, g_search_text) == 0;
    } else {
        matches = _wcsicmp(wide, g_search_text) == 0;
    }
    free(wide);
    return matches;
}

static void replace_current(Window *window) {
    if (selection_matches_search(window)) {
        SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) g_replace_text);
    }
    find_next(window, g_search_down);
}

static void replace_all(Window *window) {
    if (!window || !window->edit_hwnd || g_search_text[0] == L'\0')
        return;

    WPARAM flags = FR_DOWN;
    if (g_match_case)
        flags |= FR_MATCHCASE;
    if (g_whole_word)
        flags |= FR_WHOLEWORD;

    int replaced = 0;
    LONG start = 0;

    for (;;) {
        FINDTEXTEXW ft;
        memset(&ft, 0, sizeof(ft));
        ft.lpstrText = g_search_text;
        ft.chrg.cpMin = start;
        ft.chrg.cpMax = -1;

        LRESULT pos = SendMessageW(window->edit_hwnd, EM_FINDTEXTEXW, flags, (LPARAM) &ft);
        if (pos == -1)
            break;

        SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &ft.chrgText);
        SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) g_replace_text);
        start = ft.chrgText.cpMin + (LONG) wcslen(g_replace_text);
        replaced++;
    }

    char message[128];
    if (replaced > 0) {
        snprintf(message, sizeof(message), "Replaced %d occurrence%s", replaced,
                 replaced == 1 ? "" : "s");
    } else {
        snprintf(message, sizeof(message), "Cannot find the search text");
    }
    wchar_t *wmessage = utf8_to_wide(message);
    if (wmessage) {
        SendMessageW(window->status_hwnd, SB_SETTEXTW, 0, (LPARAM) wmessage);
        free(wmessage);
    }
}

static INT_PTR CALLBACK find_replace_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam) {
    bool has_replace = GetDlgItem(dialog, ID_REPLACE_WITH) != NULL;
    Window *window = (Window *) GetWindowLongPtrW(dialog, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dialog, GWLP_USERDATA, lparam);
            SetDlgItemTextW(dialog, ID_FIND_TEXT, g_search_text);
            if (GetDlgItem(dialog, ID_REPLACE_WITH)) {
                SetDlgItemTextW(dialog, ID_REPLACE_WITH, g_replace_text);
            }
            CheckDlgButton(dialog, ID_FIND_CASE, g_match_case ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, ID_FIND_WHOLE_WORD, g_whole_word ? BST_CHECKED : BST_UNCHECKED);
            CheckRadioButton(dialog, IDC_RADIO_UP, IDC_RADIO_DOWN,
                             g_search_down ? IDC_RADIO_DOWN : IDC_RADIO_UP);
            SendDlgItemMessageW(dialog, ID_FIND_TEXT, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, ID_FIND_TEXT));
            return FALSE; // Focus set manually
        }

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case ID_FIND_NEXT:
                    read_search_fields(dialog, has_replace);
                    if (window)
                        find_next(window, g_search_down);
                    return TRUE;

                case ID_REPLACE_NEXT:
                    read_search_fields(dialog, has_replace);
                    if (window)
                        replace_current(window);
                    return TRUE;

                case ID_REPLACE_ALL:
                    read_search_fields(dialog, has_replace);
                    if (window)
                        replace_all(window);
                    return TRUE;

                case ID_FIND_CANCEL:
                case IDCANCEL:
                    DestroyWindow(dialog);
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(dialog);
            return TRUE;

        case WM_DESTROY:
            if (g_find_dialog == dialog) {
                g_find_dialog = NULL;
            }
            return FALSE;
    }
    return FALSE;
}

static void show_find_replace_dialog(Window *parent, bool replace) {
    if (!parent)
        return;

    // Only one find/replace dialog at a time, like Notepad
    if (g_find_dialog) {
        DestroyWindow(g_find_dialog);
        g_find_dialog = NULL;
    }

    g_find_dialog =
        CreateDialogParamW(g_hinstance, MAKEINTRESOURCEW(replace ? IDD_REPLACE : IDD_FIND),
                           parent->hwnd, find_replace_proc, (LPARAM) parent);
    if (g_find_dialog) {
        ShowWindow(g_find_dialog, SW_SHOW);
    } else {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Find dialog creation",
                         "Failed to create find/replace dialog");
    }
}

void ui_platform_show_find_dialog(Window *parent) {
    show_find_replace_dialog(parent, false);
}

void ui_platform_show_replace_dialog(Window *parent) {
    show_find_replace_dialog(parent, true);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

typedef HRESULT(WINAPI *DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);

static void set_title_bar_dark(HWND hwnd, bool dark) {
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi)
        return;

    union {
        FARPROC proc;
        DwmSetWindowAttributeFunc func;
    } fn;
    fn.proc = GetProcAddress(dwmapi, "DwmSetWindowAttribute");

    if (fn.func) {
        BOOL value = dark ? TRUE : FALSE;
        // Attribute 20 on current builds, 19 on early Windows 10 1809
        if (FAILED(fn.func(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value)))) {
            fn.func(hwnd, 19, &value, sizeof(value));
        }
    }
    FreeLibrary(dwmapi);
}

static void apply_theme(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    COLORREF back = g_dark_mode ? RGB(30, 30, 30) : GetSysColor(COLOR_WINDOW);
    COLORREF text = g_dark_mode ? RGB(220, 220, 220) : GetSysColor(COLOR_WINDOWTEXT);

    // Preserve document state while re-styling
    LRESULT was_modified = SendMessageW(window->edit_hwnd, EM_GETMODIFY, 0, 0);
    window->setting_text_programmatically = true;

    SendMessageW(window->edit_hwnd, EM_SETBKGNDCOLOR, 0, (LPARAM) back);

    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = text;
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf);
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf);

    window->setting_text_programmatically = false;
    SendMessageW(window->edit_hwnd, EM_SETMODIFY, (WPARAM) was_modified, 0);

    if (window->status_hwnd) {
        SendMessageW(window->status_hwnd, SB_SETBKCOLOR, 0,
                     g_dark_mode ? (LPARAM) RGB(45, 45, 45) : (LPARAM) CLR_DEFAULT);
    }

    if (window->hwnd) {
        set_title_bar_dark(window->hwnd, g_dark_mode);
    }

    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_VIEW_DARK_MODE, g_dark_mode ? MF_CHECKED : MF_UNCHECKED);
    }
}

void ui_platform_set_dark_mode(bool enabled) {
    g_dark_mode = enabled;
    if (g_main_window) {
        apply_theme(g_main_window);
    }
}

bool ui_platform_is_dark_mode(void) {
    return g_dark_mode;
}

bool ui_platform_system_supports_dark_mode(void) {
    return true; // Windows 10 1809+
}

// ---------------------------------------------------------------------------
// Status bar and state queries
// ---------------------------------------------------------------------------

bool ui_platform_is_text_modified(Window *window) {
    return window ? window->is_modified : false;
}

void ui_platform_set_text_modified(Window *window, bool modified) {
    if (window) {
        window->is_modified = modified;
        if (window->edit_hwnd) {
            SendMessageW(window->edit_hwnd, EM_SETMODIFY, modified ? TRUE : FALSE, 0);
        }
        update_status_bar(window);
    }
}

int ui_platform_get_line_count(Window *window) {
    if (!window || !window->edit_hwnd)
        return 0;
    return (int) SendMessageW(window->edit_hwnd, EM_GETLINECOUNT, 0, 0);
}

void ui_platform_get_cursor_line_column(Window *window, int *line, int *column) {
    if (line)
        *line = 0;
    if (column)
        *column = 0;
    if (!window || !window->edit_hwnd || !line || !column)
        return;

    int pos = ui_platform_get_cursor_position(window);
    *line = (int) SendMessageW(window->edit_hwnd, EM_EXLINEFROMCHAR, 0, pos) + 1;
    int line_start = (int) SendMessageW(window->edit_hwnd, EM_LINEINDEX, *line - 1, 0);
    *column = pos - line_start + 1;
}

void ui_platform_set_status_info(Window *window, const char *encoding_name, const char *eol_name) {
    if (!window)
        return;
    if (encoding_name) {
        snprintf(window->status_encoding, sizeof(window->status_encoding), "%s", encoding_name);
    }
    if (eol_name) {
        snprintf(window->status_line_ending, sizeof(window->status_line_ending), "%s", eol_name);
    }
    update_status_bar(window);
}

void ui_platform_set_auto_save_timer(Window *window, int seconds) {
    if (!window || !window->hwnd)
        return;

    KillTimer(window->hwnd, NPAD_AUTO_SAVE_TIMER_ID);
    if (seconds > 0) {
        SetTimer(window->hwnd, NPAD_AUTO_SAVE_TIMER_ID, (UINT) seconds * 1000, NULL);
    }
}

void *ui_platform_get_native_handle(Window *window) {
    return window ? window->hwnd : NULL;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool register_window_class(void) {
    WNDCLASSEXW wc;

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinstance;
    wc.hIcon = LoadIconW(g_hinstance, MAKEINTRESOURCEW(IDI_NPAD));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NPAD_WINDOW_CLASS;
    wc.hIconSm = LoadIconW(g_hinstance, MAKEINTRESOURCEW(IDI_NPAD));

    if (RegisterClassExW(&wc) == 0) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "Window class registration",
                         "Failed to register window class");
        return false;
    }
    return true;
}

static int get_zoom(Window *window) {
    WPARAM numerator = 0;
    LPARAM denominator = 0;
    SendMessageW(window->edit_hwnd, EM_GETZOOM, (WPARAM) &numerator, (LPARAM) &denominator);
    // EM_GETZOOM writes through the pointers; 0/0 means default (100%)
    // cppcheck-suppress knownConditionTrueFalse
    if (numerator == 0 || denominator == 0) {
        return 100;
    }
    return (int) ((numerator * 100) / denominator);
}

static void set_zoom(Window *window, int percent) {
    if (percent < 10)
        percent = 10;
    if (percent > 500)
        percent = 500;
    SendMessageW(window->edit_hwnd, EM_SETZOOM, (WPARAM) percent, 100);
    update_status_bar(window);
}

static void apply_word_wrap(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    // EM_SETTARGETDEVICE with line width 0 wraps to the window; 1 disables wrap
    SendMessageW(window->edit_hwnd, EM_SETTARGETDEVICE, 0, window->word_wrap_enabled ? 0 : 1);

    // Toggle the horizontal scrollbar to match
    LONG_PTR style = GetWindowLongPtrW(window->edit_hwnd, GWL_STYLE);
    if (window->word_wrap_enabled) {
        style &= ~((LONG_PTR) (WS_HSCROLL | ES_AUTOHSCROLL));
    } else {
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    }
    SetWindowLongPtrW(window->edit_hwnd, GWL_STYLE, style);
    SetWindowPos(window->edit_hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_FORMAT_WORD_WRAP,
                      window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
    }
}

// Apply the persisted font (default: Consolas 11pt, matching Notepad's
// monospace look) to the whole document and as the insertion default.
static void apply_font(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    char *face = settings_get_string("font_face", "Consolas");
    int size = settings_get_int("font_size", 11);
    int weight = settings_get_int("font_weight", FW_NORMAL);
    bool italic = settings_get_bool("font_italic", false);

    if (size < 6)
        size = 6;
    if (size > 72)
        size = 72;

    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT | CFM_ITALIC | CFM_COLOR;
    cf.yHeight = size * 20; // Twips
    cf.wWeight = (WORD) weight;
    cf.dwEffects = italic ? CFE_ITALIC : 0;
    cf.crTextColor = g_dark_mode ? RGB(220, 220, 220) : GetSysColor(COLOR_WINDOWTEXT);

    wchar_t *wide_face = utf8_to_wide(face ? face : "Consolas");
    if (wide_face) {
        wcsncpy(cf.szFaceName, wide_face, LF_FACESIZE - 1);
        cf.szFaceName[LF_FACESIZE - 1] = L'\0';
        free(wide_face);
    }
    free(face);

    LRESULT was_modified = SendMessageW(window->edit_hwnd, EM_GETMODIFY, 0, 0);
    window->setting_text_programmatically = true;
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf);
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf);
    window->setting_text_programmatically = false;
    SendMessageW(window->edit_hwnd, EM_SETMODIFY, (WPARAM) was_modified, 0);
}

static void show_font_dialog(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    char *face = settings_get_string("font_face", "Consolas");
    int size = settings_get_int("font_size", 11);
    int weight = settings_get_int("font_weight", FW_NORMAL);
    bool italic = settings_get_bool("font_italic", false);

    HDC hdc = GetDC(window->edit_hwnd);
    int log_pixels_y = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(window->edit_hwnd, hdc);

    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(size, log_pixels_y, 72);
    lf.lfWeight = weight;
    lf.lfItalic = italic ? TRUE : FALSE;
    wchar_t *wide_face = utf8_to_wide(face ? face : "Consolas");
    if (wide_face) {
        wcsncpy(lf.lfFaceName, wide_face, LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        free(wide_face);
    }
    free(face);

    CHOOSEFONTW cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = window->hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
    cf.nFontType = SCREEN_FONTTYPE;

    if (ChooseFontW(&cf)) {
        char *new_face = wide_to_utf8(lf.lfFaceName);
        if (new_face) {
            settings_set_string("font_face", new_face);
            free(new_face);
        }
        settings_set_int("font_size", cf.iPointSize / 10); // iPointSize is tenths of a point
        settings_set_int("font_weight", (int) lf.lfWeight);
        settings_set_bool("font_italic", lf.lfItalic != 0);

        apply_font(window);
    }
}

// Go To Line dialog (resource-based, truly modal)
static INT_PTR CALLBACK goto_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void) lparam;
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemInt(dialog, ID_GOTO_EDIT, 1, FALSE);
            SendDlgItemMessageW(dialog, ID_GOTO_EDIT, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, ID_GOTO_EDIT));
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDOK: {
                    BOOL ok = FALSE;
                    UINT line = GetDlgItemInt(dialog, ID_GOTO_EDIT, &ok, FALSE);
                    EndDialog(dialog, ok ? (INT_PTR) line : 0);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dialog, 0);
                    return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(dialog, 0);
            return TRUE;
    }
    return FALSE;
}

static void show_goto_dialog(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    INT_PTR line =
        DialogBoxParamW(g_hinstance, MAKEINTRESOURCEW(IDD_GOTO), window->hwnd, goto_proc, 0);
    if (line <= 0)
        return;

    LRESULT char_index = SendMessageW(window->edit_hwnd, EM_LINEINDEX, (WPARAM) (line - 1), 0);
    if (char_index < 0) {
        MessageBoxW(window->hwnd, L"The line number is beyond the total number of lines", L"npad",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    CHARRANGE range = { (LONG) char_index, (LONG) char_index };
    SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &range);
    SendMessageW(window->edit_hwnd, EM_SCROLLCARET, 0, 0);
    update_status_bar(window);
}

// Insert the current time and date at the caret, like Notepad's F5
static void insert_time_date(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t time_str[64] = L"";
    wchar_t date_str[64] = L"";
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, time_str, 64);
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date_str, 64);

    wchar_t combined[132];
    _snwprintf(combined, 131, L"%s %s", time_str, date_str);
    combined[131] = L'\0';

    SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) combined);
}

static void create_menu(Window *window) {
    HMENU hmenu = CreateMenu();
    HMENU hfile = CreatePopupMenu();
    HMENU hrecent = CreatePopupMenu();
    HMENU hedit = CreatePopupMenu();
    HMENU hformat = CreatePopupMenu();
    HMENU hview = CreatePopupMenu();
    HMENU hzoom = CreatePopupMenu();
    HMENU hhelp = CreatePopupMenu();

    if (!hmenu || !hfile || !hrecent || !hedit || !hformat || !hview || !hzoom || !hhelp) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu creation",
                         "Failed to create menu components");
        return;
    }

    // File menu
    AppendMenuW(hfile, MF_STRING, ID_FILE_NEW, L"&New\tCtrl+N");
    AppendMenuW(hfile, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hfile, MF_STRING, ID_FILE_SAVE_AS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING | MF_POPUP, (UINT_PTR) hrecent, L"&Recent Files");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING, ID_FILE_EXIT, L"E&xit");

    // Edit menu
    AppendMenuW(hedit, MF_STRING, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hedit, MF_STRING, ID_EDIT_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_DELETE, L"De&lete\tDel");
    AppendMenuW(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hedit, MF_STRING, ID_EDIT_FIND, L"&Find...\tCtrl+F");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_FIND_NEXT, L"Find &Next\tF3");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_FIND_PREV, L"Find Pre&vious\tShift+F3");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_REPLACE, L"R&eplace...\tCtrl+H");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_GOTO_LINE, L"&Go To...\tCtrl+G");
    AppendMenuW(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hedit, MF_STRING, ID_EDIT_SELECT_ALL, L"Select &All\tCtrl+A");
    AppendMenuW(hedit, MF_STRING, ID_EDIT_TIME_DATE, L"Time/&Date\tF5");

    // Format menu
    AppendMenuW(hformat, MF_STRING, ID_FORMAT_WORD_WRAP, L"&Word Wrap\tAlt+Z");
    AppendMenuW(hformat, MF_STRING, ID_FORMAT_FONT, L"&Font...");

    // View menu
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_IN, L"Zoom &In\tCtrl+Plus");
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_OUT, L"Zoom &Out\tCtrl+Minus");
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_RESET, L"&Restore Default Zoom\tCtrl+0");
    AppendMenuW(hview, MF_STRING | MF_POPUP, (UINT_PTR) hzoom, L"&Zoom");
    AppendMenuW(hview, MF_STRING, ID_VIEW_STATUS_BAR, L"&Status Bar");
    AppendMenuW(hview, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hview, MF_STRING, ID_VIEW_DARK_MODE, L"&Dark Mode");

    // Help menu
    AppendMenuW(hhelp, MF_STRING, ID_HELP_ABOUT, L"&About npad");

    AppendMenuW(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hfile, L"&File");
    AppendMenuW(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hedit, L"&Edit");
    AppendMenuW(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hformat, L"F&ormat");
    AppendMenuW(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hview, L"&View");
    AppendMenuW(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hhelp, L"&Help");

    if (!SetMenu(window->hwnd, hmenu)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu attachment",
                         "Failed to attach menu to window");
        DestroyMenu(hmenu);
        return;
    }

    window->hmenu = hmenu;
    window->recent_menu = hrecent;

    CheckMenuItem(hmenu, ID_FORMAT_WORD_WRAP,
                  window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hmenu, ID_VIEW_STATUS_BAR,
                  window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
    rebuild_recent_menu(window);
}

// Rebuild the File > Recent Files submenu from settings
static void rebuild_recent_menu(Window *window) {
    if (!window || !window->recent_menu)
        return;

    while (RemoveMenu(window->recent_menu, 0, MF_BYPOSITION)) {
        // Remove all existing items
    }

    int count = 0;
    char **files = settings_get_recent_files(&count);

    if (count == 0) {
        AppendMenuW(window->recent_menu, MF_STRING | MF_GRAYED, 0, L"(Empty)");
    } else {
        for (int i = 0; i < count && i < MAX_RECENT_MENU_FILES; i++) {
            char item[512];
            snprintf(item, sizeof(item), "&%d %.400s", i + 1, files[i]);
            wchar_t *wide = utf8_to_wide(item);
            if (wide) {
                AppendMenuW(window->recent_menu, MF_STRING, ID_FILE_RECENT_BASE + (UINT) i, wide);
                free(wide);
            }
        }
        AppendMenuW(window->recent_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(window->recent_menu, MF_STRING, ID_FILE_RECENT_CLEAR, L"&Clear Recent Files");
    }

    if (files) {
        settings_free_recent_files(files, count);
    }
}

// Enable/disable menu items to match the current state, like Notepad
static void update_menu_states(Window *window) {
    if (!window || !window->hmenu || !window->edit_hwnd)
        return;

    bool has_selection = ui_platform_has_selection(window);
    bool has_text = SendMessageW(window->edit_hwnd, WM_GETTEXTLENGTH, 0, 0) > 0;
    bool can_paste =
        IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT);

    UINT enabled = MF_BYCOMMAND | MF_ENABLED;
    UINT disabled = MF_BYCOMMAND | MF_GRAYED;

    EnableMenuItem(window->hmenu, ID_EDIT_UNDO, ui_platform_can_undo(window) ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_REDO, ui_platform_can_redo(window) ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_CUT, has_selection ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_COPY, has_selection ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_DELETE, has_selection ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_PASTE, can_paste ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_FIND, has_text ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_FIND_NEXT,
                   (has_text && g_search_text[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_FIND_PREV,
                   (has_text && g_search_text[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(window->hmenu, ID_EDIT_REPLACE, has_text ? enabled : disabled);
}

static void handle_command(Window *window, WORD command) {
    // Recent file entries occupy a reserved ID range
    if (command >= ID_FILE_RECENT_BASE && command < ID_FILE_RECENT_BASE + MAX_RECENT_MENU_FILES) {
        int index = command - ID_FILE_RECENT_BASE;
        int count = 0;
        char **files = settings_get_recent_files(&count);
        if (files && index < count) {
            ui_post_event(UI_EVENT_FILE_DROPPED, window, files[index]);
        }
        if (files) {
            settings_free_recent_files(files, count);
        }
        return;
    }

    switch (command) {
        case ID_FILE_NEW:
            ui_post_event(UI_EVENT_FILE_NEW, window, NULL);
            break;
        case ID_FILE_OPEN:
            ui_post_event(UI_EVENT_FILE_OPEN, window, NULL);
            break;
        case ID_FILE_SAVE:
            ui_post_event(UI_EVENT_FILE_SAVE, window, NULL);
            break;
        case ID_FILE_SAVE_AS:
            ui_post_event(UI_EVENT_FILE_SAVE_AS, window, NULL);
            break;
        case ID_FILE_RECENT_CLEAR:
            settings_clear_recent_files();
            rebuild_recent_menu(window);
            break;
        case ID_FILE_EXIT:
            ui_post_event(UI_EVENT_QUIT, window, NULL);
            break;
        case ID_EDIT_UNDO:
            ui_post_event(UI_EVENT_EDIT_UNDO, window, NULL);
            break;
        case ID_EDIT_REDO:
            ui_post_event(UI_EVENT_EDIT_REDO, window, NULL);
            break;
        case ID_EDIT_CUT:
            ui_post_event(UI_EVENT_EDIT_CUT, window, NULL);
            break;
        case ID_EDIT_COPY:
            ui_post_event(UI_EVENT_EDIT_COPY, window, NULL);
            break;
        case ID_EDIT_PASTE:
            ui_post_event(UI_EVENT_EDIT_PASTE, window, NULL);
            break;
        case ID_EDIT_DELETE:
            SendMessageW(window->edit_hwnd, WM_CLEAR, 0, 0);
            break;
        case ID_EDIT_SELECT_ALL:
            ui_post_event(UI_EVENT_EDIT_SELECT_ALL, window, NULL);
            break;
        case ID_EDIT_FIND:
            ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            break;
        case ID_EDIT_FIND_NEXT:
            if (g_search_text[0] != L'\0') {
                find_next(window, true);
            } else {
                ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            }
            break;
        case ID_EDIT_FIND_PREV:
            if (g_search_text[0] != L'\0') {
                find_next(window, false);
            } else {
                ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            }
            break;
        case ID_EDIT_REPLACE:
            ui_post_event(UI_EVENT_EDIT_REPLACE, window, NULL);
            break;
        case ID_EDIT_GOTO_LINE:
            show_goto_dialog(window);
            break;
        case ID_EDIT_TIME_DATE:
            insert_time_date(window);
            break;
        case ID_FORMAT_WORD_WRAP:
            window->word_wrap_enabled = !window->word_wrap_enabled;
            settings_set_bool("word_wrap", window->word_wrap_enabled);
            apply_word_wrap(window);
            break;
        case ID_FORMAT_FONT:
            show_font_dialog(window);
            break;
        case ID_VIEW_ZOOM_IN:
            set_zoom(window, get_zoom(window) + 10);
            break;
        case ID_VIEW_ZOOM_OUT:
            set_zoom(window, get_zoom(window) - 10);
            break;
        case ID_VIEW_ZOOM_RESET:
            set_zoom(window, 100);
            break;
        case ID_VIEW_STATUS_BAR:
            window->status_bar_visible = !window->status_bar_visible;
            settings_set_bool("status_bar_visible", window->status_bar_visible);
            ShowWindow(window->status_hwnd, window->status_bar_visible ? SW_SHOW : SW_HIDE);
            resize_controls(window);
            if (window->hmenu) {
                CheckMenuItem(window->hmenu, ID_VIEW_STATUS_BAR,
                              window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
            }
            break;
        case ID_VIEW_DARK_MODE:
            ui_post_event(UI_EVENT_VIEW_TOGGLE_DARK_MODE, window, NULL);
            break;
        case ID_HELP_ABOUT:
            ui_platform_show_about_dialog(window);
            break;
    }
}

static void update_status_bar(Window *window) {
    if (!window || !window->status_hwnd || !window->edit_hwnd || !window->status_bar_visible)
        return;

    int line = 0, column = 0;
    ui_platform_get_cursor_line_column(window, &line, &column);

    wchar_t text[128];

    _snwprintf(text, 127, L"Ln %d, Col %d", line, column);
    text[127] = L'\0';
    SendMessageW(window->status_hwnd, SB_SETTEXTW, 1, (LPARAM) text);

    _snwprintf(text, 127, L"%d%%", get_zoom(window));
    text[127] = L'\0';
    SendMessageW(window->status_hwnd, SB_SETTEXTW, 2, (LPARAM) text);

    wchar_t *eol = utf8_to_wide(window->status_line_ending);
    if (eol) {
        SendMessageW(window->status_hwnd, SB_SETTEXTW, 3, (LPARAM) eol);
        free(eol);
    }

    wchar_t *encoding = utf8_to_wide(window->status_encoding);
    if (encoding) {
        SendMessageW(window->status_hwnd, SB_SETTEXTW, 4, (LPARAM) encoding);
        free(encoding);
    }
}

static void resize_controls(Window *window) {
    if (!window || !window->hwnd || !window->edit_hwnd || !window->status_hwnd)
        return;

    RECT rect;
    GetClientRect(window->hwnd, &rect);

    // Status bar auto-positions itself at the bottom
    SendMessageW(window->status_hwnd, WM_SIZE, 0, 0);

    int status_height = 0;
    if (window->status_bar_visible) {
        RECT status_rect;
        GetWindowRect(window->status_hwnd, &status_rect);
        status_height = status_rect.bottom - status_rect.top;

        // Status bar parts, scaled for DPI: [message][Ln,Col][zoom][EOL][encoding]
        int dpi = (int) get_window_dpi(window->hwnd);
        int width = rect.right;
        int parts[5];
        parts[4] = width;
        parts[3] = width - MulDiv(110, dpi, 96);
        parts[2] = width - MulDiv(230, dpi, 96);
        parts[1] = width - MulDiv(290, dpi, 96);
        parts[0] = width - MulDiv(410, dpi, 96);
        SendMessageW(window->status_hwnd, SB_SETPARTS, 5, (LPARAM) parts);
        update_status_bar(window);
    }

    SetWindowPos(window->edit_hwnd, NULL, 0, 0, rect.right, rect.bottom - status_height,
                 SWP_NOZORDER);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window *window = (Window *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW *) lparam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
            return 0;
        }

        case WM_SIZE: {
            if (window) {
                resize_controls(window);
            }
            return 0;
        }

        case WM_SETFOCUS: {
            if (window && window->edit_hwnd) {
                SetFocus(window->edit_hwnd);
            }
            return 0;
        }

        case WM_INITMENUPOPUP: {
            if (window) {
                update_menu_states(window);
                rebuild_recent_menu(window);
            }
            return 0;
        }

        case WM_COMMAND: {
            if (window) {
                WORD notification = HIWORD(wparam);
                WORD control_id = LOWORD(wparam);

                if (control_id == ID_EDIT_CONTROL) {
                    if (notification == EN_CHANGE && !window->setting_text_programmatically) {
                        window->is_modified = true;
                        ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                    }
                } else {
                    handle_command(window, control_id);
                }
            }
            return 0;
        }

        case WM_NOTIFY: {
            if (window) {
                NMHDR *nmhdr = (NMHDR *) lparam;
                if (nmhdr->idFrom == ID_EDIT_CONTROL && nmhdr->code == EN_SELCHANGE) {
                    update_status_bar(window);
                }
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP) wparam;
            wchar_t path[MAX_PATH];
            if (window && DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
                char *utf8 = wide_to_utf8(path);
                if (utf8) {
                    ui_post_event(UI_EVENT_FILE_DROPPED, window, utf8);
                    free(utf8);
                }
            }
            DragFinish(drop);
            return 0;
        }

        case WM_TIMER: {
            if (window && wparam == NPAD_AUTO_SAVE_TIMER_ID) {
                ui_post_event(UI_EVENT_AUTO_SAVE, window, NULL);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            if (window) {
                RECT *suggested_rect = (RECT *) lparam;
                if (suggested_rect) {
                    SetWindowPos(hwnd, NULL, suggested_rect->left, suggested_rect->top,
                                 suggested_rect->right - suggested_rect->left,
                                 suggested_rect->bottom - suggested_rect->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                apply_font(window);
                resize_controls(window);
            }
            return 0;
        }

        case WM_CLOSE: {
            // Single close path: the editor decides (prompting to save if
            // needed) and calls ui_quit() when the app may exit
            ui_post_event(UI_EVENT_QUIT, window, NULL);
            return 0;
        }

        case WM_DESTROY: {
            if (window == g_main_window) {
                PostQuitMessage(0);
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
