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
#include <winhttp.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shlobj.h>

#include "../ui_interface.h"
#include "../main.h"
#include "../core/editor.h"
#include "../core/error.h"
#include "../core/file_ops.h"
#include "../core/list_ops.h"
#include "../core/session.h"
#include "../core/settings.h"
#include "../core/startup_prof.h"
#include "../core/update_check.h"
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
#define NPAD_COUNTS_TIMER_ID 5
#define NPAD_HIGHLIGHT_TIMER_ID 6

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
// Markdown list tools (optional; shown when list_tools_enabled)
#define ID_LIST_SORT_ASC 2114
#define ID_LIST_SORT_DESC 2115
#define ID_LIST_SORT_CASE 2116
#define ID_LIST_UNIQUE 2117
#define ID_LIST_CONVERT_DELIM 2118
#define ID_LIST_INDENT 2119      // Ctrl+], default format
#define ID_LIST_UNINDENT 2120    // Ctrl+[
#define ID_LIST_INDENT_BASE 2121 // 2121..2127, one per ListIndentFormat (2127 = Custom, prompts)
#define ID_HELP_ABOUT 2401
#define ID_HELP_CHECK_UPDATES 2402
#define ID_HELP_UPDATE_AVAILABLE 2403

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
    bool list_menu_present;      // The optional top-level Markdown menu is inserted
    bool line_cut_pending;       // Last Ctrl+X was a whole-line cut (paste-above mode)
    DWORD line_cut_clip_seq;     // Clipboard sequence number right after that cut
    bool update_item_present;    // The dynamic "Update Available" Help item is inserted
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
// Effective (escape-interpreted, '\r'-normalized) forms actually searched
// for / inserted; the raw buffers above are what the dialog shows
static wchar_t g_search_eff[256] = L"";
static wchar_t g_replace_eff[256] = L"";
static bool g_match_case = false;
static bool g_whole_word = false;
static bool g_search_down = true;
static bool g_wrap_around = true;
static bool g_interpret_escapes = false;  // Interpret \n \t \\ \uXXXX in find/replace
static bool g_highlight_all = false;      // Highlight every match while the dialog is open
static bool g_highlights_applied = false; // Highlight overlay currently has matches

// Highlight-all overlay match list, sorted ascending (drawn over the text in
// the WM_PAINT hook; the control's plain-text mode rejects per-range
// character formatting, so highlighting cannot use EM_SETCHARFORMAT)
static CHARRANGE *g_hl_matches = NULL;
static int g_hl_count = 0;
static int g_hl_capacity = 0;

// A consumed WM_KEYDOWN (Tab indent, Enter list continuation) must also eat
// the WM_CHAR that TranslateMessage already posted for the same keystroke.
// The matching Tab KEYUP is eaten too so the control never sees an orphan
// key-up for a keystroke it knows nothing about.
static bool g_swallow_tab_char = false;
static bool g_swallow_return_char = false;
static bool g_swallow_tab_keyup = false;

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
static void apply_list_tools_menu(Window *window);
static void show_convert_delim_dialog(Window *window);
static bool show_custom_indent_dialog(Window *window);
static void list_do(const Window *window, int op, int arg);
static void list_indent_default(Window *window, bool unindent);
static char *get_custom_indent(void);
static void apply_new_window_pref(Window *window);
static void launch_new_window(void);
static void handle_command(Window *window, WORD command);
static void update_status_bar(Window *window);
static void schedule_status_update(Window *window);
static void schedule_counts_update(Window *window);
static void apply_counts_pref(Window *window);
static void draw_highlight_overlay(HWND e, HRGN clip);
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
static void show_preferences_dialog(const Window *window, int start_page, bool show_debug);
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
bool ui_platform_has_selection(Window *window);

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

// Every dialog opens at this notepad-style offset into its owner window
// (96-DPI units, scaled per monitor) rather than centred or at the screen's
// top-left. Matches where classic Notepad drops its Find dialog.
#define NPAD_DIALOG_OFFSET_X 84
#define NPAD_DIALOG_OFFSET_Y 185

// Place a dialog at the standard offset into its owner window, clamped to
// the owner's monitor work area.
static void position_dialog_on_owner(HWND dlg) {
    HWND owner = GetWindow(dlg, GW_OWNER);
    if (!owner)
        owner = GetParent(dlg);
    if (!owner)
        return;
    RECT ro, rd;
    if (!GetWindowRect(owner, &ro) || !GetWindowRect(dlg, &rd))
        return;
    int w = rd.right - rd.left;
    int h = rd.bottom - rd.top;
    int dpi = (int) get_window_dpi(owner);
    int x = ro.left + MulDiv(NPAD_DIALOG_OFFSET_X, dpi, 96);
    int y = ro.top + MulDiv(NPAD_DIALOG_OFFSET_Y, dpi, 96);

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + w > mi.rcWork.right)
            x = mi.rcWork.right - w;
        if (y + h > mi.rcWork.bottom)
            y = mi.rcWork.bottom - h;
        if (x < mi.rcWork.left)
            x = mi.rcWork.left;
        if (y < mi.rcWork.top)
            y = mi.rcWork.top;
    }
    SetWindowPos(dlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Precise character-position length of the control's text (RichEdit counts
// each paragraph break as one '\r'; WM_GETTEXTLENGTH overcounts CRLF pairs).
static LONG edit_text_cp_length(HWND e) {
    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200; // UTF-16
    return (LONG) SendMessageW(e, EM_GETTEXTLENGTHEX, (WPARAM) &gtl, 0);
}

// Bounds of the logical line (paragraph) containing pos, excluding its break.
// EM_LINEINDEX/EM_LINELENGTH work on *display* lines when word wrap is on, so
// walk the actual '\r' breaks via EM_FINDTEXTEXW instead. Returns true when a
// break follows the line (i.e. it is not the last line of the document).
static bool get_paragraph_bounds(HWND e, LONG pos, LONG *start, LONG *end) {
    FINDTEXTEXW ft;
    ft.lpstrText = L"\r";
    ft.chrgText.cpMin = -1;
    ft.chrgText.cpMax = -1;

    // Backward over [0, pos) for the previous break
    ft.chrg.cpMin = pos;
    ft.chrg.cpMax = 0;
    LRESULT hit = SendMessageW(e, EM_FINDTEXTEXW, 0, (LPARAM) &ft);
    *start = (hit >= 0) ? ft.chrgText.cpMax : 0;

    // Forward from pos for the next break
    ft.chrg.cpMin = pos;
    ft.chrg.cpMax = -1;
    hit = SendMessageW(e, EM_FINDTEXTEXW, FR_DOWN, (LPARAM) &ft);
    if (hit >= 0) {
        *end = ft.chrgText.cpMin;
        return true;
    }
    *end = edit_text_cp_length(e);
    return false;
}

// Tab/Shift+Tab indent handling (Markdown tools, default binding). Returns
// true when the keystroke was consumed.
static bool handle_markdown_tab(HWND hwnd) {
    if (!g_main_window || hwnd != g_main_window->edit_hwnd)
        return false;
    if (!settings_get_bool("list_tools_enabled", false) ||
        settings_get_bool("list_indent_shortcut_brackets", false))
        return false;
    if (GetKeyState(VK_CONTROL) < 0)
        return false; // Ctrl+Tab is not ours
    bool shift = GetKeyState(VK_SHIFT) < 0;
    // Arm the swallow flag BEFORE acting: list_indent_default can open the
    // modal Custom Indent dialog, whose message loop would otherwise dispatch
    // the already-queued WM_CHAR '\t' to this control (inserting a stray tab)
    // and leave the flag armed for a later, unrelated Tab. Set first so the
    // pending WM_CHAR is swallowed whether it arrives during the modal loop or
    // back in the outer loop.
    if (shift) {
        // Shift+Tab always unindents: the selected lines, or the caret line
        // when nothing is selected (a literal tab is never what it means)
        g_swallow_tab_char = true;
        list_indent_default(g_main_window, true);
        return true;
    }
    if (ui_platform_has_selection(g_main_window)) {
        g_swallow_tab_char = true;
        list_indent_default(g_main_window, false);
        return true;
    }
    return false; // Plain Tab types a tab, classic Notepad behavior
}

// Length of the bullet marker at s: the custom marker body first, then the
// built-in "* " / "- ". allow_bare also accepts the space-less marker when
// the text ends right after it (empty bullets, pre-0.13 documents).
static LONG wide_marker_at(const wchar_t *s, LONG len, const wchar_t *custom_body,
                           bool allow_bare) {
    if (custom_body && custom_body[0]) {
        LONG bl = (LONG) wcslen(custom_body);
        if (len >= bl && wcsncmp(s, custom_body, bl) == 0)
            return bl;
        if (allow_bare) {
            LONG tl = bl;
            while (tl > 0 && (custom_body[tl - 1] == L' ' || custom_body[tl - 1] == L'\t'))
                tl--;
            if (tl > 0 && len == tl && wcsncmp(s, custom_body, tl) == 0)
                return tl;
        }
    }
    if (len >= 2 && (s[0] == L'*' || s[0] == L'-') && s[1] == L' ')
        return 2;
    if (allow_bare && len == 1 && (s[0] == L'*' || s[0] == L'-'))
        return 1;
    return 0;
}

// The custom indent prefix's marker body as a wide string (leading
// whitespace stripped), or NULL when unset/whitespace-only. Caller frees.
static wchar_t *get_custom_marker_body(void) {
    char *custom = get_custom_indent();
    if (!custom)
        return NULL;
    const char *body8 = custom;
    while (*body8 == ' ' || *body8 == '\t')
        body8++;
    wchar_t *wbody = (*body8) ? utf8_to_wide(body8) : NULL;
    free(custom);
    return wbody;
}

// Enter on a list line continues the marker on the new line; Enter on an
// empty bullet removes the marker and inserts a plain newline (ends the
// list). Returns true when the keystroke was consumed.
static bool handle_markdown_return(HWND hwnd) {
    if (!g_main_window || hwnd != g_main_window->edit_hwnd)
        return false;
    if (!settings_get_bool("list_tools_enabled", false))
        return false;
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0)
        return false;

    CHARRANGE sel = { 0, 0 };
    SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM) &sel);
    if (sel.cpMin != sel.cpMax)
        return false; // Selection: stock replace-selection Enter

    LONG start, end;
    get_paragraph_bounds(hwnd, sel.cpMin, &start, &end);
    if (end <= start)
        return false;

    // Fetch the head of the line; any real prefix fits well within this
    LONG fetch_end = (end - start > 256) ? start + 256 : end;
    wchar_t buf[257];
    TEXTRANGEW tr;
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = fetch_end;
    tr.lpstrText = buf;
    LONG got = (LONG) SendMessageW(hwnd, EM_GETTEXTRANGE, 0, (LPARAM) &tr);
    if (got <= 0)
        return false;
    buf[got] = L'\0';

    // Parse leading whitespace, then a full marker (its trailing space
    // included, so a lone "-" the user just typed is left alone)
    LONG ws = 0;
    while (ws < got && (buf[ws] == L' ' || buf[ws] == L'\t'))
        ws++;
    wchar_t *wbody = get_custom_marker_body();
    LONG marker_len = wide_marker_at(buf + ws, got - ws, wbody, false);
    if (marker_len == 0) {
        free(wbody);
        return false;
    }

    LONG prefix_len = ws + marker_len;
    LONG caret = sel.cpMin;
    if (caret < start + prefix_len) {
        free(wbody);
        return false; // Caret inside the prefix: plain Enter
    }

    bool handled = true;
    if (end - start == prefix_len) {
        // Empty bullet: end the list - drop the marker, keep the newline
        // (single EM_REPLACESEL, one undo unit)
        CHARRANGE r = { start, caret };
        SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM) &r);
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM) L"\r");
    } else {
        // Splitting right before an existing bullet must not mint another
        // marker ("- item |- second" would become "- - second"): the text
        // after the caret already starts its own bullet, so let the plain
        // newline through instead
        bool rem_bullet = false;
        LONG rem_end = (end - caret > 64) ? caret + 64 : end;
        if (rem_end > caret) {
            wchar_t rbuf[65];
            TEXTRANGEW rtr;
            rtr.chrg.cpMin = caret;
            rtr.chrg.cpMax = rem_end;
            rtr.lpstrText = rbuf;
            LONG rgot = (LONG) SendMessageW(hwnd, EM_GETTEXTRANGE, 0, (LPARAM) &rtr);
            if (rgot > 0) {
                rbuf[rgot] = L'\0';
                LONG rws = 0;
                while (rws < rgot && (rbuf[rws] == L' ' || rbuf[rws] == L'\t'))
                    rws++;
                LONG m = wide_marker_at(rbuf + rws, rgot - rws, wbody, rem_end == end);
                // A match that exactly fills a truncated fetch window is
                // inconclusive; trust it only when we saw the real line end
                rem_bullet = m > 0 && (rws + m < rgot || rem_end == end);
            }
        }
        if (rem_bullet) {
            handled = false;
        } else {
            // Continue the list: newline + the same whitespace/marker prefix
            // (splits the line when the caret is mid-content)
            wchar_t ins[260];
            ins[0] = L'\r';
            memcpy(ins + 1, buf, (size_t) prefix_len * sizeof(wchar_t));
            ins[1 + prefix_len] = L'\0';
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM) ins);
        }
    }
    free(wbody);
    if (!handled)
        return false;
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    g_swallow_return_char = true;
    return true;
}

// Subclass for the edit control: refresh the status bar after events that
// EN_SELCHANGE does not reliably cover (native Ctrl+wheel zoom, caret
// movement onto a new empty line, mouse-driven caret moves)
static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR id, DWORD_PTR ref) {
    (void) ref;

    // Markdown keyboard handling runs BEFORE the default proc so consumed
    // keys never reach the control. A consumed key still has its WM_CHAR
    // already queued by TranslateMessage, so that gets eaten too.
    if (msg == WM_KEYDOWN) {
        if (wparam == VK_TAB && handle_markdown_tab(hwnd)) {
            g_swallow_tab_keyup = true;
            return 0;
        }
        if (wparam == VK_RETURN && handle_markdown_return(hwnd)) {
            if (g_main_window)
                schedule_status_update(g_main_window);
            return 0;
        }
    } else if (msg == WM_KEYUP) {
        if (wparam == VK_TAB && g_swallow_tab_keyup) {
            g_swallow_tab_keyup = false;
            if (g_main_window)
                schedule_status_update(g_main_window);
            return 0;
        }
    } else if (msg == WM_CHAR) {
        if (wparam == L'\t' && g_swallow_tab_char) {
            g_swallow_tab_char = false;
            return 0;
        }
        if (wparam == L'\r' && g_swallow_return_char) {
            g_swallow_return_char = false;
            return 0;
        }
    }

    LRESULT result;
    if (msg == WM_PAINT) {
        // Capture the update region before RichEdit validates it, so the
        // highlight overlay can be drawn clipped to exactly what repainted
        HRGN update_rgn = NULL;
        if (g_hl_count > 0) {
            update_rgn = CreateRectRgn(0, 0, 0, 0);
            if (update_rgn && GetUpdateRgn(hwnd, update_rgn, FALSE) == NULLREGION) {
                DeleteObject(update_rgn);
                update_rgn = NULL;
            }
        }
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
        if (update_rgn) {
            draw_highlight_overlay(hwnd, update_rgn);
            DeleteObject(update_rgn);
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
    g_interpret_escapes = settings_get_bool("find_interpret_escapes", false);
    g_highlight_all = settings_get_bool("find_highlight_all", false);

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
    // (falling back to the pre-0.7 "monospace_enabled" key for migration).
    // Proportional by default, mirroring classic Notepad out of the box.
    window->monospace_current =
        settings_get_bool("default_font_mono", settings_get_bool("monospace_enabled", false));

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
        schedule_counts_update(window); // Initial counts (when enabled)
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

// Read CF_UNICODETEXT from the clipboard (malloc'd copy; NULL if unavailable)
static wchar_t *read_clipboard_text(HWND owner) {
    if (!OpenClipboard(owner))
        return NULL;
    wchar_t *copy = NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *src = GlobalLock(h);
        if (src) {
            size_t len = wcslen(src);
            copy = malloc((len + 1) * sizeof(wchar_t));
            if (copy)
                memcpy(copy, src, (len + 1) * sizeof(wchar_t));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return copy;
}

void ui_platform_cut(Window *window) {
    if (!window || !window->edit_hwnd)
        return;
    HWND e = window->edit_hwnd;
    window->line_cut_pending = false;
    if (settings_get_bool("list_tools_enabled", false) && !ui_platform_has_selection(window)) {
        // Markdown tools: Ctrl+X with no selection cuts the whole logical
        // line, including its line break, onto the clipboard
        CHARRANGE sel = { 0, 0 };
        SendMessageW(e, EM_EXGETSEL, 0, (LPARAM) &sel);
        LONG start, end;
        bool has_break = get_paragraph_bounds(e, sel.cpMin, &start, &end);
        if (has_break)
            end++; // Consume the following '\r' (one cp in RichEdit)
        else if (start > 0)
            start--; // Last line: consume the preceding break instead
        if (start == end)
            return; // Empty document: nothing to cut, clipboard untouched
        CHARRANGE line = { start, end };
        SendMessageW(e, EM_EXSETSEL, 0, (LPARAM) &line);
        SendMessageW(e, WM_CUT, 0, 0);
        // Read the sequence number after the cut so our own change counts;
        // any later clipboard change invalidates paste-above mode
        window->line_cut_pending = true;
        window->line_cut_clip_seq = GetClipboardSequenceNumber();
        return;
    }
    SendMessageW(e, WM_CUT, 0, 0);
}

void ui_platform_copy(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessageW(window->edit_hwnd, WM_COPY, 0, 0);
    }
}

void ui_platform_paste(Window *window) {
    if (!window || !window->edit_hwnd)
        return;
    HWND e = window->edit_hwnd;
    if (settings_get_bool("list_tools_enabled", false) && window->line_cut_pending &&
        GetClipboardSequenceNumber() == window->line_cut_clip_seq &&
        !ui_platform_has_selection(window)) {
        // Paste the line cut by Ctrl+X back ABOVE the current line, keeping
        // the caret where it is within its own line. Stays in effect for
        // repeat pastes until the clipboard changes hands.
        wchar_t *clip = read_clipboard_text(window->hwnd);
        if (clip && clip[0]) {
            // A last-line cut starts with the preceding break: strip it, then
            // guarantee a trailing break so the insert lands as a full line
            const wchar_t *body = clip;
            if (body[0] == L'\r' && body[1] == L'\n')
                body += 2;
            else if (body[0] == L'\r' || body[0] == L'\n')
                body += 1;
            size_t blen = wcslen(body);
            wchar_t *ins = malloc((blen + 3) * sizeof(wchar_t));
            if (ins) {
                memcpy(ins, body, blen * sizeof(wchar_t));
                size_t n = blen;
                if (blen == 0 || (body[blen - 1] != L'\n' && body[blen - 1] != L'\r')) {
                    ins[n++] = L'\r';
                    ins[n++] = L'\n';
                }
                ins[n] = L'\0';

                CHARRANGE sel = { 0, 0 };
                SendMessageW(e, EM_EXGETSEL, 0, (LPARAM) &sel);
                LONG caret = sel.cpMin;
                LONG line_start, line_end;
                get_paragraph_bounds(e, caret, &line_start, &line_end);
                LONG offset = caret - line_start;

                CHARRANGE at = { line_start, line_start };
                SendMessageW(e, EM_EXSETSEL, 0, (LPARAM) &at);
                SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM) ins);
                // Measure the inserted length from the control: RichEdit
                // stores each break as one cp, not the two chars we sent
                CHARRANGE after = { 0, 0 };
                SendMessageW(e, EM_EXGETSEL, 0, (LPARAM) &after);
                CHARRANGE fin = { after.cpMax + offset, after.cpMax + offset };
                SendMessageW(e, EM_EXSETSEL, 0, (LPARAM) &fin);
                SendMessageW(e, EM_SCROLLCARET, 0, 0);
                free(ins);
                free(clip);
                return;
            }
        }
        free(clip); // Clipboard text unavailable: fall back to a normal paste
    }
    window->line_cut_pending = false;
    SendMessageW(e, WM_PASTE, 0, 0);
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

// TaskDialogIndirect loaded dynamically: it only exists in comctl32 v6, and
// resolving it at runtime keeps the import table friendly to older setups
typedef HRESULT(WINAPI *TaskDialogIndirectFunc)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

UiOpenChoice ui_platform_prompt_binary_open(Window *parent, const char *filename) {
    HWND hwnd = parent ? parent->hwnd : NULL;

    char message[600];
    char *name = file_get_filename(filename);
    snprintf(message, sizeof(message),
             "\"%.260s\" does not look like a text file.\n\n"
             "Opening it in npad may show unreadable characters, and saving "
             "it from npad could corrupt it.",
             name ? name : (filename ? filename : ""));
    free(name);
    wchar_t *wmessage = utf8_to_wide(message);

    UiOpenChoice choice = UI_OPEN_CANCEL;
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    union {
        FARPROC proc;
        TaskDialogIndirectFunc func;
    } td;
    td.proc = comctl ? GetProcAddress(comctl, "TaskDialogIndirect") : NULL;

    if (td.proc) {
        const TASKDIALOG_BUTTON buttons[] = {
            { 101, L"Open in npad" },
            { 102, L"Open with the default app" },
        };
        TASKDIALOGCONFIG cfg;
        ZeroMemory(&cfg, sizeof(cfg));
        cfg.cbSize = sizeof(cfg);
        cfg.hwndParent = hwnd;
        cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        cfg.pszWindowTitle = L"npad";
        cfg.pszMainIcon = TD_WARNING_ICON;
        cfg.pszMainInstruction = L"This file looks like a binary file";
        cfg.pszContent = wmessage ? wmessage : L"This file does not look like a text file.";
        cfg.cButtons = 2;
        cfg.pButtons = buttons;
        cfg.nDefaultButton = IDCANCEL;

        int pressed = 0;
        if (SUCCEEDED(td.func(&cfg, &pressed, NULL, NULL))) {
            if (pressed == 101)
                choice = UI_OPEN_IN_NPAD;
            else if (pressed == 102)
                choice = UI_OPEN_WITH_DEFAULT;
        }
    } else {
        // Fallback: Yes = open here, No = system default, Cancel = abort
        wchar_t fallback[700];
        _snwprintf(fallback, 699,
                   L"%s\n\nYes: open in npad anyway\nNo: open with the default "
                   L"app\nCancel: do nothing",
                   wmessage ? wmessage : L"This file does not look like a text file.");
        fallback[699] = L'\0';
        int result = MessageBoxW(hwnd, fallback, L"npad", MB_YESNOCANCEL | MB_ICONWARNING);
        if (result == IDYES)
            choice = UI_OPEN_IN_NPAD;
        else if (result == IDNO)
            choice = UI_OPEN_WITH_DEFAULT;
    }

    free(wmessage);
    return choice;
}

void ui_platform_open_with_default_app(const char *filename) {
    if (!filename)
        return;
    wchar_t *wpath = utf8_to_wide(filename);
    if (!wpath)
        return;
    ShellExecuteW(g_main_window ? g_main_window->hwnd : NULL, L"open", wpath, NULL, NULL,
                  SW_SHOWNORMAL);
    free(wpath);
}

size_t ui_platform_system_memory_mb(void) {
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return 0;
    return (size_t) (ms.ullTotalPhys / (1024 * 1024));
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

// Show a transient message in the leftmost status bar part (shared with the
// optional counts display, whose cache must stay in sync so the next counts
// refresh is not skipped as a duplicate)
static void set_status_message(Window *window, const char *message) {
    if (!window || !window->status_hwnd)
        return;
    // Cancel any pending counts refresh: a debounce armed by an earlier edit
    // must not fire later and overwrite this (newer) message. The next edit
    // re-arms it.
    if (window->hwnd)
        KillTimer(window->hwnd, NPAD_COUNTS_TIMER_ID);
    wchar_t *wide = utf8_to_wide(message ? message : "");
    if (wide) {
        wcsncpy(window->status_cache[0], wide, 63);
        window->status_cache[0][63] = L'\0';
        SendMessageW(window->status_hwnd, SB_SETTEXTW, 0, (LPARAM) wide);
        free(wide);
    }
}

// ---------------------------------------------------------------------------
// Check for Updates (Help menu). Strictly on-demand per the core principle:
// npad never checks in the background and never updates automatically.
// ---------------------------------------------------------------------------

#define NPAD_WM_UPDATE_CHECKED (WM_APP + 1)
#define NPAD_WM_UPDATE_PROGRESS (WM_APP + 2)
#define NPAD_WM_UPDATE_DOWNLOADED (WM_APP + 3)

typedef struct {
    bool ok;
    char tag[32];
    char error[160];
} UpdateCheckResult;

typedef struct {
    HWND hwnd;
    char tag[32];
} UpdateDownloadJob;

typedef struct {
    bool ok;
    wchar_t path[MAX_PATH];
    char tag[32];
    char error[160];
} UpdateDownloadResult;

// One update operation (check or download) at a time
static volatile LONG g_update_busy = 0;

// True while the in-flight check was user-initiated (Help menu / Check Now):
// drives loud-vs-silent surfacing of the result. Set before spawning a check.
static bool g_update_check_manual = false;

// Refreshes the Help-menu update indicator for a window (defined with the
// menu code); forward-declared for the update result handlers
static void apply_update_indicator(Window *window);

// Current version as "vMAJOR.MINOR.PATCH"
static void current_version_string(char *out, size_t cap) {
    snprintf(out, cap, "v%d.%d.%d", NPAD_VERSION_MAJOR, NPAD_VERSION_MINOR, NPAD_VERSION_PATCH);
}

// Stamp the last-checked time (local "YYYY-MM-DD HH:MM") into settings
static void stamp_update_checked(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
             st.wMinute);
    settings_set_string("update_last_checked", buf);
}

// Bounded HTTPS GET on the calling (worker) thread. Returns a malloc'd,
// NUL-terminated buffer and its length; NULL on any failure. Posts percent
// progress to progress_hwnd when given and the size is known.
static char *http_get_alloc(const wchar_t *host, const wchar_t *path, size_t max_bytes,
                            size_t *out_len, HWND progress_hwnd) {
    char *data = NULL;
    size_t len = 0, cap = 0;
    bool ok = false;

    HINTERNET ses = WinHttpOpen(L"npad-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = NULL, req = NULL;
    if (!ses)
        goto done;
    con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con)
        goto done;
    req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req)
        goto done;
    // GitHub's API requires a User-Agent
    if (!WinHttpSendRequest(req, L"User-Agent: npad\r\nAccept: application/vnd.github+json\r\n",
                            (DWORD) -1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto done;
    if (!WinHttpReceiveResponse(req, NULL))
        goto done;

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
        goto done;

    DWORD total = 0;
    sz = sizeof(total);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz, WINHTTP_NO_HEADER_INDEX);

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail))
            goto done;
        if (avail == 0)
            break;
        if (len + avail > max_bytes)
            goto done;
        if (len + avail + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 65536;
            while (ncap < len + avail + 1)
                ncap *= 2;
            char *grown = realloc(data, ncap);
            if (!grown)
                goto done;
            data = grown;
            cap = ncap;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, data + len, avail, &got))
            goto done;
        len += got;
        if (progress_hwnd && total > 0)
            PostMessageW(progress_hwnd, NPAD_WM_UPDATE_PROGRESS, (WPARAM) ((len * 100) / total), 0);
    }
    if (data) {
        data[len] = '\0';
        ok = true;
    }

done:
    if (req)
        WinHttpCloseHandle(req);
    if (con)
        WinHttpCloseHandle(con);
    if (ses)
        WinHttpCloseHandle(ses);
    if (!ok) {
        free(data);
        data = NULL;
        len = 0;
    }
    if (out_len)
        *out_len = len;
    return data;
}

// SHA-256 of a file as lowercase hex via Windows CNG
static bool sha256_file_hex(const wchar_t *path, char out[65]) {
    bool ok = false;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    FILE *f = NULL;
    unsigned char digest[32];

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
        return false;
    if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0)
        goto done;
    f = _wfopen(path, L"rb");
    if (!f)
        goto done;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (BCryptHashData(hash, buf, (ULONG) n, 0) != 0)
            goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0)
        goto done;
    for (int i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    ok = true;

done:
    if (f)
        fclose(f);
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static DWORD WINAPI update_check_thread(LPVOID param) {
    HWND hwnd = (HWND) param;
    UpdateCheckResult *r = calloc(1, sizeof(*r));
    if (!r) {
        InterlockedExchange(&g_update_busy, 0);
        return 0;
    }
    size_t len = 0;
    char *json = http_get_alloc(L"api.github.com", L"/repos/platima/npad/releases/latest",
                                1024 * 1024, &len, NULL);
    if (!json) {
        snprintf(r->error, sizeof(r->error), "Could not reach github.com to check for updates.");
    } else if (!update_extract_tag(json, r->tag, sizeof(r->tag))) {
        snprintf(r->error, sizeof(r->error), "Unexpected response from the update server.");
    } else {
        r->ok = true;
    }
    free(json);
    PostMessageW(hwnd, NPAD_WM_UPDATE_CHECKED, 0, (LPARAM) r);
    return 0;
}

// Start a check if none is running. manual=true surfaces the result loudly
// (Help menu / Check Now button); manual=false is the silent launch/auto path.
// Returns false when a check is already in flight or the thread failed.
static bool start_update_check(Window *window, bool manual) {
    if (!window)
        return false;
    if (InterlockedCompareExchange(&g_update_busy, 1, 0) != 0)
        return false;
    g_update_check_manual = manual;
    set_status_message(window, "Checking for updates...");
    HANDLE thread = CreateThread(NULL, 0, update_check_thread, window->hwnd, 0, NULL);
    if (!thread) {
        InterlockedExchange(&g_update_busy, 0);
        set_status_message(window, "Update check failed to start");
        return false;
    }
    CloseHandle(thread);
    return true;
}

static DWORD WINAPI update_download_thread(LPVOID param) {
    UpdateDownloadJob *job = (UpdateDownloadJob *) param;
    UpdateDownloadResult *r = calloc(1, sizeof(*r));
    if (!r) {
        free(job);
        InterlockedExchange(&g_update_busy, 0);
        return 0;
    }
    snprintf(r->tag, sizeof(r->tag), "%s", job->tag);
    const char *ver = (job->tag[0] == 'v' || job->tag[0] == 'V') ? job->tag + 1 : job->tag;

    wchar_t wver[32], wtag[32];
    _snwprintf(wver, 31, L"%hs", ver);
    wver[31] = L'\0';
    _snwprintf(wtag, 31, L"%hs", job->tag);
    wtag[31] = L'\0';

    wchar_t path[256];
    size_t exe_len = 0, sum_len = 0;
    char *exe = NULL, *sum = NULL;

    // The installer (progress reported) and its published digest
    _snwprintf(path, 255, L"/platima/npad/releases/download/%s/npad-setup-%s.exe", wtag, wver);
    path[255] = L'\0';
    exe = http_get_alloc(L"github.com", path, (size_t) 200 * 1024 * 1024, &exe_len, job->hwnd);
    if (exe) {
        _snwprintf(path, 255, L"/platima/npad/releases/download/%s/npad-setup-%s.exe.sha256", wtag,
                   wver);
        path[255] = L'\0';
        sum = http_get_alloc(L"github.com", path, 4096, &sum_len, NULL);
    }

    char expected[65], actual[65];
    if (!exe || exe_len == 0) {
        snprintf(r->error, sizeof(r->error), "Could not download the installer for npad %s.",
                 job->tag);
    } else if (!sum || !update_parse_sha256(sum, expected)) {
        snprintf(r->error, sizeof(r->error),
                 "Could not download the checksum to verify the installer.");
    } else {
        wchar_t temp_dir[MAX_PATH];
        if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
            snprintf(r->error, sizeof(r->error), "Could not resolve the temporary directory.");
        } else {
            _snwprintf(r->path, MAX_PATH - 1, L"%snpad-setup-%s.exe", temp_dir, wver);
            r->path[MAX_PATH - 1] = L'\0';
            FILE *f = _wfopen(r->path, L"wb");
            bool written = f && fwrite(exe, 1, exe_len, f) == exe_len;
            if (f)
                fclose(f);
            if (!written) {
                snprintf(r->error, sizeof(r->error), "Could not save the downloaded installer.");
            } else if (!sha256_file_hex(r->path, actual) || strcmp(actual, expected) != 0) {
                DeleteFileW(r->path); // Corrupt or tampered: never leave it around
                snprintf(r->error, sizeof(r->error),
                         "The downloaded installer failed SHA-256 verification and was "
                         "deleted.");
            } else {
                r->ok = true;
            }
        }
    }

    free(exe);
    free(sum);
    HWND hwnd = job->hwnd;
    free(job);
    PostMessageW(hwnd, NPAD_WM_UPDATE_DOWNLOADED, 0, (LPARAM) r);
    return 0;
}

// UI-thread handler for the check result: report, or offer the update
// Spawn the verified-download worker for tag. Returns true when the thread
// started (the busy flag then stays set until the download result arrives).
static bool spawn_update_download(Window *window, const char *tag) {
    UpdateDownloadJob *job = calloc(1, sizeof(*job));
    if (!job)
        return false;
    job->hwnd = window ? window->hwnd : NULL;
    snprintf(job->tag, sizeof(job->tag), "%s", tag);
    HANDLE thread = CreateThread(NULL, 0, update_download_thread, job, 0, NULL);
    if (!thread) {
        free(job);
        return false;
    }
    CloseHandle(thread);
    return true;
}

// Present the "newer version available" TaskDialog (with a Skip option) and
// act on the choice. Called for a manual check that found a newer release, or
// an automatic check in "prompt" mode.
static void prompt_update_available(Window *window, const char *tag, const char *cur) {
    HWND hwnd = window ? window->hwnd : NULL;
    char msg[200];
    snprintf(msg, sizeof(msg), "npad %s is available (you have %s).", tag, cur);
    wchar_t *wmsg = utf8_to_wide(msg);

    int choice = 0; // 0 = not now, 101 = install, 102 = notes, 103 = skip
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    union {
        FARPROC proc;
        TaskDialogIndirectFunc func;
    } td;
    td.proc = comctl ? GetProcAddress(comctl, "TaskDialogIndirect") : NULL;
    if (td.proc) {
        const TASKDIALOG_BUTTON buttons[] = {
            { 101, L"Download and install" },
            { 102, L"View the release notes" },
            { 103, L"Skip this version" },
        };
        TASKDIALOGCONFIG cfg;
        ZeroMemory(&cfg, sizeof(cfg));
        cfg.cbSize = sizeof(cfg);
        cfg.hwndParent = hwnd;
        cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        cfg.pszWindowTitle = L"npad";
        cfg.pszMainIcon = TD_INFORMATION_ICON;
        cfg.pszMainInstruction = L"A newer version of npad is available";
        cfg.pszContent = wmsg ? wmsg : L"A newer release is available.";
        cfg.cButtons = 3;
        cfg.pButtons = buttons;
        cfg.nDefaultButton = 101;
        int pressed = 0;
        if (SUCCEEDED(td.func(&cfg, &pressed, NULL, NULL)))
            choice = pressed;
    } else {
        wchar_t fb[300];
        _snwprintf(fb, 299,
                   L"%s\n\nYes: download and install\nNo: view the release notes\n"
                   L"Cancel: not now",
                   wmsg ? wmsg : L"A newer release is available.");
        fb[299] = L'\0';
        int res = MessageBoxW(hwnd, fb, L"npad", MB_YESNOCANCEL | MB_ICONINFORMATION);
        choice = (res == IDYES) ? 101 : (res == IDNO) ? 102 : 0;
    }
    free(wmsg);

    if (choice == 101) {
        char status[96];
        snprintf(status, sizeof(status), "Downloading npad %s...", tag);
        set_status_message(window, status);
        if (!spawn_update_download(window, tag)) {
            set_status_message(window, "Update download failed to start");
            InterlockedExchange(&g_update_busy, 0);
        }
        return; // Busy flag stays set for the download (or was cleared above)
    }
    if (choice == 102) {
        ShellExecuteW(hwnd, L"open", L"https://github.com/platima/npad/releases/latest", NULL, NULL,
                      SW_SHOWNORMAL);
    } else if (choice == 103) {
        settings_set_string("update_skipped_version", tag);
        settings_save();
        ui_platform_notify_settings_changed();
    }
    InterlockedExchange(&g_update_busy, 0);
    apply_update_indicator(window);
}

static void handle_update_checked(Window *window, UpdateCheckResult *r) {
    HWND hwnd = window ? window->hwnd : NULL;
    bool manual = g_update_check_manual;
    char cur[32];
    current_version_string(cur, sizeof(cur));

    if (!r->ok) {
        // Silent for automatic/launch checks; loud only when the user asked
        if (manual) {
            wchar_t *w = utf8_to_wide(r->error);
            MessageBoxW(hwnd, w ? w : L"Update check failed.", L"npad", MB_OK | MB_ICONWARNING);
            free(w);
        }
        set_status_message(window, "Update check failed");
        InterlockedExchange(&g_update_busy, 0);
        free(r);
        return;
    }

    // Record the result for the prefs display and the Help indicator, and let
    // other instances pick it up (they refresh their own indicator on reload)
    stamp_update_checked();
    settings_set_string("update_latest_version", r->tag);
    settings_save();
    ui_platform_notify_settings_changed();

    bool newer = update_version_compare(r->tag, cur) > 0;
    char *skipped = settings_get_string("update_skipped_version", "");
    char *mode = settings_get_string("update_mode", "off");
    bool surface_auto = update_is_newer_unskipped(cur, r->tag, skipped);
    free(skipped);

    // A manual check re-surfaces even a skipped version (the user asked); an
    // automatic check obeys the mode and the skip
    bool show_prompt = manual ? newer : (surface_auto && strcmp(mode, "prompt") == 0);
    bool do_download = !manual && surface_auto && strcmp(mode, "auto") == 0;
    free(mode);

    if (do_download) {
        char status[96];
        snprintf(status, sizeof(status), "Downloading npad %s...", r->tag);
        set_status_message(window, status);
        if (!spawn_update_download(window, r->tag))
            InterlockedExchange(&g_update_busy, 0);
        apply_update_indicator(window);
        free(r);
        return;
    }

    if (show_prompt) {
        // prompt_update_available owns the busy flag from here
        char tag[32];
        snprintf(tag, sizeof(tag), "%s", r->tag);
        free(r);
        prompt_update_available(window, tag, cur);
        return;
    }

    // Silent outcomes: up to date (manual), notify-mode dot only, or skipped
    if (manual) {
        char msg[160];
        snprintf(msg, sizeof(msg), "npad is up to date.\n\nYou have %s; the latest release is %s.",
                 cur, r->tag);
        wchar_t *w = utf8_to_wide(msg);
        MessageBoxW(hwnd, w ? w : L"npad is up to date.", L"npad", MB_OK | MB_ICONINFORMATION);
        free(w);
        set_status_message(window, "npad is up to date");
    } else if (surface_auto) {
        set_status_message(window, "An update is available (see the Help menu)");
    }
    InterlockedExchange(&g_update_busy, 0);
    apply_update_indicator(window);
    free(r);
}

// UI-thread handler for the verified download: confirm, launch, quit
static void handle_update_downloaded(Window *window, UpdateDownloadResult *r) {
    HWND hwnd = window ? window->hwnd : NULL;
    if (!r->ok) {
        wchar_t *w = utf8_to_wide(r->error);
        MessageBoxW(hwnd, w ? w : L"Update download failed.", L"npad", MB_OK | MB_ICONWARNING);
        free(w);
        set_status_message(window, "Update download failed");
    } else {
        set_status_message(window, "Update downloaded and verified");
        wchar_t msg[MAX_PATH + 160];
        _snwprintf(msg, MAX_PATH + 159,
                   L"npad %hs was downloaded and its SHA-256 checksum verified.\n\n"
                   L"Install now? The installer will start and npad will close (you'll be "
                   L"prompted to save any unsaved changes).",
                   r->tag);
        msg[MAX_PATH + 159] = L'\0';
        if (MessageBoxW(hwnd, msg, L"npad", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            ShellExecuteW(hwnd, L"open", r->path, NULL, NULL, SW_SHOWNORMAL);
            ui_post_event(UI_EVENT_QUIT, window, NULL);
        } else {
            wchar_t note[MAX_PATH + 96];
            _snwprintf(note, MAX_PATH + 95, L"The verified installer was saved to:\n%s", r->path);
            note[MAX_PATH + 95] = L'\0';
            MessageBoxW(hwnd, note, L"npad", MB_OK | MB_ICONINFORMATION);
        }
    }
    InterlockedExchange(&g_update_busy, 0);
    apply_update_indicator(window); // The update is still available if declined
    free(r);
}

// --- Highlight all matches -------------------------------------------------
//
// The edit control runs in plain-text mode (one character format for the
// whole document), so per-range background formatting is rejected by
// RichEdit. Highlights are therefore drawn as a painted overlay in the
// WM_PAINT subclass hook: pure painting, inherently invisible to the undo
// history and the modified flag.

// Recompute the match list for the highlight-all overlay (want), or clear
// it (!want), then repaint the control
static void refresh_highlights(const Window *window, bool want) {
    if (!window || !window->edit_hwnd)
        return;
    HWND e = window->edit_hwnd;
    int had = g_hl_count;
    g_hl_count = 0;

    if (want && g_search_eff[0] != L'\0') {
        WPARAM flags = FR_DOWN;
        if (g_match_case)
            flags |= FR_MATCHCASE;
        if (g_whole_word)
            flags |= FR_WHOLEWORD;
        LONG from = 0;
        for (int n = 0; n < 10000; n++) {
            FINDTEXTEXW ft;
            memset(&ft, 0, sizeof(ft));
            ft.lpstrText = g_search_eff;
            ft.chrg.cpMin = from;
            ft.chrg.cpMax = -1;
            if (SendMessageW(e, EM_FINDTEXTEXW, flags, (LPARAM) &ft) == -1)
                break;
            if (g_hl_count == g_hl_capacity) {
                int cap = g_hl_capacity ? g_hl_capacity * 2 : 64;
                CHARRANGE *grown = realloc(g_hl_matches, (size_t) cap * sizeof(CHARRANGE));
                if (!grown)
                    break;
                g_hl_matches = grown;
                g_hl_capacity = cap;
            }
            g_hl_matches[g_hl_count++] = ft.chrgText;
            from = ft.chrgText.cpMax;
            if (from <= ft.chrgText.cpMin)
                from = ft.chrgText.cpMin + 1; // Guard against zero-width matches
        }
    }

    g_highlights_applied = g_hl_count > 0;
    if (had || g_hl_count)
        InvalidateRect(e, NULL, TRUE);
}

// Alpha-blend one highlight rectangle
static void blend_highlight_rect(HDC dc, HDC mem, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 96; // ~38% wash over the text
    bf.AlphaFormat = 0;
    AlphaBlend(dc, x, y, w, h, mem, 0, 0, 1, 1, bf);
}

// Draw the highlight overlay for the matches visible in the given update
// region. Clipping to the region keeps the alpha wash single-layered: each
// repainted pixel is blended exactly once per repaint.
static void draw_highlight_overlay(HWND e, HRGN clip) {
    HDC dc = GetDC(e);
    if (!dc)
        return;
    SelectClipRgn(dc, clip);

    RECT rc;
    GetClientRect(e, &rc);
    POINTL tl = { rc.left, rc.top };
    POINTL br = { rc.right, rc.bottom };
    LONG first = (LONG) SendMessageW(e, EM_CHARFROMPOS, 0, (LPARAM) &tl);
    LONG last = (LONG) SendMessageW(e, EM_CHARFROMPOS, 0, (LPARAM) &br);
    if (last < first)
        last = first;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 1, 1);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetPixel(mem, 0, 0, RGB(255, 210, 40)); // Amber; the alpha wash adapts to any theme

    for (int i = 0; i < g_hl_count; i++) {
        CHARRANGE m = g_hl_matches[i];
        if (m.cpMax < first)
            continue;
        if (m.cpMin > last)
            break;
        // Per display line: a match may span wrapped or real lines
        LONG c = m.cpMin;
        for (int guard = 0; c < m.cpMax && guard < 64; guard++) {
            POINTL p1 = { 0, 0 };
            SendMessageW(e, EM_POSFROMCHAR, (WPARAM) &p1, c);
            LONG line = (LONG) SendMessageW(e, EM_EXLINEFROMCHAR, 0, c);
            LONG lstart = (LONG) SendMessageW(e, EM_LINEINDEX, (WPARAM) line, 0);
            LONG lend = lstart + (LONG) SendMessageW(e, EM_LINELENGTH, (WPARAM) lstart, 0);
            LONG seg_end = (m.cpMax < lend) ? m.cpMax : lend;

            // Line height from the next display line's y, else a sane floor
            int height = 0;
            LONG nstart = (LONG) SendMessageW(e, EM_LINEINDEX, (WPARAM) (line + 1), 0);
            if (nstart >= 0) {
                POINTL pn = { 0, 0 };
                SendMessageW(e, EM_POSFROMCHAR, (WPARAM) &pn, nstart);
                if (pn.y > p1.y)
                    height = pn.y - p1.y;
            }
            if (height <= 0)
                height = MulDiv(19, (int) get_window_dpi(e), 96);

            // Segment end position; a segment that continues past this
            // display line runs to the right edge instead
            POINTL p2 = { 0, 0 };
            SendMessageW(e, EM_POSFROMCHAR, (WPARAM) &p2, seg_end);
            int x2 = (seg_end < m.cpMax && p2.x <= p1.x) ? rc.right : p2.x;
            blend_highlight_rect(dc, mem, p1.x, p1.y, x2 - p1.x, height);

            if (seg_end <= c)
                break; // No forward progress: bail out
            c = seg_end;
            if (c < m.cpMax && c == lend)
                c++; // Step over the line break of a real line
        }
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(e, dc);
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
        ft.lpstrText = g_search_eff;
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
    ft.lpstrText = g_search_eff;
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
    if (!window || !window->edit_hwnd || g_search_eff[0] == L'\0')
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

// Interpret backslash escapes in a search/replace buffer in place, then
// normalize CRLF and lone LF to the single '\r' RichEdit uses internally --
// required both for EM_FINDTEXTEXW to match line breaks and for replace_all's
// position arithmetic (an inserted break is one cp, whatever we sent).
static void unescape_search_buffer(wchar_t *buf, size_t cap) {
    char *utf8 = wide_to_utf8(buf);
    if (utf8) {
        char *unescaped = list_unescape(utf8);
        free(utf8);
        if (unescaped) {
            wchar_t *wide = utf8_to_wide(unescaped);
            free(unescaped);
            if (wide) {
                // Unescaping only shrinks, but guard the copy anyway
                wcsncpy(buf, wide, cap - 1);
                buf[cap - 1] = L'\0';
                free(wide);
            }
        }
    }
    wchar_t *w = buf;
    for (const wchar_t *r = buf; *r; r++) {
        if (*r == L'\r' && r[1] == L'\n') {
            *w++ = L'\r';
            r++;
        } else if (*r == L'\n') {
            *w++ = L'\r';
        } else {
            *w++ = *r;
        }
    }
    *w = L'\0';
}

// Read the search/replace text and option checkboxes into the globals (no
// history push, no settings write): shared by the action buttons and the
// live highlight-all refresh
static void read_search_text(HWND dialog, bool has_replace) {
    GetDlgItemTextW(dialog, ID_FIND_TEXT, g_search_text,
                    sizeof(g_search_text) / sizeof(g_search_text[0]));
    if (has_replace) {
        GetDlgItemTextW(dialog, ID_REPLACE_WITH, g_replace_text,
                        sizeof(g_replace_text) / sizeof(g_replace_text[0]));
    }
    g_match_case = IsDlgButtonChecked(dialog, ID_FIND_CASE) == BST_CHECKED;
    g_whole_word = IsDlgButtonChecked(dialog, ID_FIND_WHOLE_WORD) == BST_CHECKED;
    g_wrap_around = IsDlgButtonChecked(dialog, ID_FIND_WRAP) == BST_CHECKED;
    g_interpret_escapes = IsDlgButtonChecked(dialog, ID_FIND_ESCAPES) == BST_CHECKED;
    g_highlight_all = IsDlgButtonChecked(dialog, ID_FIND_HIGHLIGHT) == BST_CHECKED;
    if (GetDlgItem(dialog, IDC_RADIO_UP)) {
        g_search_down = IsDlgButtonChecked(dialog, IDC_RADIO_UP) != BST_CHECKED;
    }

    // History and the dialog keep the RAW typed text; the search machinery
    // uses the effective copies (escape-interpreted when enabled)
    wcscpy(g_search_eff, g_search_text);
    wcscpy(g_replace_eff, g_replace_text);
    if (g_interpret_escapes) {
        unescape_search_buffer(g_search_eff, sizeof(g_search_eff) / sizeof(g_search_eff[0]));
        unescape_search_buffer(g_replace_eff, sizeof(g_replace_eff) / sizeof(g_replace_eff[0]));
    }
}

// Read search parameters out of the find/replace dialog controls, persist
// them across sessions, and refresh the highlight-all overlay
static void read_search_fields(HWND dialog, bool has_replace) {
    read_search_text(dialog, has_replace);

    settings_set_bool("find_match_case", g_match_case);
    settings_set_bool("find_whole_word", g_whole_word);
    settings_set_bool("find_wrap_around", g_wrap_around);
    settings_set_bool("find_search_down", g_search_down);
    settings_set_bool("find_interpret_escapes", g_interpret_escapes);
    settings_set_bool("find_highlight_all", g_highlight_all);

    history_push("find_hist", g_search_text);
    if (has_replace) {
        history_push("replace_hist", g_replace_text);
    }

    const Window *window = (const Window *) GetWindowLongPtrW(dialog, GWLP_USERDATA);
    refresh_highlights(window, g_highlight_all);
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
        matches = wcscmp(wide, g_search_eff) == 0;
    } else {
        matches = _wcsicmp(wide, g_search_eff) == 0;
    }
    free(wide);
    return matches;
}

static void replace_current(Window *window) {
    if (selection_matches_search(window)) {
        SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) g_replace_eff);
    }
    find_next(window, g_search_down);
}

static void replace_all(Window *window) {
    if (!window || !window->edit_hwnd || g_search_eff[0] == L'\0')
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
        ft.lpstrText = g_search_eff;
        ft.chrg.cpMin = start;
        ft.chrg.cpMax = -1;

        LRESULT pos = SendMessageW(window->edit_hwnd, EM_FINDTEXTEXW, flags, (LPARAM) &ft);
        if (pos == -1)
            break;

        SendMessageW(window->edit_hwnd, EM_EXSETSEL, 0, (LPARAM) &ft.chrgText);
        SendMessageW(window->edit_hwnd, EM_REPLACESEL, TRUE, (LPARAM) g_replace_eff);
        start = ft.chrgText.cpMin + (LONG) wcslen(g_replace_eff);
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
            CheckDlgButton(dialog, ID_FIND_ESCAPES,
                           g_interpret_escapes ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, ID_FIND_HIGHLIGHT,
                           g_highlight_all ? BST_CHECKED : BST_UNCHECKED);
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
                    SetWindowPos(dialog, NULL,
                                 parent_rect.left + MulDiv(NPAD_DIALOG_OFFSET_X, dpi, 96),
                                 parent_rect.top + MulDiv(NPAD_DIALOG_OFFSET_Y, dpi, 96), 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }

            SendDlgItemMessageW(dialog, ID_FIND_TEXT, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
            SetFocus(GetDlgItem(dialog, ID_FIND_TEXT));
            return FALSE; // Focus set manually
        }

        case WM_COMMAND:
            // Live highlight refresh: retyping the search text (debounced)
            // and toggling any option that changes what matches
            if (LOWORD(wparam) == ID_FIND_TEXT && HIWORD(wparam) == CBN_EDITCHANGE) {
                if (IsDlgButtonChecked(dialog, ID_FIND_HIGHLIGHT) == BST_CHECKED)
                    SetTimer(dialog, 1, 300, NULL);
                break;
            }
            if (HIWORD(wparam) == BN_CLICKED &&
                (LOWORD(wparam) == ID_FIND_HIGHLIGHT || LOWORD(wparam) == ID_FIND_CASE ||
                 LOWORD(wparam) == ID_FIND_WHOLE_WORD || LOWORD(wparam) == ID_FIND_ESCAPES)) {
                read_search_text(dialog, has_replace);
                settings_set_bool("find_highlight_all", g_highlight_all);
                refresh_highlights(window, g_highlight_all);
                break;
            }
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

        case WM_TIMER:
            if (wparam == 1) {
                KillTimer(dialog, 1); // One-shot (debounced live highlight)
                read_search_text(dialog, has_replace);
                refresh_highlights(window, g_highlight_all);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(dialog);
            return TRUE;

        case WM_DESTROY: {
            KillTimer(dialog, 1);
            refresh_highlights(window, false); // Clear highlights with the dialog
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
            SetDlgItemInt(
                page, ID_PREF_LARGE_FILE_MB,
                (UINT) settings_get_int("large_file_warning_mb", editor_default_large_file_mb()),
                FALSE);
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
            CheckDlgButton(page, ID_PREF_STATUS_COUNTS,
                           settings_get_bool("status_show_counts", false) ? BST_CHECKED
                                                                          : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD code = HIWORD(wparam);
            WORD id = LOWORD(wparam);
            if ((code == BN_CLICKED && (id == ID_PREF_OPENDYSLEXIC || id == ID_PREF_STATUSBAR ||
                                        id == ID_PREF_SYNC_VIEW || id == ID_PREF_STATUS_COUNTS)) ||
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

                settings_set_bool("status_show_counts",
                                  IsDlgButtonChecked(page, ID_PREF_STATUS_COUNTS) == BST_CHECKED);
                if (g_main_window) {
                    apply_counts_pref(g_main_window);
                }

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

// Reset every preference to its default (Backup page). Rather than an
// allowlist of keys to remove (which silently drifts every time a new
// Preferences tab/setting is added - and did), this resets EVERYTHING except
// the small, stable set of preserved categories: recent files, window
// geometry, and Find/Replace state (options + history). Any new preference
// key is therefore reset by default; getters fall back to their defaults for
// the now-missing keys. To keep something across a reset, add its prefix here.
static void reset_all_preferences(HWND owner) {
    static const char *const keep_prefixes[] = {
        "recent_file_", // recent files list (recent_files_max is a pref -> reset)
        "window_",      // window geometry
        "find_",        // find options + find history
        "replace_",     // replace history
    };

    if (MessageBoxW(owner,
                    L"Reset all preferences to their defaults?\n\n"
                    L"Recent files, window position and Find/Replace options and history "
                    L"are kept.\n"
                    L"The Preferences window will close.",
                    L"npad", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    settings_reset_except_prefixes(keep_prefixes,
                                   (int) (sizeof(keep_prefixes) / sizeof(keep_prefixes[0])));
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
            // Match the new-window seeding fallback (pre-0.7 monospace_enabled),
            // so the shown default equals the effective default and Apply does
            // not silently flip a migrated user's mono default to proportional
            SendMessageW(font_combo, CB_SETCURSEL,
                         settings_get_bool("default_font_mono",
                                           settings_get_bool("monospace_enabled", false))
                             ? 0
                             : 1,
                         0);

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

// Preferences: Updates page - modes and status (all opt-in, off by default)
static const char *const UPDATE_MODE_KEYS[] = { "off", "notify", "prompt", "auto" };
static const wchar_t *const UPDATE_MODE_LABELS[] = {
    L"Do nothing (check manually from the Help menu)", L"Notify silently (a dot on the Help menu)",
    L"Prompt me to download and install", L"Download and install automatically"
};
#define UPDATE_MODE_COUNT 4

// Enable "Skip This Version" only when a newer, non-skipped release is known
static void prefs_updates_sync_skip(HWND page) {
    char cur[32];
    current_version_string(cur, sizeof(cur));
    char *latest = settings_get_string("update_latest_version", "");
    char *skipped = settings_get_string("update_skipped_version", "");
    EnableWindow(GetDlgItem(page, ID_PREF_UPD_SKIP),
                 update_is_newer_unskipped(cur, latest, skipped));
    free(latest);
    free(skipped);
}

static INT_PTR CALLBACK prefs_updates_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG: {
            char cur[32];
            current_version_string(cur, sizeof(cur));
            wchar_t wcur[32];
            _snwprintf(wcur, 31, L"%hs", cur);
            wcur[31] = L'\0';
            SetDlgItemTextW(page, ID_PREF_UPD_CURRENT, wcur);

            char *latest = settings_get_string("update_latest_version", "");
            wchar_t *wlatest = utf8_to_wide((latest && latest[0]) ? latest : "unknown");
            if (wlatest) {
                SetDlgItemTextW(page, ID_PREF_UPD_LATEST, wlatest);
                free(wlatest);
            }
            free(latest);

            char *last = settings_get_string("update_last_checked", "");
            wchar_t *wlast = utf8_to_wide((last && last[0]) ? last : "never");
            if (wlast) {
                SetDlgItemTextW(page, ID_PREF_UPD_LASTCHECK, wlast);
                free(wlast);
            }
            free(last);

            HWND combo = GetDlgItem(page, ID_PREF_UPD_MODE);
            for (int i = 0; i < UPDATE_MODE_COUNT; i++)
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) UPDATE_MODE_LABELS[i]);
            char *mode = settings_get_string("update_mode", "off");
            int sel = 0;
            for (int i = 0; i < UPDATE_MODE_COUNT; i++) {
                if (strcmp(mode, UPDATE_MODE_KEYS[i]) == 0) {
                    sel = i;
                    break;
                }
            }
            free(mode);
            SendMessageW(combo, CB_SETCURSEL, (WPARAM) sel, 0);

            CheckDlgButton(page, ID_PREF_UPD_ON_LAUNCH,
                           settings_get_bool("update_check_on_launch", false) ? BST_CHECKED
                                                                              : BST_UNCHECKED);
            prefs_updates_sync_skip(page);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD code = HIWORD(wparam);
            WORD id = LOWORD(wparam);
            if ((code == CBN_SELCHANGE && id == ID_PREF_UPD_MODE) ||
                (code == BN_CLICKED && id == ID_PREF_UPD_ON_LAUNCH)) {
                mark_prefs_dirty(page);
                return TRUE;
            }
            if (id == ID_PREF_UPD_CHECK) {
                // Manual check from the prefs page (result goes to the main window)
                if (g_main_window)
                    start_update_check(g_main_window, true);
                return TRUE;
            }
            if (id == ID_PREF_UPD_SKIP) {
                char *latest = settings_get_string("update_latest_version", "");
                if (latest && latest[0]) {
                    settings_set_string("update_skipped_version", latest);
                    settings_save();
                    ui_platform_notify_settings_changed();
                    if (g_main_window)
                        apply_update_indicator(g_main_window);
                }
                free(latest);
                prefs_updates_sync_skip(page);
                return TRUE;
            }
            break;
        }

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                LRESULT sel = SendMessageW(GetDlgItem(page, ID_PREF_UPD_MODE), CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < UPDATE_MODE_COUNT)
                    settings_set_string("update_mode", UPDATE_MODE_KEYS[sel]);
                settings_set_bool("update_check_on_launch",
                                  IsDlgButtonChecked(page, ID_PREF_UPD_ON_LAUNCH) == BST_CHECKED);
                settings_save();
                ui_platform_notify_settings_changed();
                if (g_main_window)
                    apply_update_indicator(g_main_window);
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Preferences: Lists page - optional list tools (off by default)
static const wchar_t *const INDENT_FORMAT_LABELS[] = { L"Spaces",
                                                       L"Tab",
                                                       L"Asterisk (\"* \")",
                                                       L"Hyphen (\"- \")",
                                                       L"Asterisk, leading space (\" * \")",
                                                       L"Hyphen, leading space (\" - \")",
                                                       L"Custom..." };
#define INDENT_FORMAT_COUNT 7

// Custom-prefix edit follows the combo selection; the Tab/bracket shortcut
// choice only means something while the tools are enabled
static void prefs_lists_sync_enables(HWND page) {
    bool on = IsDlgButtonChecked(page, ID_PREF_LIST_ENABLED) == BST_CHECKED;
    LRESULT sel = SendMessageW(GetDlgItem(page, ID_PREF_LIST_INDENT_FORMAT), CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(page, ID_PREF_LIST_CUSTOM_TEXT), sel == LIST_INDENT_CUSTOM);
    EnableWindow(GetDlgItem(page, ID_PREF_LIST_TAB_BRACKETS), on);
}

static INT_PTR CALLBACK prefs_lists_proc(HWND page, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INITDIALOG: {
            CheckDlgButton(page, ID_PREF_LIST_ENABLED,
                           settings_get_bool("list_tools_enabled", false) ? BST_CHECKED
                                                                          : BST_UNCHECKED);
            HWND combo = GetDlgItem(page, ID_PREF_LIST_INDENT_FORMAT);
            for (int i = 0; i < INDENT_FORMAT_COUNT; i++)
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) INDENT_FORMAT_LABELS[i]);
            int sel = settings_get_int("list_default_indent_format", 0);
            if (sel < 0 || sel >= INDENT_FORMAT_COUNT)
                sel = 0;
            SendMessageW(combo, CB_SETCURSEL, (WPARAM) sel, 0);
            char *custom = settings_get_string("list_custom_indent", "");
            if (custom) {
                wchar_t *wide = utf8_to_wide(custom);
                if (wide) {
                    SetDlgItemTextW(page, ID_PREF_LIST_CUSTOM_TEXT, wide);
                    free(wide);
                }
                free(custom);
            }
            CheckDlgButton(page, ID_PREF_LIST_TAB_BRACKETS,
                           settings_get_bool("list_indent_shortcut_brackets", false)
                               ? BST_CHECKED
                               : BST_UNCHECKED);
            prefs_lists_sync_enables(page);
            return TRUE;
        }

        case WM_COMMAND:
            if ((HIWORD(wparam) == BN_CLICKED && (LOWORD(wparam) == ID_PREF_LIST_ENABLED ||
                                                  LOWORD(wparam) == ID_PREF_LIST_TAB_BRACKETS)) ||
                (HIWORD(wparam) == CBN_SELCHANGE && LOWORD(wparam) == ID_PREF_LIST_INDENT_FORMAT) ||
                (HIWORD(wparam) == EN_CHANGE && LOWORD(wparam) == ID_PREF_LIST_CUSTOM_TEXT)) {
                mark_prefs_dirty(page);
                prefs_lists_sync_enables(page);
            }
            break;

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *) lparam;
            if (nmhdr->code == PSN_APPLY) {
                settings_set_bool("list_tools_enabled",
                                  IsDlgButtonChecked(page, ID_PREF_LIST_ENABLED) == BST_CHECKED);
                LRESULT sel =
                    SendMessageW(GetDlgItem(page, ID_PREF_LIST_INDENT_FORMAT), CB_GETCURSEL, 0, 0);
                if (sel >= 0)
                    settings_set_int("list_default_indent_format", (int) sel);
                wchar_t wide_custom[128];
                GetDlgItemTextW(page, ID_PREF_LIST_CUSTOM_TEXT, wide_custom, 128);
                char *custom = wide_to_utf8(wide_custom);
                if (custom) {
                    // Stored raw (as typed); unescaped at point of use
                    settings_set_string("list_custom_indent", custom);
                    free(custom);
                }
                settings_set_bool("list_indent_shortcut_brackets",
                                  IsDlgButtonChecked(page, ID_PREF_LIST_TAB_BRACKETS) ==
                                      BST_CHECKED);
                settings_save();
                // Apply the enable toggle to this window right away (menu +
                // indent accelerators), then propagate to other instances
                if (g_main_window) {
                    apply_list_tools_menu(g_main_window);
                    build_accelerators(g_main_window);
                }
                ui_platform_notify_settings_changed();
                SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Preferences page indices (Updates before the hidden Debug page)
#define PREFS_PAGE_UPDATES 5

// start_page: a page index to open on, or -1 for the default (page 0, or the
// Debug page when show_debug). show_debug appends the hidden diagnostics page.
static void show_preferences_dialog(const Window *window, int start_page, bool show_debug) {
    if (!window)
        return;

    PROPSHEETPAGEW pages[7];
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
    pages[3].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_LISTS);
    pages[3].pfnDlgProc = prefs_lists_proc;

    pages[4].dwSize = sizeof(PROPSHEETPAGEW);
    pages[4].hInstance = g_hinstance;
    pages[4].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_BACKUP);
    pages[4].pfnDlgProc = prefs_backup_proc;

    pages[5].dwSize = sizeof(PROPSHEETPAGEW);
    pages[5].hInstance = g_hinstance;
    pages[5].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_UPDATES);
    pages[5].pfnDlgProc = prefs_updates_proc;

    // Hidden diagnostics page: only present when opened via Ctrl+Shift+.
    // or a Shift+click on the Preferences menu item
    pages[6].dwSize = sizeof(PROPSHEETPAGEW);
    pages[6].hInstance = g_hinstance;
    pages[6].pszTemplate = MAKEINTRESOURCEW(IDD_PREFS_DEBUG);
    pages[6].pfnDlgProc = prefs_debug_proc;

    PROPSHEETHEADERW psh;
    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(PROPSHEETHEADERW);
    // Include the Apply button so changes can be previewed without closing
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    psh.hwndParent = window->hwnd;
    psh.hInstance = g_hinstance;
    psh.pszCaption = L"Preferences";
    psh.nPages = show_debug ? 7 : 6;
    psh.nStartPage = (start_page >= 0) ? (UINT) start_page : (show_debug ? 6 : 0);
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
            position_dialog_on_owner(dialog);
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

// Custom Indent prompt: asks for the custom prefix (raw, escapes allowed) and
// saves it; the caller then applies the indent. Mirrors the Go To Line dialog.
static INT_PTR CALLBACK custom_indent_proc(HWND dialog, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void) lparam;
    switch (msg) {
        case WM_INITDIALOG: {
            position_dialog_on_owner(dialog);
            char *raw = settings_get_string("list_custom_indent", "");
            if (raw) {
                wchar_t *wide = utf8_to_wide(raw);
                if (wide) {
                    SetDlgItemTextW(dialog, ID_CUSTOM_INDENT_EDIT, wide);
                    free(wide);
                }
                free(raw);
            }
            SendDlgItemMessageW(dialog, ID_CUSTOM_INDENT_EDIT, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, ID_CUSTOM_INDENT_EDIT));
            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDOK: {
                    wchar_t wide[128];
                    GetDlgItemTextW(dialog, ID_CUSTOM_INDENT_EDIT, wide, 128);
                    char *raw = wide_to_utf8(wide);
                    bool ok = raw && raw[0];
                    if (ok) {
                        // Stored raw (as typed); unescaped at point of use
                        settings_set_string("list_custom_indent", raw);
                        settings_save();
                        ui_platform_notify_settings_changed();
                    }
                    free(raw);
                    EndDialog(dialog, ok ? 1 : 0);
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

static bool show_custom_indent_dialog(Window *window) {
    if (!window)
        return false;
    return DialogBoxParamW(g_hinstance, MAKEINTRESOURCEW(IDD_CUSTOM_INDENT), window->hwnd,
                           custom_indent_proc, 0) == 1;
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
                      { FCONTROL | FVIRTKEY, VK_NUMPAD0, ID_VIEW_ZOOM_RESET },
                      // Markdown indent/unindent - kept LAST so they can be
                      // trimmed from the table when not in effect
                      { FCONTROL | FVIRTKEY, VK_OEM_6, ID_LIST_INDENT },     // ']'
                      { FCONTROL | FVIRTKEY, VK_OEM_4, ID_LIST_UNINDENT } }; // '['

    int accel_count = (int) (sizeof(accel) / sizeof(accel[0]));
    // The Ctrl+]/Ctrl+[ pair only exists when the Markdown tools are on AND
    // the bracket-shortcut preference is chosen (default is Tab/Shift+Tab,
    // handled in the edit control subclass, not the accelerator table)
    if (!settings_get_bool("list_tools_enabled", false) ||
        !settings_get_bool("list_indent_shortcut_brackets", false))
        accel_count -= 2;

    HACCEL new_table = CreateAcceleratorTableW(accel, accel_count);
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

// ===========================================================================
// Markdown list tools (optional; gated on the list_tools_enabled preference)
// ===========================================================================

// Normalize CRLF/CR line breaks to '\n' in place (result is same length or
// shorter). Used on text extracted from the control before list_ops.
static void normalize_to_lf(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '\r') {
            *w++ = '\n';
            if (r[1] == '\n')
                r++; // Skip the LF of a CRLF pair
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

// Resolve the character range a list op should act on.
//   mode 0 (sort/unique): a selection spanning a line break -> those full
//     lines; otherwise the whole document.
//   mode 1 (indent):      the selected full lines, or the current line.
//   mode 2 (delimiter):   the exact selection when sel_only, else whole doc.
static void list_target_range(const Window *window, int mode, bool sel_only, LONG *out_min,
                              LONG *out_max) {
    HWND e = window->edit_hwnd;
    CHARRANGE sel = { 0, 0 };
    SendMessageW(e, EM_EXGETSEL, 0, (LPARAM) &sel);
    LONG total = edit_text_cp_length(e);
    bool has_sel = sel.cpMax > sel.cpMin;

    if (mode == 2) {
        if (sel_only && has_sel) {
            *out_min = sel.cpMin;
            *out_max = sel.cpMax;
        } else {
            *out_min = 0;
            *out_max = total;
        }
        return;
    }

    // Logical-line bounds (paragraphs), so word wrap does not fragment the
    // scope into display lines
    LONG first_start, first_end;
    get_paragraph_bounds(e, sel.cpMin, &first_start, &first_end);
    bool spans_newline = has_sel && first_end < sel.cpMax;

    // A selection ending exactly at a line start does not include that line
    LONG last_pos = sel.cpMax;
    if (spans_newline) {
        LONG ls, le;
        get_paragraph_bounds(e, last_pos, &ls, &le);
        if (ls == last_pos)
            last_pos--;
    }
    LONG last_start, last_end;
    get_paragraph_bounds(e, last_pos, &last_start, &last_end);

    if (mode == 0 && !spans_newline) {
        *out_min = 0; // Sort/Unique with no multi-line selection: whole document
        *out_max = total;
    } else {
        *out_min = first_start;
        *out_max = last_end;
    }
}

// Extract [cpMin,cpMax] as UTF-8 with '\n' line breaks (caller frees).
static char *list_extract(HWND e, LONG cpMin, LONG cpMax) {
    if (cpMax < cpMin)
        return NULL;
    LONG n = cpMax - cpMin;
    wchar_t *wbuf = malloc(((size_t) n + 1) * sizeof(wchar_t));
    if (!wbuf)
        return NULL;
    TEXTRANGEW tr;
    tr.chrg.cpMin = cpMin;
    tr.chrg.cpMax = cpMax;
    tr.lpstrText = wbuf;
    LRESULT got = SendMessageW(e, EM_GETTEXTRANGE, 0, (LPARAM) &tr);
    if (got < 0)
        got = 0;
    wbuf[got] = L'\0';
    char *utf8 = wide_to_utf8(wbuf);
    free(wbuf);
    if (utf8)
        normalize_to_lf(utf8);
    return utf8;
}

// Replace [cpMin,cpMax] with utf8_lf as one undo step; reselect the result.
static void list_replace(const Window *window, LONG cpMin, LONG cpMax, const char *utf8_lf) {
    HWND e = window->edit_hwnd;
    // The control's line breaks are CRLF on the way in; normalize whatever the
    // transform produced to LF, then emit CRLF uniformly (guards against a
    // delimiter conversion that injected raw CR/LF)
    char *norm = malloc(strlen(utf8_lf) + 1);
    if (!norm)
        return;
    strcpy(norm, utf8_lf);
    normalize_to_lf(norm);

    size_t breaks = 0;
    for (const char *p = norm; *p; p++)
        if (*p == '\n')
            breaks++;
    char *crlf = malloc(strlen(norm) + breaks + 1);
    if (!crlf) {
        free(norm);
        return;
    }
    char *w = crlf;
    for (const char *p = norm; *p; p++) {
        if (*p == '\n')
            *w++ = '\r';
        *w++ = *p;
    }
    *w = '\0';
    free(norm);

    wchar_t *wide = utf8_to_wide(crlf);
    free(crlf);
    if (!wide)
        return;

    CHARRANGE target = { cpMin, cpMax };
    SendMessageW(e, EM_EXSETSEL, 0, (LPARAM) &target);
    SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM) wide); // TRUE = single undo unit
    free(wide);

    CHARRANGE after = { 0, 0 };
    SendMessageW(e, EM_EXGETSEL, 0, (LPARAM) &after);
    CHARRANGE reselect = { cpMin, after.cpMax };
    SendMessageW(e, EM_EXSETSEL, 0, (LPARAM) &reselect);
    SendMessageW(e, EM_SCROLLCARET, 0, 0);
}

// The unescaped custom indent prefix, or NULL if none is configured. Caller
// frees.
static char *get_custom_indent(void) {
    char *raw = settings_get_string("list_custom_indent", NULL);
    if (!raw || !raw[0]) {
        free(raw);
        return NULL;
    }
    char *unescaped = list_unescape(raw);
    free(raw);
    return unescaped;
}

// op: 0 sort, 1 unique, 2 indent, 3 unindent. arg = descending (sort) or
// ListIndentFormat (indent/unindent).
static void list_do(const Window *window, int op, int arg) {
    if (!window || !window->edit_hwnd)
        return;
    int mode = (op == 0 || op == 1) ? 0 : 1;
    LONG cpMin, cpMax;
    list_target_range(window, mode, false, &cpMin, &cpMax);
    char *text = list_extract(window->edit_hwnd, cpMin, cpMax);
    if (!text)
        return;

    char *out = NULL;
    switch (op) {
        case 0:
            out = list_sort_lines(text, arg != 0,
                                  settings_get_bool("list_sort_case_sensitive", false));
            break;
        case 1:
            out = list_unique_lines(text);
            break;
        case 2:
        case 3: {
            // The custom prefix is passed for every format, so lines carrying
            // a custom bullet still deepen/unindent under a built-in format
            char *custom = get_custom_indent();
            out = (op == 2) ? list_indent_lines(text, (ListIndentFormat) arg, custom)
                            : list_unindent_lines(text, (ListIndentFormat) arg, custom);
            free(custom);
            break;
        }
        default:
            break;
    }
    free(text);
    if (out) {
        list_replace(window, cpMin, cpMax, out);
        free(out);
    }
}

// Indent/unindent with the configured default format (menu item, Tab, or
// Ctrl+]/[). A Custom default with no prefix configured prompts first.
static void list_indent_default(Window *window, bool unindent) {
    int fmt = settings_get_int("list_default_indent_format", 0);
    if (fmt < 0 || fmt >= INDENT_FORMAT_COUNT)
        fmt = 0;
    if (fmt == LIST_INDENT_CUSTOM) {
        char *custom = get_custom_indent();
        if (!custom) {
            if (!show_custom_indent_dialog(window))
                return;
        } else {
            free(custom);
        }
    }
    list_do(window, unindent ? 3 : 2, fmt);
}

// Append the Markdown-tool items into an existing menu (used by both the
// Markdown menu-bar popup and the right-click context menu).
static void populate_list_menu(HMENU menu) {
    // The indent/unindent shortcut labels track the active binding
    bool brackets = settings_get_bool("list_indent_shortcut_brackets", false);

    HMENU sort = CreatePopupMenu();
    AppendMenuW(sort, MF_STRING, ID_LIST_SORT_ASC, L"&Ascending");
    AppendMenuW(sort, MF_STRING, ID_LIST_SORT_DESC, L"&Descending");
    AppendMenuW(sort, MF_SEPARATOR, 0, NULL);
    AppendMenuW(sort, MF_STRING, ID_LIST_SORT_CASE, L"&Case sensitive");
    CheckMenuItem(sort, ID_LIST_SORT_CASE,
                  settings_get_bool("list_sort_case_sensitive", false) ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, MF_STRING | MF_POPUP, (UINT_PTR) sort, L"&Sort");
    AppendMenuW(menu, MF_STRING, ID_LIST_UNIQUE, L"&Unique");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_LIST_CONVERT_DELIM, L"Convert &Delimiters...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    HMENU indent = CreatePopupMenu();
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT,
                brackets ? L"&Indent (default)\tCtrl+]" : L"&Indent (default)\tTab");
    AppendMenuW(indent, MF_SEPARATOR, 0, NULL);
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 0, L"&Spaces");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 1, L"&Tab");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 2, L"&Asterisk (\"* \")");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 3, L"&Hyphen (\"- \")");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 4, L"Asterisk, &leading space (\" * \")");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 5, L"Hyphen, l&eading space (\" - \")");
    AppendMenuW(indent, MF_STRING, ID_LIST_INDENT_BASE + 6, L"&Custom...");
    AppendMenuW(menu, MF_STRING | MF_POPUP, (UINT_PTR) indent, L"Inden&t");
    AppendMenuW(menu, MF_STRING, ID_LIST_UNINDENT,
                brackets ? L"&Unindent\tCtrl+[" : L"&Unindent\tShift+Tab");
}

// Insert or remove the top-level Markdown menu to match the preference (always
// rebuilt so shortcut labels stay current). Called at window creation and
// whenever settings change (live toggle).
static void apply_list_tools_menu(Window *window) {
    if (!window || !window->hmenu)
        return;
    bool enabled = settings_get_bool("list_tools_enabled", false);
    if (window->list_menu_present) {
        HMENU old = GetSubMenu(window->hmenu, 3); // Markdown sits after Format
        RemoveMenu(window->hmenu, 3, MF_BYPOSITION);
        if (old)
            DestroyMenu(old);
        window->list_menu_present = false;
    }
    if (enabled) {
        HMENU list = CreatePopupMenu();
        populate_list_menu(list);
        InsertMenuW(window->hmenu, 3, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR) list,
                    L"&Markdown");
        window->list_menu_present = true;
    }
    if (window->hwnd)
        DrawMenuBar(window->hwnd);
}

static INT_PTR CALLBACK convert_delim_proc(HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam) {
    static Window *win = NULL;
    switch (msg) {
        case WM_INITDIALOG: {
            win = (Window *) lparam;
            position_dialog_on_owner(dlg);
            static const wchar_t *const presets[] = { L",",   L";",      L"|", L"\\t",
                                                      L"\\n", L"\\r\\n", L" " };
            HWND from = GetDlgItem(dlg, ID_DELIM_FROM);
            HWND to = GetDlgItem(dlg, ID_DELIM_TO);
            for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
                SendMessageW(from, CB_ADDSTRING, 0, (LPARAM) presets[i]);
                SendMessageW(to, CB_ADDSTRING, 0, (LPARAM) presets[i]);
            }
            SetDlgItemTextW(dlg, ID_DELIM_FROM, L",");
            SetDlgItemTextW(dlg, ID_DELIM_TO, L"\\r\\n"); // OS default line ending
            bool has_sel = win && ui_platform_has_selection(win);
            EnableWindow(GetDlgItem(dlg, ID_DELIM_SEL_ONLY), has_sel);
            CheckDlgButton(dlg, ID_DELIM_SEL_ONLY, has_sel ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wparam) == IDOK) {
                wchar_t wfrom[256] = L"", wto[256] = L"";
                GetDlgItemTextW(dlg, ID_DELIM_FROM, wfrom, 256);
                GetDlgItemTextW(dlg, ID_DELIM_TO, wto, 256);
                bool sel_only = IsDlgButtonChecked(dlg, ID_DELIM_SEL_ONLY) == BST_CHECKED;

                char *from8 = wide_to_utf8(wfrom);
                char *to8 = wide_to_utf8(wto);
                char *from_u = from8 ? list_unescape(from8) : NULL;
                char *to_u = to8 ? list_unescape(to8) : NULL;
                if (win && from_u && from_u[0] != '\0') {
                    LONG cpMin, cpMax;
                    list_target_range(win, 2, sel_only, &cpMin, &cpMax);
                    char *text = list_extract(win->edit_hwnd, cpMin, cpMax);
                    if (text) {
                        char *out = list_replace_all(text, from_u, to_u ? to_u : "");
                        if (out) {
                            list_replace(win, cpMin, cpMax, out);
                            free(out);
                        }
                        free(text);
                    }
                }
                free(from8);
                free(to8);
                free(from_u);
                free(to_u);
                EndDialog(dlg, 1);
                return TRUE;
            }
            if (LOWORD(wparam) == IDCANCEL) {
                EndDialog(dlg, 0);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(dlg, 0);
            return TRUE;
    }
    return FALSE;
}

static void show_convert_delim_dialog(Window *window) {
    if (!window)
        return;
    DialogBoxParamW(g_hinstance, MAKEINTRESOURCEW(IDD_CONVERT_DELIM), window->hwnd,
                    convert_delim_proc, (LPARAM) window);
}

// Reflect update availability in the Help menu: a "●" after the Help title and
// a dynamic "Update Available" item that opens Preferences on the Updates tab.
// Shown only when the mode is not "off" and a newer, non-skipped release is
// known. Safe to call repeatedly. Help is always the last top-level menu (the
// optional Markdown menu inserts earlier), so locate it by count.
static void apply_update_indicator(Window *window) {
    if (!window || !window->hmenu)
        return;
    int help_pos = GetMenuItemCount(window->hmenu) - 1;
    if (help_pos < 0)
        return;

    char cur[32];
    current_version_string(cur, sizeof(cur));
    char *mode = settings_get_string("update_mode", "off");
    char *latest = settings_get_string("update_latest_version", "");
    char *skipped = settings_get_string("update_skipped_version", "");
    bool avail = strcmp(mode, "off") != 0 && update_is_newer_unskipped(cur, latest, skipped);
    free(mode);
    free(latest);
    free(skipped);

    // Top-level Help title
    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = avail ? L"&Help  \x25CF" : L"&Help";
    SetMenuItemInfoW(window->hmenu, (UINT) help_pos, TRUE, &mii);

    // The dynamic "Update Available" item at the top of the Help submenu
    HMENU hhelp = GetSubMenu(window->hmenu, help_pos);
    if (hhelp) {
        if (avail && !window->update_item_present) {
            InsertMenuW(hhelp, 0, MF_BYPOSITION | MF_STRING, ID_HELP_UPDATE_AVAILABLE,
                        L"&Update Available...");
            InsertMenuW(hhelp, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            window->update_item_present = true;
        } else if (!avail && window->update_item_present) {
            RemoveMenu(hhelp, 1, MF_BYPOSITION); // separator
            RemoveMenu(hhelp, 0, MF_BYPOSITION); // item
            window->update_item_present = false;
        }
    }
    if (window->hwnd)
        DrawMenuBar(window->hwnd);
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
    AppendMenuW(hhelp, MF_STRING, ID_HELP_CHECK_UPDATES, L"Check for &Updates...");
    AppendMenuW(hhelp, MF_SEPARATOR, 0, NULL);
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
    window->list_menu_present = false;
    window->update_item_present = false;

    CheckMenuItem(hmenu, ID_FORMAT_WORD_WRAP,
                  window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hmenu, ID_VIEW_STATUS_BAR,
                  window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
    rebuild_recent_menu(window);
    apply_list_tools_menu(window);  // Insert the Markdown menu when enabled
    apply_update_indicator(window); // After Markdown, so Help is still last
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

    // With the Markdown tools on, Ctrl+X without a selection cuts the whole
    // line, so Cut only needs a non-empty document
    bool can_cut = has_selection || (settings_get_bool("list_tools_enabled", false) && has_text);

    EnableMenuItem(menu, ID_EDIT_UNDO, ui_platform_can_undo(window) ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_REDO, ui_platform_can_redo(window) ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_CUT, can_cut ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_COPY, has_selection ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_DELETE, has_selection ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_PASTE, can_paste ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND, has_text ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND_NEXT,
                   (has_text && g_search_eff[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_FIND_PREV,
                   (has_text && g_search_eff[0] != L'\0') ? enabled : disabled);
    EnableMenuItem(menu, ID_EDIT_REPLACE, has_text ? enabled : disabled);

    // Markdown tools (only present in the menu when enabled; harmless
    // otherwise). They operate on lines, so require a non-empty document.
    UINT list_state = has_text ? enabled : disabled;
    EnableMenuItem(menu, ID_LIST_SORT_ASC, list_state);
    EnableMenuItem(menu, ID_LIST_SORT_DESC, list_state);
    EnableMenuItem(menu, ID_LIST_UNIQUE, list_state);
    EnableMenuItem(menu, ID_LIST_CONVERT_DELIM, list_state);
    EnableMenuItem(menu, ID_LIST_INDENT, list_state);
    EnableMenuItem(menu, ID_LIST_UNINDENT, list_state);
    for (UINT i = 0; i < 7; i++)
        EnableMenuItem(menu, ID_LIST_INDENT_BASE + i, list_state);
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

    // List tools mirror the List menu when enabled
    if (settings_get_bool("list_tools_enabled", false)) {
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        populate_list_menu(menu);
    }

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
    // update_status_bar (via resize_controls) refreshes parts 1-5 only; part 0
    // holds the optional counts, which the debounce skips while hidden, so
    // repopulate/clear it explicitly on a visibility change
    apply_counts_pref(window);
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

    // Explicit indent-format items from the Markdown/Indent submenu
    if (command >= ID_LIST_INDENT_BASE && command <= ID_LIST_INDENT_BASE + 5) {
        list_do(window, 2, command - ID_LIST_INDENT_BASE);
        return;
    }
    // Custom... always prompts (prefilled with the saved prefix), then indents
    if (command == ID_LIST_INDENT_BASE + LIST_INDENT_CUSTOM) {
        if (show_custom_indent_dialog(window))
            list_do(window, 2, LIST_INDENT_CUSTOM);
        return;
    }

    switch (command) {
        case ID_LIST_SORT_ASC:
            list_do(window, 0, 0);
            break;
        case ID_LIST_SORT_DESC:
            list_do(window, 0, 1);
            break;
        case ID_LIST_SORT_CASE: {
            bool cs = !settings_get_bool("list_sort_case_sensitive", false);
            settings_set_bool("list_sort_case_sensitive", cs);
            settings_save();
            if (window->hmenu)
                CheckMenuItem(window->hmenu, ID_LIST_SORT_CASE, cs ? MF_CHECKED : MF_UNCHECKED);
            ui_platform_notify_settings_changed();
            break;
        }
        case ID_LIST_UNIQUE:
            list_do(window, 1, 0);
            break;
        case ID_LIST_CONVERT_DELIM:
            show_convert_delim_dialog(window);
            break;
        case ID_LIST_INDENT:
            list_indent_default(window, false);
            break;
        case ID_LIST_UNINDENT:
            list_indent_default(window, true);
            break;
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
            show_preferences_dialog(window, -1, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            break;
        case ID_FILE_PREFERENCES_DEBUG: // Ctrl+Shift+. accelerator
            show_preferences_dialog(window, -1, true);
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
            if (g_search_eff[0] != L'\0') {
                find_next(window, true);
            } else {
                ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            }
            break;
        case ID_EDIT_FIND_PREV:
            if (g_search_eff[0] != L'\0') {
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
        case ID_HELP_CHECK_UPDATES:
            start_update_check(window, true); // Manual: surface the result loudly
            break;
        case ID_HELP_UPDATE_AVAILABLE:
            show_preferences_dialog(window, PREFS_PAGE_UPDATES, false);
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

// Recompute the optional word/char/line counts and show them in the
// leftmost status part. That part is shared with transient messages
// ("Match 3 of 7"), which win until the next text change refreshes counts.
static void update_text_counts(Window *window) {
    if (!window || !window->status_hwnd || !window->status_bar_visible)
        return;
    if (!settings_get_bool("status_show_counts", false))
        return;

    char *text = ui_platform_get_text(window);
    size_t words = 0, chars = 0, lines = 0;
    file_count_text_stats(text, &words, &chars, &lines);
    free(text);

    wchar_t msg[96];
    _snwprintf(msg, 95, L"%lu words, %lu chars, %lu lines", (unsigned long) words,
               (unsigned long) chars, (unsigned long) lines);
    msg[95] = L'\0';
    set_status_part(window, 0, msg);
}

// Debounced: re-armed on every change, so the full-document scan runs once
// after typing, a paste or a large open settles rather than per keystroke
static void schedule_counts_update(Window *window) {
    if (!window || !window->hwnd)
        return;
    if (!settings_get_bool("status_show_counts", false))
        return;
    KillTimer(window->hwnd, NPAD_COUNTS_TIMER_ID);
    SetTimer(window->hwnd, NPAD_COUNTS_TIMER_ID, 350, NULL);
}

// Apply the counts preference right away: populate the part, or clear it
static void apply_counts_pref(Window *window) {
    if (!window)
        return;
    if (settings_get_bool("status_show_counts", false)) {
        update_text_counts(window);
    } else if (window->status_hwnd) {
        set_status_part(window, 0, L"");
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
    apply_new_window_pref(window); // Also rebuilds accelerators (list Ctrl+]/[ gating)
    rebuild_recent_menu(window);
    apply_list_tools_menu(window);  // Insert/remove the Markdown menu per the pref
    apply_update_indicator(window); // Mode/skip/latest may have changed live

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
    apply_counts_pref(window); // Show or clear the optional counts display

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
                    if (notification == EN_CHANGE) {
                        // Counts refresh on programmatic changes too (open,
                        // transforms), not only user edits
                        schedule_counts_update(window);
                        // Text changed under active highlights: re-apply
                        // after the edit settles
                        if (g_highlights_applied && g_find_dialog) {
                            KillTimer(hwnd, NPAD_HIGHLIGHT_TIMER_ID);
                            SetTimer(hwnd, NPAD_HIGHLIGHT_TIMER_ID, 300, NULL);
                        }
                        if (!window->setting_text_programmatically) {
                            window->is_modified = true;
                            ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                        }
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
                // Optional launch-time update check (off by default): fires
                // once, after first paint, only for the primary window, and
                // only when a surfacing mode is set. Failures are silent.
                if (window == g_main_window && settings_get_bool("update_check_on_launch", false)) {
                    char *mode = settings_get_string("update_mode", "off");
                    bool on = strcmp(mode, "off") != 0;
                    free(mode);
                    if (on)
                        start_update_check(window, false);
                }
            } else if (window && wparam == NPAD_STATUS_TIMER_ID) {
                KillTimer(hwnd, NPAD_STATUS_TIMER_ID); // One-shot (coalescing)
                window->status_update_pending = false;
                update_status_bar(window);
            } else if (window && wparam == NPAD_COUNTS_TIMER_ID) {
                KillTimer(hwnd, NPAD_COUNTS_TIMER_ID); // One-shot (debounced)
                update_text_counts(window);
            } else if (window && wparam == NPAD_HIGHLIGHT_TIMER_ID) {
                KillTimer(hwnd, NPAD_HIGHLIGHT_TIMER_ID); // One-shot (debounced)
                refresh_highlights(window, g_highlight_all && g_find_dialog != NULL);
            }
            return 0;
        }

        case NPAD_WM_UPDATE_CHECKED: {
            if (lparam)
                handle_update_checked(window, (UpdateCheckResult *) lparam);
            return 0;
        }

        case NPAD_WM_UPDATE_PROGRESS: {
            if (window) {
                char progress[64];
                snprintf(progress, sizeof(progress), "Downloading update... %d%%", (int) wparam);
                set_status_message(window, progress);
            }
            return 0;
        }

        case NPAD_WM_UPDATE_DOWNLOADED: {
            if (lparam)
                handle_update_downloaded(window, (UpdateDownloadResult *) lparam);
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
