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
#include <prsht.h>
#include <richedit.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlobj.h>

#include "../ui_interface.h"
#include "../main.h"
#include "../core/editor.h"
#include "../core/error.h"
#include "../core/session.h"
#include "../core/settings.h"
#include "../core/startup_prof.h"
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

// Timers
#define NPAD_AUTO_SAVE_TIMER_ID 1
#define NPAD_SESSION_TIMER_ID 2
#define NPAD_STARTUP_TIMER_ID 3
#define NPAD_STATUS_TIMER_ID 4

// Live edit-control counters surfaced on the hidden Debug preferences page
// (scroll/paint performance diagnostics)
static unsigned g_paint_count = 0;
static unsigned g_selchange_count = 0;
static double g_last_paint_ms = 0.0;
static double g_paint_total_ms = 0.0;

static double qpc_ms(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (double) counter.QuadPart * 1000.0 / (double) freq.QuadPart;
}

// Default font faces for Windows. The monospace and proportional defaults
// differ so the Monospace toggle produces a visible change out of the box.
#define DEFAULT_MONO_FONT "Consolas"
#define DEFAULT_PROP_FONT "Segoe UI"
#define OPENDYSLEXIC_FONT "OpenDyslexic"
#define OPENDYSLEXIC_FONT_W L"OpenDyslexic"

// Control IDs
#define ID_EDIT_CONTROL 1001
#define ID_STATUS_BAR 1002
#define ID_FILE_NEW 2001
#define ID_FILE_OPEN 2002
#define ID_FILE_SAVE 2003
#define ID_FILE_SAVE_AS 2004
#define ID_FILE_EXIT 2005
#define ID_FILE_RECENT_CLEAR 2006
#define ID_FILE_PREFERENCES 2007
#define ID_FILE_NEW_WINDOW 2008
#define ID_FILE_CLOSE 2009
#define ID_FILE_RECENT_BASE 2010 // 2010..2019 reserved for recent files
#define ID_FILE_CLOSE_ALL 2020
// Hidden: opens Preferences with the Debug page (Ctrl+Shift+. or
// Shift+click on the Preferences menu item)
#define ID_FILE_PREFERENCES_DEBUG 2021

// Control ids for the modern Save dialog's custom encoding dropdown
#define SAVE_ENC_GROUP_ID 1200
#define SAVE_ENC_COMBO_ID 1201
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
#define ID_FORMAT_MONOSPACE 2203
#define ID_FORMAT_EOL_CRLF 2211 // 2211..2213, order matches the LineEnding enum
#define ID_FORMAT_EOL_LF 2212
#define ID_FORMAT_EOL_CR 2213
#define ID_FORMAT_EOL_CYCLE 2214
#define ID_ENCODING_BASE 2221 // 2221..2225, order matches the TextEncoding enum
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
    bool monospace_current;      // Per-window view state (starts from default_font_mono)
    char status_encoding[32];    // e.g. "UTF-8" (set by the editor core)
    char status_line_ending[32]; // e.g. "Windows (CRLF)"
    bool status_update_pending;  // Coalescing timer armed (see schedule_status_update)
    wchar_t status_cache[6][64]; // Last text sent per status part; skip identical sends
} Window;

// Global variables
static HINSTANCE g_hinstance = NULL;
static bool g_dark_mode = false;
static Window *g_main_window = NULL;
static HMODULE g_richedit_lib = NULL;
static const wchar_t *g_richedit_class = MSFTEDIT_CLASS;

// Registered broadcast message telling other instances that settings on disk
// changed (wParam carries the sender's pid so it can ignore its own message)
static UINT g_settings_changed_msg = 0;

// Registered broadcast message telling every npad window to close itself
// (each runs its own save-prompt; a Cancel keeps that window open)
static UINT g_close_all_msg = 0;

// Registered message carrying live view state (font type + zoom) between
// instances when "Sync view across all instances" is enabled.
// wParam = sender pid; lParam = MAKELPARAM(zoom_percent, monospace ? 1 : 0)
static UINT g_view_sync_msg = 0;

// Modeless find/replace dialog (only one can be open at a time, like Notepad)
static HWND g_find_dialog = NULL;

// Last search parameters shared by Find, Replace and F3 (persisted)
static wchar_t g_search_text[256] = L"";
static wchar_t g_replace_text[256] = L"";
static bool g_match_case = false;
static bool g_whole_word = false;
static bool g_search_down = true;
static bool g_wrap_around = true;

// Last find/replace dialog position, remembered for the session
static POINT g_find_dialog_pos;
static bool g_find_dialog_pos_valid = false;

// Optional per-monitor DPI function (Windows 10+)
typedef UINT(WINAPI *GetDpiForWindowFunc)(HWND);
static GetDpiForWindowFunc g_GetDpiForWindow = NULL;

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window *window);
static void build_accelerators(Window *window);
static void apply_new_window_pref(Window *window);
static void launch_new_window(void);
static void handle_command(Window *window, WORD command);
static void update_status_bar(Window *window);
static void schedule_status_update(Window *window);
static void resize_controls(Window *window);
static bool register_window_class(void);
static void apply_theme(Window *window);
static void apply_font(Window *window);
static void apply_font_default(Window *window);
static void refresh_font_binding(Window *window);
static void apply_word_wrap(Window *window);
static void update_menu_states(Window *window);
static void rebuild_recent_menu(Window *window);
static void show_font_dialog(Window *window, const char *font_key, const char *default_face);
static void show_goto_dialog(Window *window);
static void show_preferences_dialog(const Window *window, bool show_debug);
static void reload_and_apply_settings(Window *window);
static int count_npad_windows(void);
static void show_context_menu(Window *window, int x, int y);
static void show_line_ending_popup(Window *window);
static void show_encoding_popup(Window *window);
static void set_status_bar_visible(Window *window, bool visible);
static void set_zoom(Window *window, int percent);
static int get_zoom(Window *window);
static bool find_next(Window *window, bool down);
static bool font_is_installed(const wchar_t *face);
static void on_view_state_changed(Window *window);
static void broadcast_to_npad_windows(UINT msg, LPARAM lparam);
void ui_platform_notify_settings_changed(void);

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

// Whether apps should use dark mode according to the OS setting
static bool read_system_dark_mode(void) {
    bool dark = false;
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
            dark = (value == 0);
        }
        RegCloseKey(hkey);
    }
    return dark;
}

// Resolve the "theme" setting to editor colors and whether the scheme is
// dark (used for the title-bar decision). Schemes: system, light, dark,
// solarized-light, solarized-dark.
static bool theme_colors(COLORREF *bg, COLORREF *fg) {
    bool dark = false;
    COLORREF back = GetSysColor(COLOR_WINDOW);
    COLORREF text = GetSysColor(COLOR_WINDOWTEXT);

    // Default to Light: classic Notepad has no colour schemes, so a fresh
    // install is plain Light rather than following the OS dark mode.
    char *theme = settings_get_string("theme", "light");
    const char *t = theme ? theme : "light";

    if (strcmp(t, "dark") == 0) {
        dark = true;
        back = RGB(30, 30, 30);
        text = RGB(220, 220, 220);
    } else if (strcmp(t, "light") == 0) {
        dark = false;
        back = RGB(255, 255, 255);
        text = RGB(0, 0, 0);
    } else if (strcmp(t, "solarized-light") == 0) {
        dark = false;
        back = RGB(0xFD, 0xF6, 0xE3); // base3
        text = RGB(0x65, 0x7B, 0x83); // base00
    } else if (strcmp(t, "solarized-dark") == 0) {
        dark = true;
        back = RGB(0x00, 0x2B, 0x36); // base03
        text = RGB(0x83, 0x94, 0x96); // base0
    } else {                          // "system"
        dark = read_system_dark_mode();
        if (dark) {
            back = RGB(30, 30, 30);
            text = RGB(220, 220, 220);
        }
    }
    free(theme);

    if (bg)
        *bg = back;
    if (fg)
        *fg = text;
    return dark;
}

// Resolve the "theme" setting to a dark-mode flag (title bar decision)
static bool resolve_theme_dark_mode(void) {
    return theme_colors(NULL, NULL);
}

// Subclass for the edit control: refresh the status bar after events that
// EN_SELCHANGE does not reliably cover (native Ctrl+wheel zoom, caret
// movement onto a new empty line, mouse-driven caret moves)
static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR id, DWORD_PTR ref) {
    (void) ref;
    LRESULT result;
    if (msg == WM_PAINT) {
        // Time the paint for the Debug page's counters, and mark the first
        // one as the end of the perceived startup time
        double t0 = qpc_ms();
        result = DefSubclassProc(hwnd, msg, wparam, lparam);
        g_last_paint_ms = qpc_ms() - t0;
        g_paint_total_ms += g_last_paint_ms;
        g_paint_count++;
        if (g_paint_count == 1) {
            startup_prof_mark("first paint");
        }
    } else {
        result = DefSubclassProc(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
        case WM_MOUSEWHEEL:
            if (g_main_window) {
                // Coalesced: plain scrolling must stay realtime, so no
                // synchronous Ln/Col recompute per wheel notch
                schedule_status_update(g_main_window);
                // Ctrl+wheel is RichEdit's native zoom: treat it as a
                // user-initiated view-state change (sync / auto-defaults)
                if (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) {
                    on_view_state_changed(g_main_window);
                }
            }
            break;
        case WM_KEYUP:
        case WM_LBUTTONUP:
            if (g_main_window) {
                schedule_status_update(g_main_window);
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, edit_subclass_proc, id);
            break;
    }
    return result;
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

    // Cross-instance messages (all instances register the same names, so
    // the message ids match across processes)
    g_settings_changed_msg = RegisterWindowMessageW(L"npadSettingsChanged");
    g_view_sync_msg = RegisterWindowMessageW(L"npadViewStateChanged");
    g_close_all_msg = RegisterWindowMessageW(L"npadCloseAll");

    // COM apartment for the shell Save dialog (IFileSaveDialog). Tolerate an
    // already-initialised apartment (RPC_E_CHANGED_MODE / S_FALSE).
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Theme default is Light; "system" follows the OS, "dark"/solarized override
    g_dark_mode = resolve_theme_dark_mode();

    // Restore persisted find/replace options
    g_match_case = settings_get_bool("find_match_case", false);
    g_whole_word = settings_get_bool("find_whole_word", false);
    g_search_down = settings_get_bool("find_search_down", true);
    g_wrap_around = settings_get_bool("find_wrap_around", true);

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

    CoUninitialize();
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

    // Font type is per-window view state, seeded from the Defaults setting
    // (falling back to the pre-0.7 "monospace_enabled" key for migration)
    window->monospace_current =
        settings_get_bool("default_font_mono", settings_get_bool("monospace_enabled", true));

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

    // Plain text mode, effectively unlimited length, deep undo, small margins
    SendMessageW(window->edit_hwnd, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessageW(window->edit_hwnd, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(window->edit_hwnd, EM_SETUNDOLIMIT, 100000, 0);
    SendMessageW(window->edit_hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(4, 4));
    SendMessageW(window->edit_hwnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    SetWindowSubclass(window->edit_hwnd, edit_subclass_proc, 1, 0);

    // Font binding: let RichEdit pick fallback faces for characters the
    // configured font lacks (emoji, CJK), for typed and loaded text alike
    LRESULT lang = SendMessageW(window->edit_hwnd, EM_GETLANGOPTIONS, 0, 0);
    SendMessageW(window->edit_hwnd, EM_SETLANGOPTIONS, 0, lang | IMF_AUTOFONT);

    // Zoom is per-window view state, seeded from the Defaults setting
    int default_zoom = settings_get_int("default_zoom", 100);
    if (default_zoom < 10)
        default_zoom = 10;
    if (default_zoom > 500)
        default_zoom = 500;
    SendMessageW(window->edit_hwnd, EM_SETZOOM, (WPARAM) default_zoom, 100);

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
    build_accelerators(window);
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
        // Deferred startup work (crash-recovery scan) runs shortly after the
        // window is up. WM_TIMER is lowest-priority, so the first paint always
        // wins and the window appears instantly regardless of how much the
        // deferred work has to chew through.
        SetTimer(window->hwnd, NPAD_STARTUP_TIMER_ID, 50, NULL);
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

void ui_platform_get_default_window_rect(int *x, int *y, int *width, int *height) {
    RECT work;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        // Fallback to the full primary screen if the work area is unavailable
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    int work_w = work.right - work.left;
    int work_h = work.bottom - work.top;

    // Match classic Notepad's default: ~56% of the work-area width and
    // ~60% of its height, centred. Working in physical pixels makes this
    // inherently DPI-correct.
    int w = MulDiv(work_w, 56, 100);
    int h = MulDiv(work_h, 60, 100);

    if (width)
        *width = w;
    if (height)
        *height = h;
    if (x)
        *x = work.left + (work_w - w) / 2;
    if (y)
        *y = work.top + (work_h - h) / 2;
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

    // Refresh the default character format first so the incoming text adopts
    // the configured font/colour, then insert via EM_SETTEXTEX: unlike a
    // SetWindowTextW + SCF_ALL restamp, this leaves RichEdit's font binding
    // free to give emoji/CJK characters a fallback face with real glyphs.
    apply_font_default(window);

    SETTEXTEX st;
    st.flags = ST_DEFAULT;
    st.codepage = 1200; // UTF-16

    // EM_SETTEXTEX resets the zoom ratio; this window's zoom is view state
    // that must survive loading a file
    WPARAM zoom_num = 0;
    LPARAM zoom_den = 0;
    SendMessageW(window->edit_hwnd, EM_GETZOOM, (WPARAM) &zoom_num, (LPARAM) &zoom_den);

    window->setting_text_programmatically = true;
    SendMessageW(window->edit_hwnd, EM_SETTEXTEX, (WPARAM) &st, (LPARAM) wide);
    window->setting_text_programmatically = false;
    free(wide);

    // EM_GETZOOM wrote through the pointers; 0/0 means default (100%)
    // cppcheck-suppress knownConditionTrueFalse
    if (zoom_num && zoom_den) {
        SendMessageW(window->edit_hwnd, EM_SETZOOM, zoom_num, zoom_den);
    }

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
    ofn.hInstance = g_hinstance;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = L"txt";
    if (save) {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_ENABLESIZING;
    } else {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    }

    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);

    free(filter);
    free(title);

    return ok ? wide_to_utf8(filename) : NULL;
}

// Modern Save As using the shell's IFileSaveDialog. Unlike the legacy
// GetSaveFileNameW hook it looks native (matching the Open dialog) and its
// custom encoding dropdown reliably round-trips the chosen encoding back
// through params->encoding. Falls back to the classic dialog if COM fails.
static char *show_save_dialog_com(Window *parent, FileDialogParams *params) {
    IFileSaveDialog *dialog = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileSaveDialog, (void **) &dialog);
    if (FAILED(hr) || !dialog) {
        return show_file_dialog(parent, params, true);
    }

    wchar_t *wtitle = (params && params->title) ? utf8_to_wide(params->title) : NULL;
    if (wtitle) {
        dialog->lpVtbl->SetTitle(dialog, wtitle);
    }

    COMDLG_FILTERSPEC types[] = {
        { L"Text Files (*.txt)", L"*.txt" },
        { L"All Files (*.*)", L"*.*" },
    };
    dialog->lpVtbl->SetFileTypes(dialog, 2, types);
    dialog->lpVtbl->SetFileTypeIndex(dialog, 1);
    dialog->lpVtbl->SetDefaultExtension(dialog, L"txt");

    // Pre-fill name (and folder, when the current file has a path) so a Save As
    // of an open document defaults to that file rather than "Untitled.txt".
    if (params && params->default_filename && params->default_filename[0]) {
        wchar_t *wpath = utf8_to_wide(params->default_filename);
        if (wpath) {
            wchar_t *base = wpath;
            wchar_t *sep = wcsrchr(wpath, L'\\');
            wchar_t *fsep = wcsrchr(wpath, L'/');
            if (fsep && (!sep || fsep > sep)) {
                sep = fsep;
            }
            if (sep) {
                *sep = L'\0';
                base = sep + 1;
                IShellItem *folder = NULL;
                if (SUCCEEDED(SHCreateItemFromParsingName(wpath, NULL, &IID_IShellItem,
                                                          (void **) &folder)) &&
                    folder) {
                    dialog->lpVtbl->SetFolder(dialog, folder);
                    folder->lpVtbl->Release(folder);
                }
            }
            dialog->lpVtbl->SetFileName(dialog, base);
            free(wpath);
        }
    }

    // Custom encoding dropdown
    IFileDialogCustomize *custom = NULL;
    if (SUCCEEDED(
            dialog->lpVtbl->QueryInterface(dialog, &IID_IFileDialogCustomize, (void **) &custom)) &&
        custom) {
        custom->lpVtbl->StartVisualGroup(custom, SAVE_ENC_GROUP_ID, L"Encoding");
        custom->lpVtbl->AddComboBox(custom, SAVE_ENC_COMBO_ID);
        for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++) {
            wchar_t *name = utf8_to_wide(file_encoding_name((TextEncoding) i));
            if (name) {
                custom->lpVtbl->AddControlItem(custom, SAVE_ENC_COMBO_ID, (DWORD) i, name);
                free(name);
            }
        }
        custom->lpVtbl->EndVisualGroup(custom);
        DWORD initial = params ? (DWORD) params->encoding : (DWORD) NPAD_ENC_UTF8;
        custom->lpVtbl->SetSelectedControlItem(custom, SAVE_ENC_COMBO_ID, initial);
    }

    char *result = NULL;
    hr = dialog->lpVtbl->Show(dialog, parent ? parent->hwnd : NULL);
    if (SUCCEEDED(hr)) {
        if (custom && params) {
            DWORD sel = 0;
            if (SUCCEEDED(
                    custom->lpVtbl->GetSelectedControlItem(custom, SAVE_ENC_COMBO_ID, &sel)) &&
                sel <= (DWORD) NPAD_ENC_ANSI) {
                params->encoding = (TextEncoding) sel;
            }
        }
        IShellItem *item = NULL;
        if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)) && item) {
            wchar_t *wresult = NULL;
            if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &wresult)) &&
                wresult) {
                result = wide_to_utf8(wresult);
                CoTaskMemFree(wresult);
            }
            item->lpVtbl->Release(item);
        }
    }

    if (custom) {
        custom->lpVtbl->Release(custom);
    }
    dialog->lpVtbl->Release(dialog);
    free(wtitle);
    return result;
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    // The open dialog never writes back through params
    return show_file_dialog(parent, (FileDialogParams *) params, false);
}

char *ui_platform_show_save_dialog(Window *parent, FileDialogParams *params) {
    return show_save_dialog_com(parent, params);
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

// Show a transient message in the leftmost status bar part
static void set_status_message(Window *window, const char *message) {
    if (!window || !window->status_hwnd)
        return;
    wchar_t *wide = utf8_to_wide(message ? message : "");
    if (wide) {
        SendMessageW(window->status_hwnd, SB_SETTEXTW, 0, (LPARAM) wide);
        free(wide);
    }
}

// Count total matches of the current search term (capped) and the 1-based
// ordinal of the match at `current`. Match count uses the current
// case/whole-word options.
static void count_matches(Window *window, const CHARRANGE *current, int *total, int *ordinal) {
    const int CAP = 10000;
    *total = 0;
    *ordinal = 0;

    FINDTEXTEXW ft;
    LONG from = 0;
    WPARAM flags = FR_DOWN;
    if (g_match_case)
        flags |= FR_MATCHCASE;
    if (g_whole_word)
        flags |= FR_WHOLEWORD;

    while (*total < CAP) {
        memset(&ft, 0, sizeof(ft));
        ft.lpstrText = g_search_text;
        ft.chrg.cpMin = from;
        ft.chrg.cpMax = -1;
        if (SendMessageW(window->edit_hwnd, EM_FINDTEXTEXW, flags, (LPARAM) &ft) == -1)
            break;
        (*total)++;
        if (ft.chrgText.cpMin == current->cpMin) {
            *ordinal = *total;
        }
        from = ft.chrgText.cpMax;
        if (from <= ft.chrgText.cpMin)
            from = ft.chrgText.cpMin + 1; // Guard against zero-width matches
    }
}

// Run one EM_FINDTEXTEXW pass over the given range
static bool find_in_range(Window *window, LONG from, LONG to, bool down, CHARRANGE *found) {
    FINDTEXTEXW ft;
    memset(&ft, 0, sizeof(ft));
    ft.lpstrText = g_search_text;
    ft.chrg.cpMin = from;
    ft.chrg.cpMax = to;

    WPARAM flags = 0;
    if (down)
        flags |= FR_DOWN;
    if (g_match_case)
        flags |= FR_MATCHCASE;
    if (g_whole_word)
        flags |= FR_WHOLEWORD;

    if (SendMessageW(window->edit_hwnd, EM_FINDTEXTEXW, flags, (LPARAM) &ft) == -1)
        return false;

    *found = ft.chrgText;
    return true;
}

// Search from the current selection in the given direction, wrapping around
// if enabled, select the match, and report failure like Notepad.
static bool find_next(Window *window, bool down) {
    if (!window || !window->edit_hwnd || g_search_text[0] == L'\0')
        return false;

    CHARRANGE sel = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);

    CHARRANGE found;
    bool hit;
    bool wrapped = false;

    if (down) {
        hit = find_in_range(window, sel.cpMax, -1, true, &found);
    } else {
        hit = find_in_range(window, sel.cpMin, 0, false, &found);
    }

    if (!hit && g_wrap_around) {
        LONG length = (LONG) SendMessageW(window->edit_hwnd, WM_GETTEXTLENGTH, 0, 0);
        if (down) {
            hit = find_in_range(window, 0, -1, true, &found);
        } else {
            hit = find_in_range(window, length, 0, false, &found);
        }
        wrapped = hit;
    }

    if (!hit) {
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

    SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &found);
    SendMessageW(window->edit_hwnd, EM_SCROLLCARET, 0, 0);

    int total = 0, ordinal = 0;
    count_matches(window, &found, &total, &ordinal);
    char message[128];
    if (wrapped) {
        snprintf(message, sizeof(message), "Wrapped around - Match %d of %d", ordinal, total);
    } else {
        snprintf(message, sizeof(message), "Match %d of %d", ordinal, total);
    }
    set_status_message(window, message);
    return true;
}

// Find/Replace term history: most-recent-first, de-duplicated, capped
#define FIND_HISTORY_MAX 10

static void history_load(HWND combo, const char *prefix) {
    for (int i = 0; i < FIND_HISTORY_MAX; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        char *value = settings_get_string(key, NULL);
        if (!value)
            break;
        if (value[0]) {
            wchar_t *wide = utf8_to_wide(value);
            if (wide) {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) wide);
                free(wide);
            }
        }
        free(value);
    }
}

static void history_push(const char *prefix, const wchar_t *value) {
    if (!value || value[0] == L'\0')
        return;
    char *utf8 = wide_to_utf8(value);
    if (!utf8)
        return;

    // Read existing entries, dropping any duplicate of the new value
    char *entries[FIND_HISTORY_MAX];
    int count = 0;
    entries[count++] = utf8;
    for (int i = 0; i < FIND_HISTORY_MAX && count < FIND_HISTORY_MAX; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        char *existing = settings_get_string(key, NULL);
        if (!existing)
            break;
        if (existing[0] && strcmp(existing, utf8) != 0) {
            entries[count++] = existing;
        } else {
            free(existing);
        }
    }

    for (int i = 0; i < FIND_HISTORY_MAX; i++) {
        char key[48];
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        if (i < count) {
            settings_set_string(key, entries[i]);
        } else {
            settings_remove_key(key);
        }
    }

    for (int i = 0; i < count; i++) {
        free(entries[i]);
    }
}

// Read search parameters out of the find/replace dialog controls and
// persist them across sessions
static void read_search_fields(HWND dialog, bool has_replace) {
    GetDlgItemTextW(dialog, ID_FIND_TEXT, g_search_text,
                    sizeof(g_search_text) / sizeof(g_search_text[0]));
    if (has_replace) {
        GetDlgItemTextW(dialog, ID_REPLACE_WITH, g_replace_text,
                        sizeof(g_replace_text) / sizeof(g_replace_text[0]));
    }
    g_match_case = IsDlgButtonChecked(dialog, ID_FIND_CASE) == BST_CHECKED;
    g_whole_word = IsDlgButtonChecked(dialog, ID_FIND_WHOLE_WORD) == BST_CHECKED;
    g_wrap_around = IsDlgButtonChecked(dialog, ID_FIND_WRAP) == BST_CHECKED;
    if (GetDlgItem(dialog, IDC_RADIO_UP)) {
        g_search_down = IsDlgButtonChecked(dialog, IDC_RADIO_UP) != BST_CHECKED;
    }

    settings_set_bool("find_match_case", g_match_case);
    settings_set_bool("find_whole_word", g_whole_word);
    settings_set_bool("find_wrap_around", g_wrap_around);
    settings_set_bool("find_search_down", g_search_down);

    history_push("find_hist", g_search_text);
    if (has_replace) {
        history_push("replace_hist", g_replace_text);
    }
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
            history_load(GetDlgItem(dialog, ID_FIND_TEXT), "find_hist");
            SetDlgItemTextW(dialog, ID_FIND_TEXT, g_search_text);
            if (GetDlgItem(dialog, ID_REPLACE_WITH)) {
                history_load(GetDlgItem(dialog, ID_REPLACE_WITH), "replace_hist");
                SetDlgItemTextW(dialog, ID_REPLACE_WITH, g_replace_text);
            }
            CheckDlgButton(dialog, ID_FIND_CASE, g_match_case ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, ID_FIND_WHOLE_WORD, g_whole_word ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, ID_FIND_WRAP, g_wrap_around ? BST_CHECKED : BST_UNCHECKED);
            if (GetDlgItem(dialog, IDC_RADIO_UP)) {
                CheckRadioButton(dialog, IDC_RADIO_UP, IDC_RADIO_DOWN,
                                 g_search_down ? IDC_RADIO_DOWN : IDC_RADIO_UP);
            }

            // Position like notepad.exe: offset into the parent window,
            // or wherever the user last moved it this session
            Window *parent = (Window *) lparam;
            if (g_find_dialog_pos_valid) {
                SetWindowPos(dialog, NULL, g_find_dialog_pos.x, g_find_dialog_pos.y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else if (parent && parent->hwnd) {
                RECT parent_rect;
                if (GetWindowRect(parent->hwnd, &parent_rect)) {
                    int dpi = (int) get_window_dpi(parent->hwnd);
                    SetWindowPos(dialog, NULL, parent_rect.left + MulDiv(84, dpi, 96),
                                 parent_rect.top + MulDiv(185, dpi, 96), 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }

            SendDlgItemMessageW(dialog, ID_FIND_TEXT, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
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

        case WM_DESTROY: {
            RECT rect;
            if (GetWindowRect(dialog, &rect)) {
                g_find_dialog_pos.x = rect.left;
                g_find_dialog_pos.y = rect.top;
                g_find_dialog_pos_valid = true;
            }
            if (g_find_dialog == dialog) {
                g_find_dialog = NULL;
            }
            return FALSE;
        }
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

    // Colors and the dark flag come from the resolved color scheme
    COLORREF back, text;
    g_dark_mode = theme_colors(&back, &text);

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
    refresh_font_binding(window); // Restamping can drop emoji fallback faces

    window->setting_text_programmatically = false;
    SendMessageW(window->edit_hwnd, EM_SETMODIFY, (WPARAM) was_modified, 0);

    if (window->status_hwnd) {
        SendMessageW(window->status_hwnd, SB_SETBKCOLOR, 0,
                     g_dark_mode ? (LPARAM) RGB(45, 45, 45) : (LPARAM) CLR_DEFAULT);
    }

    if (window->hwnd) {
        set_title_bar_dark(window->hwnd, g_dark_mode);
    }
}

void ui_platform_set_dark_mode(bool enabled) {
    // The quick View > Dark Mode toggle maps onto the light/dark schemes
    settings_set_string("theme", enabled ? "dark" : "light");
    g_dark_mode = enabled;
    if (g_main_window) {
        // Colors only: apply_theme restamps CFM_COLOR; a full font stamp
        // would wipe the fallback faces bound to emoji/CJK runs
        apply_font_default(g_main_window);
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
    int line_index = (int) SendMessageW(window->edit_hwnd, EM_EXLINEFROMCHAR, 0, pos);

    // RichEdit reports the previous line when the caret sits at the very
    // end of the text right after a line break; if the next line starts at
    // or before the caret, the caret is actually on that next line
    LRESULT next_start =
        SendMessageW(window->edit_hwnd, EM_LINEINDEX, (WPARAM) (line_index + 1), 0);
    if (next_start != -1 && next_start <= pos) {
        line_index++;
    }

    int line_start = (int) SendMessageW(window->edit_hwnd, EM_LINEINDEX, (WPARAM) line_index, 0);
    *line = line_index + 1;
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

void ui_platform_set_session_timer(Window *window, int seconds) {
    if (!window || !window->hwnd)
        return;

    KillTimer(window->hwnd, NPAD_SESSION_TIMER_ID);
    if (seconds > 0) {
        SetTimer(window->hwnd, NPAD_SESSION_TIMER_ID, (UINT) seconds * 1000, NULL);
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
    on_view_state_changed(window); // User-initiated zoom change
}

// Called after a user-initiated view-state change (font type or zoom) in
// this window. Honors the two opt-in preferences:
//  - auto-update defaults: the new state becomes the default for new windows
//  - sync view: the new state is pushed live to all other npad windows
static void on_view_state_changed(Window *window) {
    if (!window)
        return;

    if (settings_get_bool("auto_update_defaults", false)) {
        settings_set_bool("default_font_mono", window->monospace_current);
        settings_set_int("default_zoom", get_zoom(window));
        settings_save();
        ui_platform_notify_settings_changed();
    }

    if (settings_get_bool("sync_view_state", false) && g_view_sync_msg) {
        LPARAM state = MAKELPARAM(get_zoom(window), window->monospace_current ? 1 : 0);
        broadcast_to_npad_windows(g_view_sync_msg, state);
    }
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
// When the monospace toggle (Ctrl+M) is on, Consolas overrides the
// user-chosen face.
// The settings key (and default) for the font face the editor is currently
// showing. OpenDyslexic overrides both; otherwise the Monospace toggle
// selects between the monospace and proportional faces.
static const char *active_font_key(const Window *window, const char **default_face) {
    // Only honour OpenDyslexic when the font is actually installed, so a
    // stale/imported setting cannot leave the editor stuck on a substituted
    // fallback face (which would also make the mono/proportional toggle
    // appear to do nothing).
    if (settings_get_bool("opendyslexic_enabled", false) &&
        font_is_installed(OPENDYSLEXIC_FONT_W)) {
        if (default_face)
            *default_face = OPENDYSLEXIC_FONT;
        return "opendyslexic_font";
    }
    // Font type is per-window view state (see monospace_current)
    if (!window || window->monospace_current) {
        if (default_face)
            *default_face = DEFAULT_MONO_FONT;
        return "monospace_font";
    }
    if (default_face)
        *default_face = DEFAULT_PROP_FONT;
    return "proportional_font";
}

static int CALLBACK font_enum_proc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type,
                                   LPARAM param) {
    (void) lf;
    (void) tm;
    (void) type;
    *(bool *) param = true;
    return 0; // Stop after the first match
}

// Whether a font family is installed on the system
static bool font_is_installed(const wchar_t *face) {
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    lf.lfCharSet = DEFAULT_CHARSET;

    bool found = false;
    HDC hdc = GetDC(NULL);
    EnumFontFamiliesExW(hdc, &lf, font_enum_proc, (LPARAM) &found, 0);
    ReleaseDC(NULL, hdc);
    return found;
}

// Fill cf with the window's configured font (face, size, weight, italic)
// and the current scheme's text color
static void build_char_format(const Window *window, CHARFORMAT2W *cf) {
    const char *default_face = DEFAULT_MONO_FONT;
    const char *key = active_font_key(window, &default_face);
    char *face = settings_get_string(key, default_face);
    int size = settings_get_int("font_size", 11);
    int weight = settings_get_int("font_weight", FW_NORMAL);
    bool italic = settings_get_bool("font_italic", false);

    if (size < 6)
        size = 6;
    if (size > 72)
        size = 72;

    ZeroMemory(cf, sizeof(*cf));
    cf->cbSize = sizeof(*cf);
    cf->dwMask = CFM_FACE | CFM_SIZE | CFM_WEIGHT | CFM_ITALIC | CFM_COLOR;
    cf->yHeight = size * 20; // Twips
    cf->wWeight = (WORD) weight;
    cf->dwEffects = italic ? CFE_ITALIC : 0;
    theme_colors(NULL, &cf->crTextColor); // Match the current scheme's text color

    wchar_t *wide_face = utf8_to_wide(face ? face : "Consolas");
    if (wide_face) {
        wcsncpy(cf->szFaceName, wide_face, LF_FACESIZE - 1);
        cf->szFaceName[LF_FACESIZE - 1] = L'\0';
        free(wide_face);
    }
    free(face);
}

// Refresh only the control's default character format. Existing runs keep
// their formatting - in particular the fallback faces RichEdit's font
// binding gave emoji/CJK runs, which a blanket SCF_ALL face stamp would
// replace with a face that has no glyphs for them (showing boxes).
static void apply_font_default(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    CHARFORMAT2W cf;
    build_char_format(window, &cf);
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf);
}

// The control runs in plain-text mode, where the whole document shares one
// character format: a document-wide format change (font or theme) discards
// the fallback faces RichEdit's font binding gave emoji and other non-BMP
// characters, leaving placeholder boxes. Binding only re-runs when text is
// inserted, so when the document contains astral characters, re-set the
// identical text through EM_SETTEXTEX to re-trigger it. Caret, scroll and
// modified state are preserved; the undo stack is kept via ST_KEEPUNDO.
// Callers hold setting_text_programmatically while this runs.
static void refresh_font_binding(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200;
    LRESULT len = SendMessageW(window->edit_hwnd, EM_GETTEXTLENGTHEX, (WPARAM) &gtl, 0);
    if (len <= 0)
        return;

    wchar_t *buf = malloc(((size_t) len + 1) * sizeof(wchar_t));
    if (!buf)
        return;

    GETTEXTEX gt;
    ZeroMemory(&gt, sizeof(gt));
    gt.cb = (DWORD) (((size_t) len + 1) * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    LRESULT got = SendMessageW(window->edit_hwnd, EM_GETTEXTEX, (WPARAM) &gt, (LPARAM) buf);
    if (got <= 0) {
        free(buf);
        return;
    }

    bool has_astral = false;
    for (LRESULT i = 0; i + 1 < got; i++) {
        if (IS_HIGH_SURROGATE(buf[i]) && IS_LOW_SURROGATE(buf[i + 1])) {
            has_astral = true;
            break;
        }
    }
    if (!has_astral) {
        free(buf); // Nothing the configured font could not render itself
        return;
    }

    CHARRANGE saved_sel = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &saved_sel);
    POINT scroll_pos = { 0, 0 };
    SendMessageW(window->edit_hwnd, EM_GETSCROLLPOS, 0, (LPARAM) &scroll_pos);
    WPARAM zoom_num = 0;
    LPARAM zoom_den = 0;
    SendMessageW(window->edit_hwnd, EM_GETZOOM, (WPARAM) &zoom_num, (LPARAM) &zoom_den);
    SendMessageW(window->edit_hwnd, WM_SETREDRAW, FALSE, 0);

    SETTEXTEX st;
    st.flags = ST_KEEPUNDO;
    st.codepage = 1200;
    SendMessageW(window->edit_hwnd, EM_SETTEXTEX, (WPARAM) &st, (LPARAM) buf);

    // EM_GETZOOM wrote through the pointers; EM_SETTEXTEX resets the ratio
    // cppcheck-suppress knownConditionTrueFalse
    if (zoom_num && zoom_den) {
        SendMessageW(window->edit_hwnd, EM_SETZOOM, zoom_num, zoom_den);
    }
    SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &saved_sel);
    SendMessageW(window->edit_hwnd, EM_SETSCROLLPOS, 0, (LPARAM) &scroll_pos);
    SendMessageW(window->edit_hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(window->edit_hwnd, NULL, TRUE);
    free(buf);
}

// Apply the configured font to the document. In plain-text mode setting the
// default format restyles all text; refresh_font_binding then restores
// emoji fallback faces. Text loading itself uses apply_font_default +
// EM_SETTEXTEX, which binds as it inserts.
static void apply_font(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    CHARFORMAT2W cf;
    build_char_format(window, &cf);

    LRESULT was_modified = SendMessageW(window->edit_hwnd, EM_GETMODIFY, 0, 0);
    window->setting_text_programmatically = true;
    SendMessageW(window->edit_hwnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf);
    refresh_font_binding(window);
    window->setting_text_programmatically = false;
    SendMessageW(window->edit_hwnd, EM_SETMODIFY, (WPARAM) was_modified, 0);
}

// Show the font chooser and save the selection to font_key (face) plus the
// shared size/weight/italic settings.
static void show_font_dialog(Window *window, const char *font_key, const char *default_face) {
    if (!window || !window->edit_hwnd)
        return;

    char *face = settings_get_string(font_key, default_face);
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
    wchar_t *wide_face = utf8_to_wide(face ? face : default_face);
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
            settings_set_string(font_key, new_face);
            free(new_face);
        }
        settings_set_int("font_size", cf.iPointSize / 10); // iPointSize is tenths of a point
        settings_set_int("font_weight", (int) lf.lfWeight);
        settings_set_bool("font_italic", lf.lfItalic != 0);

        apply_font(window);
        settings_save(); // Persist and propagate to other windows
        ui_platform_notify_settings_changed();
    }
}

// ---------------------------------------------------------------------------
// Preferences (tabbed property sheet)
// ---------------------------------------------------------------------------

// Mark a property-sheet page dirty so the Apply button becomes enabled
static void mark_prefs_dirty(HWND page) {
    PropSheet_Changed(GetParent(page), page);
}

static INT_PTR CALLBACK prefs_general_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG:
            CheckDlgButton(page, ID_PREF_AUTOSAVE_ENABLED,
                           editor_is_auto_save_enabled() ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(page, ID_PREF_AUTOSAVE_INTERVAL, (UINT) editor_get_auto_save_interval(),
                          FALSE);
            SetDlgItemInt(page, ID_PREF_LARGE_FILE_MB,
                          (UINT) settings_get_int("large_file_warning_mb", 100), FALSE);
            SetDlgItemInt(page, ID_PREF_RECENT_MAX, (UINT) settings_get_int("recent_files_max", 10),
                          FALSE);
            CheckDlgButton(page, ID_PREF_SESSION_ENABLED,
                           editor_is_session_resume_enabled() ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(page, ID_PREF_SESSION_INTERVAL,
                          (UINT) settings_get_int("session_interval", 30), FALSE);
            CheckDlgButton(page, ID_PREF_CTRL_N_WINDOW,
                           settings_get_bool("ctrl_n_new_window", false) ? BST_CHECKED
                                                                         : BST_UNCHECKED);
            EnableWindow(GetDlgItem(page, ID_PREF_AUTOSAVE_INTERVAL),
                         editor_is_auto_save_enabled());
            EnableWindow(GetDlgItem(page, ID_PREF_SESSION_INTERVAL),
                         editor_is_session_resume_enabled());
            return TRUE;

        case WM_COMMAND: {
            WORD code = HIWORD(wparam);
            WORD id = LOWORD(wparam);
            // Enable Apply when a value control changes (not the action buttons)
            if ((code == BN_CLICKED &&
                 (id == ID_PREF_AUTOSAVE_ENABLED || id == ID_PREF_SESSION_ENABLED ||
                  id == ID_PREF_CTRL_N_WINDOW)) ||
                (code == EN_CHANGE &&
                 (id == ID_PREF_AUTOSAVE_INTERVAL || id == ID_PREF_LARGE_FILE_MB ||
                  id == ID_PREF_RECENT_MAX || id == ID_PREF_SESSION_INTERVAL))) {
                mark_prefs_dirty(page);
            }

            if (id == ID_PREF_AUTOSAVE_ENABLED) {
                EnableWindow(GetDlgItem(page, ID_PREF_AUTOSAVE_INTERVAL),
                             IsDlgButtonChecked(page, ID_PREF_AUTOSAVE_ENABLED) == BST_CHECKED);
                return TRUE;
            }
            if (id == ID_PREF_SESSION_ENABLED) {
                EnableWindow(GetDlgItem(page, ID_PREF_SESSION_INTERVAL),
                             IsDlgButtonChecked(page, ID_PREF_SESSION_ENABLED) == BST_CHECKED);
                return TRUE;
            }
            if (id == ID_PREF_RECENT_CLEAR) {
                settings_clear_recent_files();
                if (g_main_window) {
                    rebuild_recent_menu(g_main_window);
                }
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                BOOL ok = FALSE;

                editor_enable_auto_save(IsDlgButtonChecked(page, ID_PREF_AUTOSAVE_ENABLED) ==
                                        BST_CHECKED);

                UINT interval = GetDlgItemInt(page, ID_PREF_AUTOSAVE_INTERVAL, &ok, FALSE);
                if (ok && interval > 0) {
                    editor_set_auto_save_interval((int) interval);
                }

                UINT large_mb = GetDlgItemInt(page, ID_PREF_LARGE_FILE_MB, &ok, FALSE);
                if (ok) {
                    settings_set_int("large_file_warning_mb", (int) large_mb);
                }

                UINT recent_max = GetDlgItemInt(page, ID_PREF_RECENT_MAX, &ok, FALSE);
                if (ok) {
                    if (recent_max > MAX_RECENT_MENU_FILES) {
                        recent_max = MAX_RECENT_MENU_FILES;
                    }
                    settings_set_int("recent_files_max", (int) recent_max);
                    if (g_main_window) {
                        rebuild_recent_menu(g_main_window);
                    }
                }

                UINT session_interval = GetDlgItemInt(page, ID_PREF_SESSION_INTERVAL, &ok, FALSE);
                if (ok && session_interval > 0) {
                    settings_set_int("session_interval", (int) session_interval);
                }
                // Enable/disable last so the interval it reads is current
                editor_enable_session_resume(IsDlgButtonChecked(page, ID_PREF_SESSION_ENABLED) ==
                                             BST_CHECKED);

                settings_set_bool("ctrl_n_new_window",
                                  IsDlgButtonChecked(page, ID_PREF_CTRL_N_WINDOW) == BST_CHECKED);
                if (g_main_window) {
                    apply_new_window_pref(g_main_window);
                }

                settings_save(); // Apply button: persist + propagate immediately
                ui_platform_notify_settings_changed();

                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Color scheme setting values, indexed by the Appearance combo selection
static const char *const SCHEME_KEYS[] = { "system", "light", "dark", "solarized-light",
                                           "solarized-dark" };
static const wchar_t *const SCHEME_LABELS[] = { L"Follow system", L"Light", L"Dark",
                                                L"Solarized Light", L"Solarized Dark" };
#define SCHEME_COUNT ((int) (sizeof(SCHEME_KEYS) / sizeof(SCHEME_KEYS[0])))

static INT_PTR CALLBACK prefs_appearance_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG: {
            HWND combo = GetDlgItem(page, ID_PREF_SCHEME);
            for (int i = 0; i < SCHEME_COUNT; i++) {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) SCHEME_LABELS[i]);
            }

            char *theme = settings_get_string("theme", "light");
            int sel = 1; // Light (index of "light" in SCHEME_KEYS)
            for (int i = 0; i < SCHEME_COUNT; i++) {
                if (theme && strcmp(theme, SCHEME_KEYS[i]) == 0) {
                    sel = i;
                    break;
                }
            }
            free(theme);
            SendMessageW(combo, CB_SETCURSEL, (WPARAM) sel, 0);

            CheckDlgButton(page, ID_PREF_OPENDYSLEXIC,
                           settings_get_bool("opendyslexic_enabled", false) ? BST_CHECKED
                                                                            : BST_UNCHECKED);
            CheckDlgButton(page, ID_PREF_STATUSBAR,
                           (g_main_window && g_main_window->status_bar_visible) ? BST_CHECKED
                                                                                : BST_UNCHECKED);
            CheckDlgButton(page, ID_PREF_SYNC_VIEW,
                           settings_get_bool("sync_view_state", false) ? BST_CHECKED
                                                                       : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD code = HIWORD(wparam);
            WORD id = LOWORD(wparam);
            if ((code == BN_CLICKED && (id == ID_PREF_OPENDYSLEXIC || id == ID_PREF_STATUSBAR ||
                                        id == ID_PREF_SYNC_VIEW)) ||
                (code == CBN_SELCHANGE && id == ID_PREF_SCHEME)) {
                mark_prefs_dirty(page);
            }
            if (id == ID_PREF_FONT_MONO) {
                show_font_dialog(g_main_window, "monospace_font", DEFAULT_MONO_FONT);
                return TRUE;
            }
            if (id == ID_PREF_FONT_PROP) {
                show_font_dialog(g_main_window, "proportional_font", DEFAULT_PROP_FONT);
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                int sel = (int) SendDlgItemMessageW(page, ID_PREF_SCHEME, CB_GETCURSEL, 0, 0);
                if (sel < 0 || sel >= SCHEME_COUNT) {
                    sel = 0;
                }
                settings_set_string("theme", SCHEME_KEYS[sel]);

                bool dyslexic = IsDlgButtonChecked(page, ID_PREF_OPENDYSLEXIC) == BST_CHECKED;
                if (dyslexic && !font_is_installed(OPENDYSLEXIC_FONT_W)) {
                    // Cannot enable a font that is not installed: inform the
                    // user and revert both the setting and the checkbox.
                    MessageBoxW(page,
                                L"The OpenDyslexic font is not installed.\n"
                                L"Download it from https://opendyslexic.org and install it, "
                                L"then enable this option.",
                                L"npad", MB_OK | MB_ICONINFORMATION);
                    dyslexic = false;
                    CheckDlgButton(page, ID_PREF_OPENDYSLEXIC, BST_UNCHECKED);
                }
                settings_set_bool("opendyslexic_enabled", dyslexic);

                // Re-apply colors and font for the chosen scheme (also syncs g_dark_mode)
                if (g_main_window) {
                    apply_font(g_main_window);
                    apply_theme(g_main_window);
                    update_status_bar(g_main_window);
                }

                bool show_status = IsDlgButtonChecked(page, ID_PREF_STATUSBAR) == BST_CHECKED;
                if (g_main_window && g_main_window->status_bar_visible != show_status) {
                    set_status_bar_visible(g_main_window, show_status);
                }

                bool sync_view = IsDlgButtonChecked(page, ID_PREF_SYNC_VIEW) == BST_CHECKED;
                settings_set_bool("sync_view_state", sync_view);

                settings_save(); // Apply button: persist + propagate immediately
                ui_platform_notify_settings_changed();

                // Enabling sync should bring the other windows into line with
                // this (active) window's view state right away, not only on the
                // next change. Other windows enable sync via the settings-changed
                // reload above, then adopt the view state posted here.
                if (sync_view && g_main_window) {
                    on_view_state_changed(g_main_window);
                }

                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Copy the current settings.json to a user-chosen file
static void export_settings(HWND owner) {
    settings_save(); // Flush current in-memory settings to disk first
    const char *src = settings_get_file_path();
    if (!src)
        return;

    wchar_t filename[MAX_PATH] = L"npad-settings.json";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (GetSaveFileNameW(&ofn)) {
        char *dest = wide_to_utf8(filename);
        bool ok = dest && file_copy(src, dest);
        free(dest);
        MessageBoxW(owner, ok ? L"Settings exported." : L"Failed to export settings.", L"npad",
                    ok ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONWARNING));
    }
}

// Replace the current settings with a user-chosen file and reload them
static void import_settings(HWND owner) {
    const char *dest = settings_get_file_path();
    if (!dest)
        return;

    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    char *src = wide_to_utf8(filename);
    bool ok = src && file_copy(src, dest);
    free(src);
    if (!ok) {
        MessageBoxW(owner, L"Failed to import settings.", L"npad", MB_OK | MB_ICONWARNING);
        return;
    }

    // Reload settings and re-apply what can change live
    settings_clear_all();
    settings_load();
    editor_reload_prefs();
    if (g_main_window) {
        apply_font(g_main_window);
        apply_theme(g_main_window);
        apply_new_window_pref(g_main_window);
        rebuild_recent_menu(g_main_window);
        update_status_bar(g_main_window);
    }
    // Propagate to other running instances too
    ui_platform_notify_settings_changed();

    MessageBoxW(owner,
                L"Settings imported. A few options (window size, default encoding) "
                L"take effect on restart.",
                L"npad", MB_OK | MB_ICONINFORMATION);
}

// Reset every preference to its default (Backup page). Recent files, window
// geometry and Find/Replace history are deliberately kept - only the keys
// exposed in the Preferences tabs (plus word wrap) are removed; the getters
// fall back to their defaults for missing keys.
static void reset_all_preferences(HWND owner) {
    static const char *pref_keys[] = {
        // General
        "auto_save_enabled", "auto_save_interval", "large_file_warning_mb", "recent_files_max",
        "session_resume_enabled", "session_interval", "ctrl_n_new_window",
        // Appearance
        "theme", "monospace_font", "proportional_font", "font_size", "font_weight", "font_italic",
        "opendyslexic_enabled", "opendyslexic_font", "status_bar_visible", "sync_view_state",
        // Defaults
        "default_encoding", "default_line_ending", "default_font_mono", "default_zoom",
        "auto_update_defaults",
        // Other shared options
        "word_wrap"
    };

    if (MessageBoxW(owner,
                    L"Reset all preferences to their defaults?\n\n"
                    L"Recent files, window position and Find/Replace history are kept.\n"
                    L"The Preferences window will close.",
                    L"npad", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    for (size_t i = 0; i < sizeof(pref_keys) / sizeof(pref_keys[0]); i++) {
        settings_remove_key(pref_keys[i]);
    }
    settings_save();

    // Re-apply everything that can change live (same path as a cross-instance
    // settings change), then tell other running instances
    if (g_main_window) {
        reload_and_apply_settings(g_main_window);
    }
    ui_platform_notify_settings_changed();

    // The sheet's controls still show pre-reset values; close it rather than
    // leave stale state on screen
    PropSheet_PressButton(GetParent(owner), PSBTN_CANCEL);
}

// --- Debug page (hidden: Ctrl+Shift+. or Shift+click on Preferences) ------

// Diagnostics text for the Debug page (malloc'd wide string; caller frees)
static wchar_t *build_diagnostics_text(void) {
    const size_t cap = 8192;
    wchar_t *buf = malloc(cap * sizeof(wchar_t));
    if (!buf)
        return NULL;
    size_t used = 0;
#define DIAG_APPEND(...)                                                                           \
    do {                                                                                           \
        if (used < cap - 1) {                                                                      \
            int n = _snwprintf(buf + used, cap - used - 1, __VA_ARGS__);                           \
            if (n > 0)                                                                             \
                used += (size_t) n;                                                                \
        }                                                                                          \
    } while (0)

    wchar_t *version = utf8_to_wide(NPAD_VERSION);
    DIAG_APPEND(L"npad %ls\r\n", version ? version : L"?");
    free(version);

    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH)) {
        DIAG_APPEND(L"Executable: %ls\r\n", exe_path);
    }

    const char *settings_path = settings_get_file_path();
    if (settings_path) {
        wchar_t *wide = utf8_to_wide(settings_path);
        if (wide) {
            DIAG_APPEND(L"Settings: %ls (%d entries)\r\n", wide, settings_count());
            free(wide);
        }
    }

    char *recovery_dir = editor_session_dir();
    if (recovery_dir) {
        int slot_count = 0;
        char **slots = session_list_slots(recovery_dir, &slot_count);
        session_free_slots(slots, slot_count);
        wchar_t *wide = utf8_to_wide(recovery_dir);
        if (wide) {
            DIAG_APPEND(L"Recovery: %ls (%d slots)\r\n", wide, slot_count);
            free(wide);
        }
        free(recovery_dir);
    }

    DIAG_APPEND(L"Open npad windows: %d\r\n", count_npad_windows());

    DIAG_APPEND(L"\r\nStartup profile:\r\n");
    for (int i = 0; i < startup_prof_count(); i++) {
        double at = startup_prof_ms(i);
        double delta = (i > 0) ? at - startup_prof_ms(i - 1) : 0.0;
        wchar_t *phase = utf8_to_wide(startup_prof_name(i));
        DIAG_APPEND(L"  %8.1f ms  (+%6.1f)  %ls\r\n", at, delta, phase ? phase : L"?");
        free(phase);
    }

    DIAG_APPEND(L"\r\nEditor counters (since launch):\r\n");
    DIAG_APPEND(L"  Paints: %u (last %.2f ms, avg %.2f ms)\r\n", g_paint_count, g_last_paint_ms,
                g_paint_count ? g_paint_total_ms / g_paint_count : 0.0);
    DIAG_APPEND(L"  Selection changes: %u\r\n", g_selchange_count);

#undef DIAG_APPEND
    buf[cap - 1] = L'\0';
    return buf;
}

static INT_PTR CALLBACK prefs_debug_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    static HFONT mono_font = NULL;

    switch (msg) {
        case WM_INITDIALOG: {
            if (!mono_font) {
                mono_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            }
            if (mono_font) {
                SendDlgItemMessageW(page, ID_PREF_DEBUG_TEXT, WM_SETFONT, (WPARAM) mono_font,
                                    FALSE);
            }
            wchar_t *text = build_diagnostics_text();
            if (text) {
                SetDlgItemTextW(page, ID_PREF_DEBUG_TEXT, text);
                free(text);
            }
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wparam) == ID_PREF_COPY_DIAG) {
                HWND edit = GetDlgItem(page, ID_PREF_DEBUG_TEXT);
                int len = GetWindowTextLengthW(edit);
                HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, ((size_t) len + 1) * sizeof(wchar_t));
                if (mem) {
                    wchar_t *p = (wchar_t *) GlobalLock(mem);
                    if (p) {
                        GetWindowTextW(edit, p, len + 1);
                        GlobalUnlock(mem);
                        if (OpenClipboard(page)) {
                            EmptyClipboard();
                            SetClipboardData(CF_UNICODETEXT, mem);
                            CloseClipboard();
                            mem = NULL; // Owned by the clipboard now
                        }
                    }
                    if (mem) {
                        GlobalFree(mem);
                    }
                }
                return TRUE;
            }
            break;

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }

        case WM_DESTROY:
            if (mono_font) {
                DeleteObject(mono_font);
                mono_font = NULL;
            }
            break;
    }
    return FALSE;
}

// Preferences: Defaults page - the initial state for new windows/files
static INT_PTR CALLBACK prefs_defaults_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG: {
            HWND enc_combo = GetDlgItem(page, ID_PREF_DEFAULT_ENCODING);
            for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++) {
                wchar_t *name = utf8_to_wide(file_encoding_name((TextEncoding) i));
                if (name) {
                    SendMessageW(enc_combo, CB_ADDSTRING, 0, (LPARAM) name);
                    free(name);
                }
            }
            SendMessageW(enc_combo, CB_SETCURSEL, (WPARAM) settings_get_int("default_encoding", 0),
                         0);

            HWND eol_combo = GetDlgItem(page, ID_PREF_DEFAULT_EOL);
            for (int i = 0; i <= (int) NPAD_EOL_CR; i++) {
                wchar_t *name = utf8_to_wide(file_line_ending_name((LineEnding) i));
                if (name) {
                    SendMessageW(eol_combo, CB_ADDSTRING, 0, (LPARAM) name);
                    free(name);
                }
            }
            SendMessageW(eol_combo, CB_SETCURSEL,
                         (WPARAM) settings_get_int("default_line_ending", 0), 0);

            HWND font_combo = GetDlgItem(page, ID_PREF_DEFAULT_FONT_TYPE);
            SendMessageW(font_combo, CB_ADDSTRING, 0, (LPARAM) L"Monospace");
            SendMessageW(font_combo, CB_ADDSTRING, 0, (LPARAM) L"Proportional");
            SendMessageW(font_combo, CB_SETCURSEL,
                         settings_get_bool("default_font_mono", true) ? 0 : 1, 0);

            SetDlgItemInt(page, ID_PREF_DEFAULT_ZOOM, (UINT) settings_get_int("default_zoom", 100),
                          FALSE);

            CheckDlgButton(page, ID_PREF_AUTO_DEFAULTS,
                           settings_get_bool("auto_update_defaults", false) ? BST_CHECKED
                                                                            : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD code = HIWORD(wparam);
            WORD id = LOWORD(wparam);
            if ((code == CBN_SELCHANGE &&
                 (id == ID_PREF_DEFAULT_ENCODING || id == ID_PREF_DEFAULT_EOL ||
                  id == ID_PREF_DEFAULT_FONT_TYPE)) ||
                (code == EN_CHANGE && id == ID_PREF_DEFAULT_ZOOM) ||
                (code == BN_CLICKED && id == ID_PREF_AUTO_DEFAULTS)) {
                mark_prefs_dirty(page);
            }
            if (id == ID_PREF_USE_CURRENT) {
                // Copy the active window's view state into the fields
                if (g_main_window) {
                    SendDlgItemMessageW(page, ID_PREF_DEFAULT_FONT_TYPE, CB_SETCURSEL,
                                        g_main_window->monospace_current ? 0 : 1, 0);
                    SetDlgItemInt(page, ID_PREF_DEFAULT_ZOOM, (UINT) get_zoom(g_main_window),
                                  FALSE);
                    mark_prefs_dirty(page);
                }
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                LRESULT enc =
                    SendMessageW(GetDlgItem(page, ID_PREF_DEFAULT_ENCODING), CB_GETCURSEL, 0, 0);
                if (enc >= 0) {
                    settings_set_int("default_encoding", (int) enc);
                }
                LRESULT eol =
                    SendMessageW(GetDlgItem(page, ID_PREF_DEFAULT_EOL), CB_GETCURSEL, 0, 0);
                if (eol >= 0) {
                    settings_set_int("default_line_ending", (int) eol);
                }
                LRESULT font_type =
                    SendMessageW(GetDlgItem(page, ID_PREF_DEFAULT_FONT_TYPE), CB_GETCURSEL, 0, 0);
                if (font_type >= 0) {
                    settings_set_bool("default_font_mono", font_type == 0);
                }

                BOOL ok = FALSE;
                UINT zoom = GetDlgItemInt(page, ID_PREF_DEFAULT_ZOOM, &ok, FALSE);
                if (ok) {
                    if (zoom < 10)
                        zoom = 10;
                    if (zoom > 500)
                        zoom = 500;
                    settings_set_int("default_zoom", (int) zoom);
                }

                settings_set_bool("auto_update_defaults",
                                  IsDlgButtonChecked(page, ID_PREF_AUTO_DEFAULTS) == BST_CHECKED);

                settings_save(); // Apply button: persist + propagate immediately
                ui_platform_notify_settings_changed();

                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Preferences: Backup page - settings export/import
static INT_PTR CALLBACK prefs_backup_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wparam) == ID_PREF_EXPORT) {
                export_settings(page);
                return TRUE;
            }
            if (LOWORD(wparam) == ID_PREF_IMPORT) {
                import_settings(page);
                return TRUE;
            }
            if (LOWORD(wparam) == ID_PREF_RESET_DEFAULTS) {
                reset_all_preferences(page);
                return TRUE;
            }
            break;

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                // Nothing page-local to apply; export/import act immediately
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static void show_preferences_dialog(const Window *window, bool show_debug) {
    if (!window)
        return;

    PROPSHEETPAGEW pages[5];
    ZeroMemory(pages, sizeof(pages));

    pages[0].dwSize = sizeof(PROPSHEETPAGEW);
    pages[0].hInstance = g_hinstance;
    pages[0].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_GENERAL);
    pages[0].pfnDlgProc = prefs_general_proc;

    pages[1].dwSize = sizeof(PROPSHEETPAGEW);
    pages[1].hInstance = g_hinstance;
    pages[1].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_APPEARANCE);
    pages[1].pfnDlgProc = prefs_appearance_proc;

    pages[2].dwSize = sizeof(PROPSHEETPAGEW);
    pages[2].hInstance = g_hinstance;
    pages[2].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_DEFAULTS);
    pages[2].pfnDlgProc = prefs_defaults_proc;

    pages[3].dwSize = sizeof(PROPSHEETPAGEW);
    pages[3].hInstance = g_hinstance;
    pages[3].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_BACKUP);
    pages[3].pfnDlgProc = prefs_backup_proc;

    // Hidden diagnostics page: only present when opened via Ctrl+Shift+.
    // or a Shift+click on the Preferences menu item
    pages[4].dwSize = sizeof(PROPSHEETPAGEW);
    pages[4].hInstance = g_hinstance;
    pages[4].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_DEBUG);
    pages[4].pfnDlgProc = prefs_debug_proc;

    PROPSHEETHEADERW psh;
    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    // Include the Apply button so changes can be previewed without closing
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    psh.hwndParent = window->hwnd;
    psh.hInstance = g_hinstance;
    psh.pszCaption = L"Preferences";
    psh.nPages = show_debug ? 5 : 4;
    psh.nStartPage = show_debug ? 4 : 0;
    psh.ppsp = pages;

    PropertySheetW(&psh);

    // Persist all applied preference changes to disk immediately, so they
    // survive even if this run is later killed without a clean exit
    settings_save();

    // Let other running instances pick up the change live
    ui_platform_notify_settings_changed();
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

// (Re)build the accelerator table. The Ctrl+N / Ctrl+Shift+N pair maps to
// "New" vs "New Window" according to the ctrl_n_new_window preference.
static void build_accelerators(Window *window) {
    bool cn_window = settings_get_bool("ctrl_n_new_window", false);
    WORD new_cmd = cn_window ? ID_FILE_NEW_WINDOW : ID_FILE_NEW;
    WORD new_window_cmd = cn_window ? ID_FILE_NEW : ID_FILE_NEW_WINDOW;

    ACCEL accel[] = { { FCONTROL | FVIRTKEY, 'N', new_cmd },
                      { FCONTROL | FSHIFT | FVIRTKEY, 'N', new_window_cmd },
                      { FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN },
                      { FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE },
                      { FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_FILE_SAVE_AS },
                      { FCONTROL | FVIRTKEY, 'W', ID_FILE_CLOSE },
                      { FCONTROL | FSHIFT | FVIRTKEY, 'W', ID_FILE_CLOSE_ALL },
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
                      { FCONTROL | FVIRTKEY, 'M', ID_FORMAT_MONOSPACE },
                      { FCONTROL | FVIRTKEY, 'E', ID_FORMAT_EOL_CYCLE },
                      { FCONTROL | FVIRTKEY, VK_OEM_COMMA, ID_FILE_PREFERENCES },
                      // Hidden: Preferences opened on the Debug diagnostics page
                      { FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_PERIOD, ID_FILE_PREFERENCES_DEBUG },
                      { FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_VIEW_ZOOM_IN },
                      { FCONTROL | FVIRTKEY, VK_ADD, ID_VIEW_ZOOM_IN },
                      { FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_VIEW_ZOOM_OUT },
                      { FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_VIEW_ZOOM_OUT },
                      { FCONTROL | FVIRTKEY, '0', ID_VIEW_ZOOM_RESET },
                      { FCONTROL | FVIRTKEY, VK_NUMPAD0, ID_VIEW_ZOOM_RESET } };

    HACCEL new_table = CreateAcceleratorTableW(accel, sizeof(accel) / sizeof(accel[0]));
    if (!new_table) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "Accelerator table creation",
                           "Failed to create accelerator table - keyboard shortcuts will not work");
        return;
    }
    if (window->haccel) {
        DestroyAcceleratorTable(window->haccel);
    }
    window->haccel = new_table;
}

// Re-apply the ctrl_n_new_window preference to the menu labels and accelerators
static void apply_new_window_pref(Window *window) {
    if (!window || !window->hmenu)
        return;
    bool cn_window = settings_get_bool("ctrl_n_new_window", false);
    ModifyMenuW(window->hmenu, ID_FILE_NEW, MF_BYCOMMAND | MF_STRING, ID_FILE_NEW,
                cn_window ? L"&New\tCtrl+Shift+N" : L"&New\tCtrl+N");
    ModifyMenuW(window->hmenu, ID_FILE_NEW_WINDOW, MF_BYCOMMAND | MF_STRING, ID_FILE_NEW_WINDOW,
                cn_window ? L"New &Window\tCtrl+N" : L"New &Window\tCtrl+Shift+N");
    build_accelerators(window);
}

// Launch a second npad instance (a fresh, independent window)
// Launch another npad instance, optionally with extra command-line
// arguments (already-quoted where needed). extra may be NULL.
static void spawn_self(const wchar_t *extra) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcess may modify the command line buffer, so pass a copy
    wchar_t cmdline[1024];
    if (extra && extra[0]) {
        _snwprintf(cmdline, 1023, L"\"%s\" %s", path, extra);
    } else {
        _snwprintf(cmdline, 1023, L"\"%s\"", path);
    }
    cmdline[1023] = L'\0';

    if (CreateProcessW(path, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static BOOL CALLBACK count_enum_proc(HWND hwnd, LPARAM lparam) {
    wchar_t cls[64];
    if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, NPAD_WINDOW_CLASS) == 0) {
        (*(int *) lparam)++;
    }
    return TRUE;
}

// Number of npad main windows currently open (across all instances)
static int count_npad_windows(void) {
    int count = 0;
    EnumWindows(count_enum_proc, (LPARAM) &count);
    return count;
}

static void launch_new_window(void) {
    // Cascade the new window relative to the windows already open so it does
    // not land exactly on top of them
    wchar_t extra[64];
    _snwprintf(extra, 63, L"--cascade %d", count_npad_windows());
    extra[63] = L'\0';
    spawn_self(extra);
}

void ui_platform_launch_recovery_instance(const char *slot_id, int cascade_index) {
    if (!slot_id)
        return;
    wchar_t *wslot = utf8_to_wide(slot_id);
    if (!wslot)
        return;
    wchar_t extra[160];
    _snwprintf(extra, 159, L"--recover %s --cascade %d", wslot, cascade_index);
    extra[159] = L'\0';
    free(wslot);
    spawn_self(extra);
}

// Post a message to every npad main window (receivers ignore our own pid,
// carried in wParam). More reliable than HWND_BROADCAST.
typedef struct {
    UINT msg;
    LPARAM lparam;
} NpadBroadcast;

static BOOL CALLBACK broadcast_enum_proc(HWND hwnd, LPARAM lparam) {
    const NpadBroadcast *bc = (const NpadBroadcast *) lparam;
    wchar_t cls[64];
    if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, NPAD_WINDOW_CLASS) == 0) {
        PostMessageW(hwnd, bc->msg, (WPARAM) GetCurrentProcessId(), bc->lparam);
    }
    return TRUE;
}

static void broadcast_to_npad_windows(UINT msg, LPARAM lparam) {
    NpadBroadcast bc = { msg, lparam };
    EnumWindows(broadcast_enum_proc, (LPARAM) &bc);
}

void ui_platform_notify_settings_changed(void) {
    if (g_settings_changed_msg) {
        broadcast_to_npad_windows(g_settings_changed_msg, 0);
    }
}

bool ui_platform_pid_is_running(long pid) {
    if (pid <= 0)
        return false;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (!proc) {
        return false; // No such process (or gone) - treat as not running
    }

    bool running = false;
    DWORD exit_code = 0;
    if (GetExitCodeProcess(proc, &exit_code) && exit_code == STILL_ACTIVE) {
        // Confirm it is actually npad.exe, guarding against pid reuse
        wchar_t image[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(proc, 0, image, &size)) {
            const wchar_t *base = wcsrchr(image, L'\\');
            base = base ? base + 1 : image;
            running = (_wcsicmp(base, L"npad.exe") == 0);
        }
    }
    CloseHandle(proc);
    return running;
}

static void create_menu(Window *window) {
    HMENU hmenu = CreateMenu();
    HMENU hfile = CreatePopupMenu();
    HMENU hrecent = CreatePopupMenu();
    HMENU hedit = CreatePopupMenu();
    HMENU hformat = CreatePopupMenu();
    HMENU heol = CreatePopupMenu();
    HMENU hview = CreatePopupMenu();
    HMENU hzoom = CreatePopupMenu();
    HMENU hhelp = CreatePopupMenu();

    if (!hmenu || !hfile || !hrecent || !hedit || !hformat || !heol || !hview || !hzoom || !hhelp) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu creation",
                         "Failed to create menu components");
        return;
    }

    // File menu
    bool cn_window = settings_get_bool("ctrl_n_new_window", false);
    AppendMenuW(hfile, MF_STRING, ID_FILE_NEW, cn_window ? L"&New\tCtrl+Shift+N" : L"&New\tCtrl+N");
    AppendMenuW(hfile, MF_STRING, ID_FILE_NEW_WINDOW,
                cn_window ? L"New &Window\tCtrl+N" : L"New &Window\tCtrl+Shift+N");
    AppendMenuW(hfile, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hfile, MF_STRING, ID_FILE_SAVE_AS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING | MF_POPUP, (UINT_PTR) hrecent, L"&Recent Files");
    AppendMenuW(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hfile, MF_STRING, ID_FILE_CLOSE, L"&Close\tCtrl+W");
    AppendMenuW(hfile, MF_STRING, ID_FILE_CLOSE_ALL, L"Close A&ll Windows\tCtrl+Shift+W");
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
    AppendMenuW(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hedit, MF_STRING, ID_FILE_PREFERENCES, L"&Preferences...\tCtrl+,");

    // Format menu
    AppendMenuW(hformat, MF_STRING, ID_FORMAT_WORD_WRAP, L"&Word Wrap\tAlt+Z");
    AppendMenuW(hformat, MF_STRING, ID_FORMAT_MONOSPACE, L"&Monospace\tCtrl+M");
    AppendMenuW(hformat, MF_SEPARATOR, 0, NULL);
    AppendMenuW(heol, MF_STRING, ID_FORMAT_EOL_CRLF, L"&Windows (CRLF)");
    AppendMenuW(heol, MF_STRING, ID_FORMAT_EOL_LF, L"&Unix (LF)");
    AppendMenuW(heol, MF_STRING, ID_FORMAT_EOL_CR, L"&Mac (CR)");
    AppendMenuW(hformat, MF_STRING | MF_POPUP, (UINT_PTR) heol, L"&Line Endings");
    AppendMenuW(hformat, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hformat, MF_STRING, ID_FORMAT_FONT, L"&Font...");

    // View menu
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_IN, L"Zoom &In\tCtrl+Plus");
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_OUT, L"Zoom &Out\tCtrl+Minus");
    AppendMenuW(hzoom, MF_STRING, ID_VIEW_ZOOM_RESET, L"&Restore Default Zoom\tCtrl+0");
    AppendMenuW(hview, MF_STRING | MF_POPUP, (UINT_PTR) hzoom, L"&Zoom");
    AppendMenuW(hview, MF_STRING, ID_VIEW_STATUS_BAR, L"&Status Bar");
    // Theme (incl. Solarized) is chosen in Preferences > Appearance; a plain
    // View > Dark Mode toggle would clobber a selected Solarized scheme.

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

// Enable/disable edit commands in the given menu to match the current
// state, like Notepad. Shared by the menu bar and the context menu.
static void apply_edit_command_states(Window *window, HMENU menu) {
    if (!window || !menu || !window->edit_hwnd)
        return;

    bool has_selection = ui_platform_has_selection(window);
    bool has_text = SendMessageW(window->edit_hwnd, WM_GETTEXTLENGTH, 0, 0) > 0;
    bool can_paste =
        IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT);

    UINT enabled = MF_BYCOMMAND | MF_ENABLED;
    UINT disabled = MF_BYCOMMAND | MF_GRAYED;

    EnableMenuItem(menu, ID_EDIT_UNDO, ui_platform_can_undo(window) ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_REDO, ui_platform_can_redo(window) ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_CUT, has_selection ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_COPY, has_selection ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_DELETE, has_selection ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_PASTE, can_paste ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND, has_text ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND_NEXT,
                   (has_text && g_search_text[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND_PREV,
                   (has_text && g_search_text[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_REPLACE, has_text ? enabled : disabled);
}

// Refresh enable states and checkmarks across the whole menu bar
static void update_menu_states(Window *window) {
    if (!window || !window->hmenu)
        return;

    apply_edit_command_states(window, window->hmenu);

    CheckMenuItem(window->hmenu, ID_FORMAT_MONOSPACE,
                  window->monospace_current ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuRadioItem(window->hmenu, ID_FORMAT_EOL_CRLF, ID_FORMAT_EOL_CR,
                       ID_FORMAT_EOL_CRLF + (UINT) editor_get_line_ending(), MF_BYCOMMAND);
}

// Right-click context menu for the edit control (Notepad-style)
static void show_context_menu(Window *window, int x, int y) {
    if (!window || !window->edit_hwnd)
        return;

    // Keyboard-invoked (Shift+F10): position at the caret
    if (x == -1 && y == -1) {
        CHARRANGE sel = { 0, 0 };
        SendMessageW(window->edit_hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);
        POINTL pt = { 0, 0 };
        SendMessageW(window->edit_hwnd, EM_POSFROMCHAR, (WPARAM) &pt, sel.cpMin);
        POINT screen = { pt.x, pt.y };
        ClientToScreen(window->edit_hwnd, &screen);
        x = screen.x;
        y = screen.y;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING, ID_EDIT_UNDO, L"&Undo");
    AppendMenuW(menu, MF_STRING, ID_EDIT_REDO, L"&Redo");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EDIT_CUT, L"Cu&t");
    AppendMenuW(menu, MF_STRING, ID_EDIT_COPY, L"&Copy");
    AppendMenuW(menu, MF_STRING, ID_EDIT_PASTE, L"&Paste");
    AppendMenuW(menu, MF_STRING, ID_EDIT_DELETE, L"&Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EDIT_SELECT_ALL, L"Select &All");

    apply_edit_command_states(window, menu);

    TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, window->hwnd, NULL);
    DestroyMenu(menu);
}

// Popup for the status bar's line-ending part (also mirrors Format > Line Endings)
static void show_line_ending_popup(Window *window) {
    if (!window)
        return;

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING, ID_FORMAT_EOL_CRLF, L"&Windows (CRLF)");
    AppendMenuW(menu, MF_STRING, ID_FORMAT_EOL_LF, L"&Unix (LF)");
    AppendMenuW(menu, MF_STRING, ID_FORMAT_EOL_CR, L"&Mac (CR)");
    CheckMenuRadioItem(menu, ID_FORMAT_EOL_CRLF, ID_FORMAT_EOL_CR,
                       ID_FORMAT_EOL_CRLF + (UINT) editor_get_line_ending(), MF_BYCOMMAND);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, window->hwnd, NULL);
    DestroyMenu(menu);
}

// Popup for the status bar's encoding part
static void show_encoding_popup(Window *window) {
    if (!window)
        return;

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    for (int i = 0; i <= (int) NPAD_ENC_ANSI; i++) {
        wchar_t *name = utf8_to_wide(file_encoding_name((TextEncoding) i));
        if (name) {
            AppendMenuW(menu, MF_STRING, ID_ENCODING_BASE + (UINT) i, name);
            free(name);
        }
    }
    CheckMenuRadioItem(menu, ID_ENCODING_BASE, ID_ENCODING_BASE + (UINT) NPAD_ENC_ANSI,
                       ID_ENCODING_BASE + (UINT) editor_get_encoding(), MF_BYCOMMAND);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, window->hwnd, NULL);
    DestroyMenu(menu);
}

// Toggle status bar visibility (shared by the View menu and Preferences)
static void set_status_bar_visible(Window *window, bool visible) {
    if (!window)
        return;

    window->status_bar_visible = visible;
    settings_set_bool("status_bar_visible", visible);
    ShowWindow(window->status_hwnd, visible ? SW_SHOW : SW_HIDE);
    resize_controls(window);
    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_VIEW_STATUS_BAR, visible ? MF_CHECKED : MF_UNCHECKED);
    }
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

    // Encoding entries from the status bar popup
    if (command >= ID_ENCODING_BASE && command <= ID_ENCODING_BASE + (WORD) NPAD_ENC_ANSI) {
        editor_set_encoding((TextEncoding) (command - ID_ENCODING_BASE));
        return;
    }

    switch (command) {
        case ID_FILE_NEW:
            ui_post_event(UI_EVENT_FILE_NEW, window, NULL);
            break;
        case ID_FILE_NEW_WINDOW:
            launch_new_window();
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
        case ID_FILE_PREFERENCES:
            // Shift+click reveals the hidden Debug diagnostics page
            show_preferences_dialog(window, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            break;
        case ID_FILE_PREFERENCES_DEBUG: // Ctrl+Shift+. accelerator
            show_preferences_dialog(window, true);
            break;
        case ID_FILE_CLOSE:
            // One document per window, so "close" is the window's save-checked
            // close path (same as Exit for this instance)
            PostMessageW(window->hwnd, WM_CLOSE, 0, 0);
            break;
        case ID_FILE_CLOSE_ALL:
            // Every npad window (this one included) gets a close request and
            // runs its own save prompt; a Cancel leaves that window open
            if (g_close_all_msg) {
                broadcast_to_npad_windows(g_close_all_msg, 0);
            } else {
                PostMessageW(window->hwnd, WM_CLOSE, 0, 0);
            }
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
            settings_save(); // Persist and propagate to other windows
            ui_platform_notify_settings_changed();
            break;
        case ID_FORMAT_MONOSPACE:
            // Per-window view state: affects only this window unless the
            // sync/auto-default preferences say otherwise
            window->monospace_current = !window->monospace_current;
            apply_font(window);
            update_menu_states(window);
            update_status_bar(window);
            on_view_state_changed(window);
            break;
        case ID_FORMAT_EOL_CRLF:
        case ID_FORMAT_EOL_LF:
        case ID_FORMAT_EOL_CR:
            editor_set_line_ending((LineEnding) (command - ID_FORMAT_EOL_CRLF));
            break;
        case ID_FORMAT_EOL_CYCLE:
            editor_set_line_ending((LineEnding) ((editor_get_line_ending() + 1) % 3));
            break;
        case ID_FORMAT_FONT: {
            const char *def = DEFAULT_MONO_FONT;
            const char *key = active_font_key(window, &def);
            show_font_dialog(window, key, def);
            break;
        }
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
            set_status_bar_visible(window, !window->status_bar_visible);
            settings_save(); // Persist and propagate to other windows
            ui_platform_notify_settings_changed();
            break;
        case ID_HELP_ABOUT:
            ui_platform_show_about_dialog(window);
            break;
    }
}

// Send part text only when it changed since the last send: scrolling and
// caret movement hammer this path, and redundant SB_SETTEXT repaints were a
// visible drag when holding a scroll key on large documents
static void set_status_part(Window *window, int part, const wchar_t *text) {
    if (!text || part < 0 || part > 5)
        return;
    if (wcsncmp(window->status_cache[part], text, 63) == 0)
        return;
    wcsncpy(window->status_cache[part], text, 63);
    window->status_cache[part][63] = L'\0';
    SendMessageW(window->status_hwnd, SB_SETTEXTW, (WPARAM) part, (LPARAM) text);
}

static void update_status_bar(Window *window) {
    if (!window || !window->status_hwnd || !window->edit_hwnd || !window->status_bar_visible)
        return;

    int line = 0, column = 0;
    ui_platform_get_cursor_line_column(window, &line, &column);

    wchar_t text[128];

    _snwprintf(text, 127, L"Ln %d, Col %d", line, column);
    text[127] = L'\0';
    set_status_part(window, 1, text);

    _snwprintf(text, 127, L"%d%%", get_zoom(window));
    text[127] = L'\0';
    set_status_part(window, 2, text);

    set_status_part(window, 3, window->monospace_current ? L"Mono" : L"Prop");

    wchar_t *eol = utf8_to_wide(window->status_line_ending);
    if (eol) {
        set_status_part(window, 4, eol);
        free(eol);
    }

    wchar_t *encoding = utf8_to_wide(window->status_encoding);
    if (encoding) {
        set_status_part(window, 5, encoding);
        free(encoding);
    }
}

// Coalesce status-bar refreshes from hot paths (scrolling, caret movement):
// arm a short one-shot timer instead of recomputing Ln/Col synchronously on
// every wheel notch / key repeat. At most ~30 refreshes/second.
static void schedule_status_update(Window *window) {
    if (!window || !window->hwnd || window->status_update_pending)
        return;
    window->status_update_pending = true;
    SetTimer(window->hwnd, NPAD_STATUS_TIMER_ID, 33, NULL);
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

        // Status bar parts, scaled for DPI. Right-anchored widths:
        // encoding 90, EOL 120 (fits "Windows (CRLF)"), font 55, zoom 55,
        // Ln/Col 120; the message part takes the remaining space.
        // [message][Ln,Col][zoom][Mono/Prop][EOL][encoding]
        int dpi = (int) get_window_dpi(window->hwnd);
        int width = rect.right;
        int parts[6];
        parts[5] = width;
        parts[4] = width - MulDiv(90, dpi, 96);
        parts[3] = width - MulDiv(210, dpi, 96);
        parts[2] = width - MulDiv(265, dpi, 96);
        parts[1] = width - MulDiv(320, dpi, 96);
        parts[0] = width - MulDiv(440, dpi, 96);
        SendMessageW(window->status_hwnd, SB_SETPARTS, 6, (LPARAM) parts);
        update_status_bar(window);
    }

    SetWindowPos(window->edit_hwnd, NULL, 0, 0, rect.right, rect.bottom - status_height,
                 SWP_NOZORDER);
}

// Reload settings from disk and re-apply everything that can change live.
// Used when another instance saved preferences.
static void reload_and_apply_settings(Window *window) {
    if (!window)
        return;
    settings_clear_all();
    settings_load();
    editor_reload_prefs(); // Re-read auto-save / session settings and timers
    apply_font(window);
    apply_theme(window);
    apply_new_window_pref(window);
    rebuild_recent_menu(window);

    // Sync shared window options; per-window view state (font type, zoom)
    // is deliberately left alone
    bool wrap = settings_get_bool("word_wrap", false);
    if (wrap != window->word_wrap_enabled) {
        window->word_wrap_enabled = wrap;
        apply_word_wrap(window);
    }
    bool status = settings_get_bool("status_bar_visible", true);
    if (status != window->status_bar_visible) {
        set_status_bar_visible(window, status);
    }

    update_menu_states(window);
    update_status_bar(window);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window *window = (Window *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    // Another instance changed settings on disk: reload and re-apply them.
    // Ignore our own broadcast (wParam carries the sender's pid).
    if (msg == g_settings_changed_msg && g_settings_changed_msg != 0) {
        if (window && wparam != (WPARAM) GetCurrentProcessId()) {
            reload_and_apply_settings(window);
        }
        return 0;
    }

    // Close All Windows: every npad window (including the sender) closes via
    // its own save-checked WM_CLOSE path, so a Cancel keeps that window open.
    if (msg == g_close_all_msg && g_close_all_msg != 0) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }

    // Another instance changed its view state (font type / zoom) and view
    // sync is enabled: mirror it here without re-broadcasting.
    if (msg == g_view_sync_msg && g_view_sync_msg != 0) {
        if (window && wparam != (WPARAM) GetCurrentProcessId() &&
            settings_get_bool("sync_view_state", false)) {
            int zoom = LOWORD(lparam);
            bool mono = HIWORD(lparam) != 0;
            if (zoom >= 10 && zoom <= 500) {
                SendMessageW(window->edit_hwnd, EM_SETZOOM, (WPARAM) zoom, 100);
            }
            if (window->monospace_current != mono) {
                window->monospace_current = mono;
                apply_font(window);
            }
            update_menu_states(window);
            update_status_bar(window);
        }
        return 0;
    }

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
                const NMHDR *nmhdr = (const NMHDR *) lparam;
                if (nmhdr->idFrom == ID_EDIT_CONTROL && nmhdr->code == EN_SELCHANGE) {
                    // Fires per caret step during keyboard scrolling and per
                    // character while selecting - coalesce, never recompute
                    // synchronously here
                    g_selchange_count++;
                    schedule_status_update(window);
                } else if (nmhdr->hwndFrom == window->status_hwnd && nmhdr->code == NM_CLICK) {
                    // Status bar part clicks: Ln/Col -> Go To, zoom -> reset,
                    // font mode -> toggle monospace, line ending / encoding -> pickers
                    const NMMOUSE *mouse = (const NMMOUSE *) lparam;
                    switch ((int) mouse->dwItemSpec) {
                        case 1:
                            show_goto_dialog(window);
                            break;
                        case 2:
                            set_zoom(window, 100);
                            break;
                        case 3:
                            handle_command(window, ID_FORMAT_MONOSPACE);
                            update_status_bar(window);
                            break;
                        case 4:
                            show_line_ending_popup(window);
                            break;
                        case 5:
                            show_encoding_popup(window);
                            break;
                    }
                }
            }
            break;
        }

        case WM_CONTEXTMENU: {
            if (window && (HWND) wparam == window->edit_hwnd) {
                show_context_menu(window, (int) (short) LOWORD(lparam),
                                  (int) (short) HIWORD(lparam));
                return 0;
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP) wparam;
            wchar_t path[MAX_PATH];
            if (window && DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
                char *utf8 = wide_to_utf8(path);
                if (utf8) {
                    if (GetKeyState(VK_CONTROL) & 0x8000) {
                        // Ctrl+Drop inserts the file contents at the caret
                        TextFileInfo info;
                        char *content = file_read_text_ex(utf8, &info);
                        if (content) {
                            wchar_t *wide = utf8_to_wide(content);
                            if (wide) {
                                SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) wide);
                                free(wide);
                            }
                            free(content);
                        }
                    } else {
                        ui_post_event(UI_EVENT_FILE_DROPPED, window, utf8);
                    }
                    free(utf8);
                }
            }
            DragFinish(drop);
            return 0;
        }

        case WM_TIMER: {
            if (window && wparam == NPAD_AUTO_SAVE_TIMER_ID) {
                ui_post_event(UI_EVENT_AUTO_SAVE, window, NULL);
            } else if (window && wparam == NPAD_SESSION_TIMER_ID) {
                ui_post_event(UI_EVENT_SESSION_SNAPSHOT, window, NULL);
            } else if (window && wparam == NPAD_STARTUP_TIMER_ID) {
                KillTimer(hwnd, NPAD_STARTUP_TIMER_ID); // One-shot
                startup_prof_mark("deferred tasks");
                ui_post_event(UI_EVENT_STARTUP_DEFERRED, window, NULL);
            } else if (window && wparam == NPAD_STATUS_TIMER_ID) {
                KillTimer(hwnd, NPAD_STATUS_TIMER_ID); // One-shot (coalescing)
                window->status_update_pending = false;
                update_status_bar(window);
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
                // Char formats are in twips (DPI-independent), so existing
                // runs - including font-bound emoji - keep their formatting;
                // only the default needs a refresh
                apply_font_default(window);
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
