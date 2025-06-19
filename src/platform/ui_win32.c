/*
 * npad - Win32 UI Implementation
 * Windows-specific UI implementation using Win32 API
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
#include <shellscalingapi.h>
#include <uxtheme.h>

#include "../ui_interface.h"
#include "../main.h"
#include "../core/error.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define constants for older Windows SDKs
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// Function prototypes for DPI-aware functions (Windows 10+)
typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
typedef BOOL (WINAPI *SystemParametersInfoForDpiFunc)(UINT, UINT, PVOID, UINT, UINT);

// Function pointer instances
static GetDpiForWindowFunc g_GetDpiForWindow = NULL;
static SystemParametersInfoForDpiFunc g_SystemParametersInfoForDpi = NULL;

// Window class name
#define NPAD_WINDOW_CLASS "NpadMainWindow"

// Control IDs
#define ID_EDIT_CONTROL 1001
#define ID_STATUS_BAR 1002
#define ID_FILE_NEW 2001
#define ID_FILE_OPEN 2002
#define ID_FILE_SAVE 2003
#define ID_FILE_SAVE_AS 2004
#define ID_FILE_EXIT 2005
#define ID_EDIT_UNDO 2101
#define ID_EDIT_REDO 2102
#define ID_EDIT_CUT 2103
#define ID_EDIT_COPY 2104
#define ID_EDIT_PASTE 2105
#define ID_EDIT_SELECT_ALL 2106
#define ID_EDIT_FIND 2107
#define ID_EDIT_REPLACE 2108
#define ID_EDIT_GOTO_LINE 2109
#define ID_FORMAT_WORD_WRAP 2201
#define ID_FORMAT_FONT 2202
#define ID_VIEW_STATUS_BAR 2301
// #define ID_VIEW_DARK_MODE 2302  // Commented out - not yet implemented
#define ID_HELP_ABOUT 2401

// Window structure
typedef struct Window {
    HWND hwnd;
    HWND edit_hwnd;
    HWND status_hwnd;
    HMENU hmenu;
    HACCEL haccel;
    bool is_modified;
    bool setting_text_programmatically; // Flag to prevent spurious change notifications
    char *current_file;
    bool word_wrap_enabled;
    bool status_bar_visible;
    int zoom_level;
    int font_size; // For font size tracking
} Window;

// Dialog structure
typedef struct Dialog {
    HWND hwnd;
    Window *parent;
} Dialog;

// Global variables
static HINSTANCE g_hinstance = NULL;
static bool g_dark_mode = false;
static Window *g_main_window = NULL;
static HMODULE g_richedit_lib = NULL;

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window *window);
static void handle_command(Window *window, WORD command);
static void update_title(Window *window);
static void update_status_bar(Window *window);
static void update_scrollbars(Window *window);
static void resize_controls(Window *window);
static bool register_window_class(void);
static void apply_theme(Window *window);
static void set_font_size(Window *window, int size);
static void show_font_dialog(Window *window);
static bool InputBox(HWND parent, const char *title, const char *prompt, char *buffer,
                     int buffer_size);

// Helper function to get current DPI for a window
static UINT get_window_dpi(HWND hwnd) {
    if (hwnd && g_GetDpiForWindow) {
        return g_GetDpiForWindow(hwnd);
    }
    
    // Fallback to system DPI
    HDC hdc = GetDC(NULL);
    UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

// Platform initialization
bool ui_platform_init(void) {
    g_hinstance = GetModuleHandle(NULL);

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&icex)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                         "Failed to initialize common controls");
        return false;
    }    // Load Rich Edit library
    g_richedit_lib = LoadLibrary(TEXT("riched20.dll"));
    if (!g_richedit_lib) {
        NPAD_ERROR_WARNING(
            NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
            "Failed to load Rich Edit library - falling back to standard edit control");
    }    // Load DPI-aware functions for Windows 10+
    HMODULE user32 = GetModuleHandle(TEXT("user32.dll"));
    if (user32) {
        // Use union to avoid function pointer cast warnings
        union { FARPROC proc; GetDpiForWindowFunc func; } getDpiForWindow;
        getDpiForWindow.proc = GetProcAddress(user32, "GetDpiForWindow");
        g_GetDpiForWindow = getDpiForWindow.func;
        
        union { FARPROC proc; SystemParametersInfoForDpiFunc func; } sysParamsForDpi;
        sysParamsForDpi.proc = GetProcAddress(user32, "SystemParametersInfoForDpi");
        g_SystemParametersInfoForDpi = sysParamsForDpi.func;
    }

    // Register window class
    if (!register_window_class()) {
        return false;
    }

    // Check for system dark mode support - default to light mode
    g_dark_mode = false;

    HKEY hkey;
    DWORD value = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyEx(HKEY_CURRENT_USER,
                     "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                     KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD type = REG_DWORD;
        if (RegQueryValueEx(hkey, "AppsUseLightTheme", NULL, &type, (LPBYTE) &value, &size) ==
                ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(DWORD)) {
            g_dark_mode = (value == 0);
        }
        RegCloseKey(hkey);
    }

    return true;
}

void ui_platform_cleanup(void) {
    if (g_main_window) {
        if (g_main_window->current_file) {
            free(g_main_window->current_file);
        }
        free(g_main_window);
        g_main_window = NULL;
    }

    // Free Rich Edit library
    if (g_richedit_lib) {
        FreeLibrary(g_richedit_lib);
        g_richedit_lib = NULL;
    }
}

int ui_platform_message_loop(void) {
    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0)) {
        // Check for accelerator keys first, before any other processing
        bool accelerator_handled = false;
        if (g_main_window && g_main_window->haccel && g_main_window->hwnd) {
            accelerator_handled =
                TranslateAccelerator(g_main_window->hwnd, g_main_window->haccel, &msg);
        }

        if (!accelerator_handled) {
            // Handle dialog messages for modal dialogs
            if (!IsDialogMessage(g_main_window ? g_main_window->hwnd : NULL, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    return (int) msg.wParam;
}

void ui_platform_quit(void) {
    PostQuitMessage(0);
}

Window *ui_platform_create_main_window(void) {
    Window *window = malloc(sizeof(Window));
    if (!window)
        return NULL;

    memset(window, 0, sizeof(Window));

    window->hwnd = CreateWindowEx(WS_EX_ACCEPTFILES | WS_EX_WINDOWEDGE, NPAD_WINDOW_CLASS, "npad",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 800, 600, NULL, NULL, g_hinstance, window);

    if (!window->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Window creation",
                         "Failed to create main window");
        free(window);
        return NULL;
    }

    // Set window icon
    HICON icon = LoadIcon(g_hinstance, MAKEINTRESOURCE(IDI_NPAD));
    if (icon) {
        SendMessage(window->hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
        SendMessage(window->hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);
    }

    window->edit_hwnd =
        CreateWindowEx(0, RICHEDIT_CLASS, "",
                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                           ES_MULTILINE | ES_NOHIDESEL,
                       0, 0, 0, 0, window->hwnd, (HMENU) ID_EDIT_CONTROL, g_hinstance, NULL);

    if (!window->edit_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Edit control creation",
                         "Failed to create edit control");
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    window->font_size = 11; // Default 11pt font like notepad
    set_font_size(window, window->font_size);    SendMessage(window->edit_hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));    // Set unlimited text length
    SendMessage(window->edit_hwnd, EM_LIMITTEXT, 0, 0);    // Enable change notifications for RichEdit control
    SendMessage(window->edit_hwnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

    // Set to plain text mode (not RTF) to behave like a standard text editor
    SendMessage(window->edit_hwnd, EM_SETTEXTMODE, TM_PLAINTEXT, 0);

    // Create status bar with standard styling
    window->status_hwnd =
        CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0,
                       0, window->hwnd, (HMENU) ID_STATUS_BAR, g_hinstance, NULL);

    if (!window->status_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Status bar creation",
                         "Failed to create status bar");
        DestroyWindow(window->edit_hwnd);
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    // Configure status bar parts (from left to right: message, line/col, zoom, encoding)
    int status_parts[] = { 200, 350, 450, -1 }; // -1 means remaining space for last part
    SendMessage(window->status_hwnd, SB_SETPARTS, 4, (LPARAM) status_parts);

    SendMessage(window->status_hwnd, SB_SETTEXT, 0, (LPARAM) "Ready");
    SendMessage(window->status_hwnd, SB_SETTEXT, 1, (LPARAM) "Ln 1, Col 1");
    SendMessage(window->status_hwnd, SB_SETTEXT, 2, (LPARAM) "100%");
    SendMessage(window->status_hwnd, SB_SETTEXT, 3, (LPARAM) "UTF-8");

    // Force status bar to be visible and properly sized
    ShowWindow(window->status_hwnd, SW_SHOW);
    UpdateWindow(window->status_hwnd);    // Initialize window state
    window->word_wrap_enabled = true;
    window->status_bar_visible = true;
    window->zoom_level = 100;
    window->is_modified = false;
    window->setting_text_programmatically = false;// Create menu and accelerators
    create_menu(window);

    // Update initial menu checkmarks
    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_FORMAT_WORD_WRAP,
                      window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(window->hmenu, ID_VIEW_STATUS_BAR,
                      window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
    }

    // Create accelerator table
    ACCEL accel[] = { { FCONTROL | FVIRTKEY, 'N', ID_FILE_NEW },
                      { FCONTROL | FVIRTKEY, 'O', ID_FILE_OPEN },
                      { FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE },
                      { FCONTROL | FVIRTKEY, 'Z', ID_EDIT_UNDO },
                      { FCONTROL | FVIRTKEY, 'X', ID_EDIT_CUT },
                      { FCONTROL | FVIRTKEY, 'C', ID_EDIT_COPY },
                      { FCONTROL | FVIRTKEY, 'V', ID_EDIT_PASTE },
                      { FCONTROL | FVIRTKEY, 'A', ID_EDIT_SELECT_ALL },
                      { FCONTROL | FVIRTKEY, 'F', ID_EDIT_FIND },
                      { FCONTROL | FVIRTKEY, 'H', ID_EDIT_REPLACE },
                      { FCONTROL | FVIRTKEY, 'G', ID_EDIT_GOTO_LINE },
                      { FALT | FVIRTKEY, 'Z', ID_FORMAT_WORD_WRAP } };
    window->haccel = CreateAcceleratorTable(accel, sizeof(accel) / sizeof(accel[0]));

    if (!window->haccel) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "Accelerator table creation",
                           "Failed to create accelerator table - keyboard shortcuts will not work");
    }

    // Apply theme and update scrollbars
    apply_theme(window);

    update_scrollbars(window);

    // Set as main window
    g_main_window = window;

    return window;
}

void ui_platform_destroy_window(Window *window) {
    if (!window)
        return;

    if (window->current_file) {
        free(window->current_file);
        window->current_file = NULL;
    }

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
        SetWindowText(window->hwnd, title);
    }
}

void ui_platform_get_window_size(Window *window, int *width, int *height) {
    if (window && window->hwnd && width && height) {
        RECT rect;
        if (GetClientRect(window->hwnd, &rect)) {
            *width = rect.right - rect.left;
            *height = rect.bottom - rect.top;
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
        RECT rect;
        if (GetWindowRect(window->hwnd, &rect)) {
            *x = rect.left;
            *y = rect.top;
        } else {
            *x = 0;
            *y = 0;
        }
    }
}

void ui_platform_set_window_position(Window *window, int x, int y) {
    if (window && window->hwnd) {
        SetWindowPos(window->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void ui_platform_set_text(Window *window, const char *text) {
    if (window && window->edit_hwnd && text) {
        window->setting_text_programmatically = true;
        SetWindowText(window->edit_hwnd, text);
        window->setting_text_programmatically = false;
        window->is_modified = false;
        update_title(window);
        update_status_bar(window);
        update_scrollbars(window);
    }
}

char *ui_platform_get_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    int length = GetWindowTextLength(window->edit_hwnd);
    if (length == 0) {
        char *empty = malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }

    char *text = malloc(length + 1);
    if (!text) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, 0, "Text retrieval",
                         "Failed to allocate memory for text buffer");
        return NULL;
    }

    int actual_length = GetWindowText(window->edit_hwnd, text, length + 1);
    if (actual_length != length) {
        text[actual_length] = '\0';
    }
    return text;
}

void ui_platform_clear_text(Window *window) {
    if (window && window->edit_hwnd) {
        window->setting_text_programmatically = true;
        SetWindowText(window->edit_hwnd, "");
        window->setting_text_programmatically = false;
        window->is_modified = false;
        update_title(window);
        update_status_bar(window);
        update_scrollbars(window);
    }
}

bool ui_platform_has_selection(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;

    DWORD start = 0, end = 0;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM) &start, (LPARAM) &end);
    return start != end;
}

char *ui_platform_get_selected_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    DWORD start = 0, end = 0;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM) &start, (LPARAM) &end);

    if (start == end)
        return NULL;

    DWORD length = end - start;
    char *text = malloc(length + 1);
    if (!text) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, 0, "Selected text retrieval",
                         "Failed to allocate memory for selected text");
        return NULL;
    }

    LRESULT result = SendMessage(window->edit_hwnd, EM_GETSELTEXT, 0, (LPARAM) text);
    if (result == 0) {
        free(text);
        return NULL;
    }

    return text;
}

void ui_platform_select_all(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_SETSEL, 0, -1);
    }
}

void ui_platform_set_cursor_position(Window *window, int position) {
    if (window && window->edit_hwnd && position >= 0) {
        SendMessage(window->edit_hwnd, EM_SETSEL, position, position);
        SendMessage(window->edit_hwnd, EM_SCROLLCARET, 0, 0);
        update_status_bar(window);
    }
}

int ui_platform_get_cursor_position(Window *window) {
    if (!window || !window->edit_hwnd)
        return 0;

    DWORD start = 0, end = 0;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM) &start, (LPARAM) &end);
    return (int) start;
}

void ui_platform_cut(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_CUT, 0, 0);
        if (!window->is_modified) {
            window->is_modified = true;
            update_title(window);
        }
        update_scrollbars(window);
    }
}

void ui_platform_copy(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_COPY, 0, 0);
    }
}

void ui_platform_paste(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_PASTE, 0, 0);
        if (!window->is_modified) {
            window->is_modified = true;
            update_title(window);
        }
        update_scrollbars(window);
    }
}

void ui_platform_undo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_UNDO, 0, 0);
        update_scrollbars(window);
    }
}

void ui_platform_redo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_REDO, 0, 0);
        update_scrollbars(window);
    }
}

bool ui_platform_can_undo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessage(window->edit_hwnd, EM_CANUNDO, 0, 0) != 0;
}

bool ui_platform_can_redo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessage(window->edit_hwnd, EM_CANREDO, 0, 0) != 0;
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) params;
    OPENFILENAME ofn;
    char filename[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? parent->hwnd : NULL;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (GetOpenFileName(&ofn)) {
        char *result = malloc(strlen(filename) + 1);
        if (result) {
            strcpy(result, filename);
        }
        return result;
    }

    return NULL;
}

char *ui_platform_show_save_dialog(Window *parent, const FileDialogParams *params) {
    (void) params;
    OPENFILENAME ofn;
    char filename[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? parent->hwnd : NULL;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (GetSaveFileName(&ofn)) {
        char *result = malloc(strlen(filename) + 1);
        if (result) {
            strcpy(result, filename);
        }
        return result;
    }

    return NULL;
}

bool ui_platform_show_message_box(Window *parent, const char *title, const char *message,
                                  bool is_question) {
    HWND hwnd = parent ? parent->hwnd : NULL;
    UINT type = is_question ? (MB_YESNO | MB_ICONQUESTION) : (MB_OK | MB_ICONINFORMATION);

    int result = MessageBox(hwnd, message, title, type);
    return is_question ? (result == IDYES) : true;
}

void ui_platform_show_about_dialog(Window *parent) {
    HWND hwnd = parent ? parent->hwnd : NULL;

    const char *message = "npad " NPAD_VERSION "\n\n"
                          "A lightweight, cross-platform text editor\n"
                          "inspired by classic Windows Notepad.\n\n"
                          "Author: Platima\n"
                          "https://github.com/platima/npad";    // Create a custom message box with our icon
    MSGBOXPARAMS mbp;
    ZeroMemory(&mbp, sizeof(mbp));
    mbp.cbSize = sizeof(mbp);
    mbp.hwndOwner = hwnd;
    mbp.hInstance = g_hinstance;
    mbp.lpszText = message;
    mbp.lpszCaption = "About npad";
    mbp.dwStyle = MB_OK | MB_USERICON;
    mbp.lpszIcon = MAKEINTRESOURCE(IDI_NPAD);

    MessageBoxIndirect(&mbp);
}

Dialog *ui_platform_show_find_dialog(Window *parent) {
    Dialog *dialog = malloc(sizeof(Dialog));
    if (!dialog)
        return NULL;

    dialog->parent = parent;

    dialog->hwnd = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, "STATIC", "Find",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                  300, 100, parent ? parent->hwnd : NULL, NULL, g_hinstance, NULL);

    if (!dialog->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Find dialog creation",
                         "Failed to create find dialog");
        free(dialog);
        return NULL;
    }

    ShowWindow(dialog->hwnd, SW_SHOW);
    return dialog;
}

Dialog *ui_platform_show_replace_dialog(Window *parent) {
    (void) parent;
    return NULL;
}

void ui_platform_close_dialog(Dialog *dialog) {
    if (dialog) {
        if (dialog->hwnd) {
            DestroyWindow(dialog->hwnd);
        }
        free(dialog);
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
    return true;
}

bool ui_platform_is_text_modified(Window *window) {
    return window ? window->is_modified : false;
}

void ui_platform_set_text_modified(Window *window, bool modified) {
    if (window) {
        window->is_modified = modified;
        update_title(window);
    }
}

int ui_platform_get_line_count(Window *window) {
    if (!window || !window->edit_hwnd)
        return 0;
    return (int) SendMessage(window->edit_hwnd, EM_GETLINECOUNT, 0, 0);
}

void ui_platform_get_cursor_line_column(Window *window, int *line, int *column) {
    if (!window || !window->edit_hwnd || !line || !column) {
        if (line)
            *line = 0;
        if (column)
            *column = 0;
        return;
    }

    int pos = ui_platform_get_cursor_position(window);
    *line = (int) SendMessage(window->edit_hwnd, EM_LINEFROMCHAR, pos, 0) + 1;
    int line_start = (int) SendMessage(window->edit_hwnd, EM_LINEINDEX, *line - 1, 0);
    *column = pos - line_start + 1;
}

void *ui_platform_get_native_handle(Window *window) {
    return window ? window->hwnd : NULL;
}

// Helper functions

static bool register_window_class(void) {
    WNDCLASSEX wc;

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinstance;
    wc.hIcon = LoadIcon(g_hinstance, MAKEINTRESOURCE(IDI_NPAD));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NPAD_WINDOW_CLASS;
    wc.hIconSm = LoadIcon(g_hinstance, MAKEINTRESOURCE(IDI_NPAD));

    ATOM result = RegisterClassEx(&wc);
    if (result == 0) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "Window class registration",
                         "Failed to register window class");
        return false;
    }
    return true;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window *window = (Window *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *) lparam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
            return 0;
        }        case WM_SIZE: {
            if (window) {
                resize_controls(window);
            }
            return 0;
        }        case WM_COMMAND: {
            if (window) {
                WORD notification = HIWORD(wparam);
                WORD control_id = LOWORD(wparam);
                
                if (control_id == ID_EDIT_CONTROL) {
                    if (notification == EN_CHANGE) {
                        // Ignore change notifications during programmatic text setting
                        if (!window->setting_text_programmatically) {
                            bool was_modified = window->is_modified;
                            window->is_modified = true;
                            if (!was_modified) {
                                update_title(window);
                            }
                            update_status_bar(window);
                            update_scrollbars(window);
                            ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                        }
                    } else if (notification == EN_SELCHANGE) {
                        update_status_bar(window);
                    }
                } else {
                    handle_command(window, control_id);
                }
            }
            return 0;
        }        case WM_NOTIFY: {
            if (window) {
                NMHDR *nmhdr = (NMHDR *) lparam;
                if (nmhdr->idFrom == ID_EDIT_CONTROL) {
                    switch (nmhdr->code) {
                        case EN_CHANGE: {
                            // Ignore change notifications during programmatic text setting
                            if (!window->setting_text_programmatically) {
                                bool was_modified = window->is_modified;
                                window->is_modified = true;
                                if (!was_modified) {
                                    update_title(window);
                                }
                                update_status_bar(window);
                                update_scrollbars(window);
                                ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                            }
                            break;
                        }
                        case EN_SELCHANGE: {
                            update_status_bar(window);
                            break;
                        }
                    }
                }
            }            break;
        }        case WM_DPICHANGED: {
            if (window) {
                WORD newDpi = HIWORD(wparam);
                
                // Get DPI-aware system metrics
                NONCLIENTMETRICS ncm = { 0 };
                ncm.cbSize = sizeof(ncm);
                
                // Try to get DPI-aware metrics
                if (g_SystemParametersInfoForDpi && !g_SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, newDpi)) {
                    NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "DPI change handling",
                                     "Failed to get DPI-aware metrics, falling back to system defaults");
                }
                
                // Recalculate and apply font size for new DPI
                set_font_size(window, window->font_size);
                
                // Resize controls to match new DPI
                resize_controls(window);
                
                // Update status bar for new DPI
                update_status_bar(window);
                
                // Get suggested window rectangle from system
                RECT *suggested_rect = (RECT*)lparam;
                if (suggested_rect) {
                    SetWindowPos(window->hwnd, NULL,
                                suggested_rect->left, suggested_rect->top,
                                suggested_rect->right - suggested_rect->left,
                                suggested_rect->bottom - suggested_rect->top,
                                SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            if (window && window->is_modified) {
                int result = MessageBox(hwnd, "Do you want to save changes to this document?",
                                        "npad", MB_YESNOCANCEL | MB_ICONQUESTION);

                if (result == IDCANCEL) {
                    return 0; // Don't close
                } else if (result == IDYES) {
                    handle_command(window, ID_FILE_SAVE);
                    if (window->is_modified) {
                        return 0; // Save was cancelled
                    }
                }
            }

            ui_post_event(UI_EVENT_WINDOW_CLOSING, window, NULL);
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            if (window == g_main_window) {
                PostQuitMessage(0);
            }
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void create_menu(Window *window) {
    HMENU hmenu = CreateMenu();
    HMENU hfile = CreatePopupMenu();
    HMENU hedit = CreatePopupMenu();
    HMENU hformat = CreatePopupMenu();
    HMENU hview = CreatePopupMenu();
    HMENU hhelp = CreatePopupMenu();

    if (!hmenu || !hfile || !hedit || !hformat || !hview || !hhelp) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu creation",
                         "Failed to create menu components");
        return;
    }

    // File menu
    AppendMenu(hfile, MF_STRING, ID_FILE_NEW, "&New\tCtrl+N");
    AppendMenu(hfile, MF_STRING, ID_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenu(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hfile, MF_STRING, ID_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenu(hfile, MF_STRING, ID_FILE_SAVE_AS, "Save &As...");
    AppendMenu(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hfile, MF_STRING, ID_FILE_EXIT, "E&xit");

    // Edit menu
    AppendMenu(hedit, MF_STRING, ID_EDIT_UNDO, "&Undo\tCtrl+Z");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, ID_EDIT_CUT, "Cu&t\tCtrl+X");
    AppendMenu(hedit, MF_STRING, ID_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenu(hedit, MF_STRING, ID_EDIT_PASTE, "&Paste\tCtrl+V");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);    AppendMenu(hedit, MF_STRING, ID_EDIT_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, ID_EDIT_FIND, "&Find...\tCtrl+F");
    AppendMenu(hedit, MF_STRING, ID_EDIT_REPLACE, "&Replace...\tCtrl+H");
    AppendMenu(hedit, MF_STRING, ID_EDIT_GOTO_LINE, "&Go to Line...\tCtrl+G");

    // Format menu
    AppendMenu(hformat, MF_STRING, ID_FORMAT_WORD_WRAP, "&Word Wrap\tAlt+Z");
    AppendMenu(hformat, MF_SEPARATOR, 0, NULL);
    AppendMenu(hformat, MF_STRING, ID_FORMAT_FONT, "&Font...");

    // View menu
    AppendMenu(hview, MF_STRING, ID_VIEW_STATUS_BAR, "&Status Bar");
    // AppendMenu(hview, MF_STRING, ID_VIEW_DARK_MODE, "&Dark Mode");  // Commented out - not yet implemented

    // Help menu
    AppendMenu(hhelp, MF_STRING, ID_HELP_ABOUT, "&About npad");    // Add to main menu
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hfile, "&File");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hedit, "&Edit");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hformat, "F&ormat");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hview, "&View");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hhelp, "&Help");

    if (!SetMenu(window->hwnd, hmenu)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu attachment",
                         "Failed to attach menu to window");
        DestroyMenu(hmenu);
        return;
    }

    window->hmenu = hmenu;
}

static void handle_command(Window *window, WORD command) {
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
        case ID_FILE_EXIT:
            ui_post_event(UI_EVENT_QUIT, window, NULL);
            break;
        case ID_EDIT_UNDO:
            ui_post_event(UI_EVENT_EDIT_UNDO, window, NULL);
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
        case ID_EDIT_SELECT_ALL:
            ui_post_event(UI_EVENT_EDIT_SELECT_ALL, window, NULL);
            break;
        case ID_EDIT_FIND:
            ui_post_event(UI_EVENT_EDIT_FIND, window, NULL);
            break;
        case ID_EDIT_REPLACE:
            ui_post_event(UI_EVENT_EDIT_REPLACE, window, NULL);
            break;
        case ID_EDIT_GOTO_LINE: {
            // Show Go to Line dialog
            char line_buffer[32] = "";
            if (InputBox(window->hwnd, "Go to Line", "Line number:", line_buffer,
                         sizeof(line_buffer))) {
                int line_number = atoi(line_buffer);
                if (line_number > 0) {
                    // Convert to zero-based line index
                    int line_index = line_number - 1;

                    // Get the character index for the start of the line
                    int char_index =
                        (int) SendMessage(window->edit_hwnd, EM_LINEINDEX, line_index, 0);
                    if (char_index >= 0) {
                        // Set cursor to the beginning of the line
                        SendMessage(window->edit_hwnd, EM_SETSEL, char_index, char_index);
                        // Scroll to make sure the line is visible
                        SendMessage(window->edit_hwnd, EM_SCROLLCARET, 0, 0);
                        // Update status bar
                        update_status_bar(window);
                    }
                }
            }
            break;
        }        case ID_FORMAT_WORD_WRAP:
            window->word_wrap_enabled = !window->word_wrap_enabled;
            update_scrollbars(window);
            // Update menu checkmark
            if (window->hmenu) {
                CheckMenuItem(window->hmenu, ID_FORMAT_WORD_WRAP,
                              window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
            }
            break;
        case ID_FORMAT_FONT:
            show_font_dialog(window);
            break;
        case ID_VIEW_STATUS_BAR:
            window->status_bar_visible = !window->status_bar_visible;
            ShowWindow(window->status_hwnd, window->status_bar_visible ? SW_SHOW : SW_HIDE);
            resize_controls(window);
            // Update menu checkmark
            if (window->hmenu) {
                CheckMenuItem(window->hmenu, ID_VIEW_STATUS_BAR,
                              window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
            }
            break;
        /*
        case ID_VIEW_DARK_MODE:
            ui_post_event(UI_EVENT_VIEW_TOGGLE_DARK_MODE, window, NULL);
            break;
        */
        case ID_HELP_ABOUT:
            ui_platform_show_about_dialog(window);
            break;
    }
}

static void update_title(Window *window) {
    if (!window || !window->hwnd)
        return;

    char title[512];
    const char *filename = "Untitled";

    if (window->current_file) {
        const char *separator = strrchr(window->current_file, '\\');
        if (!separator) {
            separator = strrchr(window->current_file, '/');
        }
        if (separator && separator != window->current_file) {
            filename = separator + 1;
        } else {
            filename = window->current_file;
        }

        if (!filename || strlen(filename) == 0) {
            filename = "Untitled";
        }
    }

    snprintf(title, sizeof(title), "%s%.400s - npad", window->is_modified ? "*" : "", filename);
    SetWindowText(window->hwnd, title);
}

static void apply_theme(Window *window) {
    if (!window)
        return;

    // Apply proper system colors
    if (g_dark_mode) {
        // For now, just update the menu checkmark - full dark mode would require more work
        // In a complete implementation, we'd set custom colors here
    } else {
        // Use system default colors
        // The edit control will automatically use system colors
    }    // Update menu checkmarks
    if (window->hmenu) {
        // CheckMenuItem(window->hmenu, ID_VIEW_DARK_MODE, g_dark_mode ? MF_CHECKED : MF_UNCHECKED);  // Commented out - not yet implemented
        CheckMenuItem(window->hmenu, ID_FORMAT_WORD_WRAP,
                      window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(window->hmenu, ID_VIEW_STATUS_BAR,
                      window->status_bar_visible ? MF_CHECKED : MF_UNCHECKED);
    }
}

static void update_status_bar(Window *window) {
    if (!window || !window->status_hwnd || !window->edit_hwnd)
        return;

    // Get cursor position
    DWORD start, end;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM) &start, (WPARAM) &end);

    // Calculate line and column
    int line = (int) SendMessage(window->edit_hwnd, EM_LINEFROMCHAR, start, 0) + 1;
    int line_start = (int) SendMessage(window->edit_hwnd, EM_LINEINDEX, line - 1, 0);
    int column = (int) start - line_start + 1;

    char line_col_text[64];
    snprintf(line_col_text, sizeof(line_col_text), "Ln %d, Col %d", line, column);
    SendMessage(window->status_hwnd, SB_SETTEXT, 1, (LPARAM) line_col_text);

    // Update zoom level display
    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "%d%%", window->zoom_level);
    SendMessage(window->status_hwnd, SB_SETTEXT, 2, (LPARAM) zoom_text);

    // Update encoding (for now, always UTF-8)
    SendMessage(window->status_hwnd, SB_SETTEXT, 3, (LPARAM) "UTF-8");

    // Update status message
    const char *line_ending = "Windows (CRLF)";
    char status_text[256];
    if (window->is_modified) {
        snprintf(status_text, sizeof(status_text), "Modified - %s", line_ending);
    } else {
        snprintf(status_text, sizeof(status_text), "%s", line_ending);
    }
    SendMessage(window->status_hwnd, SB_SETTEXT, 0, (LPARAM) status_text);
}

static void update_scrollbars(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    DWORD style = GetWindowLong(window->edit_hwnd, GWL_STYLE);

    if (window->word_wrap_enabled) {
        // Enable word wrap: remove horizontal scroll and autohscroll
        style &= ~(WS_HSCROLL | ES_AUTOHSCROLL);
    } else {
        // Disable word wrap: add horizontal scroll and autohscroll
        style |= (WS_HSCROLL | ES_AUTOHSCROLL);
    }

    // Always keep vertical scroll
    style |= WS_VSCROLL | ES_AUTOVSCROLL;

    SetWindowLong(window->edit_hwnd, GWL_STYLE, style);

    // Force window to redraw with new style
    SetWindowPos(window->edit_hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    SCROLLINFO si;

    // Configure vertical scrollbar to be visible but auto-disabled when not needed
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    GetScrollInfo(window->edit_hwnd, SB_VERT, &si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    SetScrollInfo(window->edit_hwnd, SB_VERT, &si, TRUE);

    // Configure horizontal scrollbar similarly when visible
    if (!window->word_wrap_enabled) {
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        GetScrollInfo(window->edit_hwnd, SB_HORZ, &si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
        SetScrollInfo(window->edit_hwnd, SB_HORZ, &si, TRUE);
    }
}

static void set_font_size(Window *window, int size) {
    if (!window || !window->edit_hwnd || size < 6 || size > 72)
        return;

    window->font_size = size;

    // TODO If there is a setting saved for user chosen font then load it here    // This can maybe be moved into a new function
    NONCLIENTMETRICS ncm;
    HFONT defaultFont;
    ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
    ncm.cbSize = sizeof(NONCLIENTMETRICS);
      // Get current DPI and use DPI-aware system parameters if available
    UINT dpi = get_window_dpi(window->hwnd);
    
    // Try DPI-aware version first (Windows 10+)
    if (g_SystemParametersInfoForDpi && g_SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0, dpi)) {
        // Successfully got DPI-aware metrics
    } else {
        // Fallback to regular system parameters
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);
    }
    
    defaultFont = CreateFontIndirect(&ncm.lfMessageFont);

    if (defaultFont) {
        LOGFONT logFont;
        if (GetObject(defaultFont, sizeof(logFont), &logFont)) {
            // Set up character format for RichEdit using the system default font
            CHARFORMAT2 cf;
            ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_ALL;
            cf.dwEffects = 0;
            cf.yHeight = size * 20; // 20 twips per point TBC
            cf.bCharSet = logFont.lfCharSet;
            strncpy(cf.szFaceName, logFont.lfFaceName, LF_FACESIZE - 1);
            cf.szFaceName[LF_FACESIZE - 1] = '\0';
            cf.wWeight = (WORD) logFont.lfWeight;

            SendMessage(window->edit_hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf);
        }
    }
}

static void show_font_dialog(Window *window) {
    if (!window || !window->edit_hwnd)
        return;

    CHOOSEFONT cf;
    LOGFONT lf;

    CHARFORMAT2 currentFormat;
    ZeroMemory(&currentFormat, sizeof(currentFormat));
    currentFormat.cbSize = sizeof(currentFormat);
    currentFormat.dwMask = CFM_ALL;
    SendMessage(window->edit_hwnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM) &currentFormat);

    // Convert current format to LOGFONT properly
    ZeroMemory(&lf, sizeof(lf));
    strncpy(lf.lfFaceName, currentFormat.szFaceName, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = '\0';
    lf.lfWeight = currentFormat.wWeight;
    lf.lfCharSet = currentFormat.bCharSet;

    // Convert twips back to logical height
    HDC hdc = GetDC(window->edit_hwnd);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    lf.lfHeight = -MulDiv(currentFormat.yHeight, logPixelsY, 1440);
    ReleaseDC(window->edit_hwnd, hdc);

    // Set up font dialog with current color
    ZeroMemory(&cf, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = window->hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    cf.nFontType = SCREEN_FONTTYPE;

    if (ChooseFont(&cf)) {
        // Calculate point size from the selected font
        int pointSize = -MulDiv(lf.lfHeight, 72, logPixelsY);
        ReleaseDC(window->edit_hwnd, hdc);

        // Update the stored font size
        window->font_size = pointSize;

        // Apply the new font with ALL characteristics
        CHARFORMAT2 format;
        ZeroMemory(&format, sizeof(format));
        format.cbSize = sizeof(format);
        format.dwMask = CFM_ALL;
        format.yHeight = pointSize * 20; // Convert to twips
        format.bCharSet = lf.lfCharSet;
        format.wWeight = (WORD) lf.lfWeight;
        format.dwEffects = 0;

        strncpy(format.szFaceName, lf.lfFaceName, LF_FACESIZE - 1);
        format.szFaceName[LF_FACESIZE - 1] = '\0';

        SendMessage(window->edit_hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &format);
    }
}

// Input dialog data structure
typedef struct {
    char *buffer;
    int buffer_size;
    const char *prompt;
} InputBoxData;

// Input box dialog procedure
static INT_PTR CALLBACK InputBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    InputBoxData *data = (InputBoxData *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_INITDIALOG:
            data = (InputBoxData *) lparam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lparam);

            // Set the prompt text
            SetDlgItemText(hwnd, 1000, data->prompt);

            // Set default value "1" for line number
            SetDlgItemText(hwnd, 1001, "1");

            // Focus on the edit control and select all text
            SetFocus(GetDlgItem(hwnd, 1001));
            SendDlgItemMessage(hwnd, 1001, EM_SETSEL, 0, -1);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDOK:
                    if (data && data->buffer) {
                        GetDlgItemText(hwnd, 1001, data->buffer, data->buffer_size);
                    }
                    EndDialog(hwnd, IDOK);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;

                case 1001: // Edit control
                    if (HIWORD(wparam) == EN_CHANGE) {
                        // Enable/disable OK button based on whether there's text
                        char temp[32];
                        GetDlgItemText(hwnd, 1001, temp, sizeof(temp));
                        EnableWindow(GetDlgItem(hwnd, IDOK), strlen(temp) > 0);
                    }
                    break;
            }
            break;

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

// Create an input box dialog
static bool InputBox(HWND parent, const char *title, const char *prompt, char *buffer,
                     int buffer_size) {
    if (!title || !prompt || !buffer || buffer_size <= 0) {
        return false;
    }

    // Create a modal dialog window
    HWND dialog = CreateWindowEx(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "#32770", // Dialog class
        title, DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - 300) / 2, (GetSystemMetrics(SM_CYSCREEN) - 120) / 2, 300,
        120, parent, NULL, g_hinstance, NULL);

    if (!dialog) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Input dialog creation",
                         "Failed to create input dialog");
        return false;
    }

    // Get system dialog font
    HFONT dialog_font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    if (!dialog_font) {
        dialog_font = (HFONT) GetStockObject(SYSTEM_FONT);
    }

    // Create controls with proper spacing and system font
    HWND label = CreateWindow("STATIC", prompt, WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 12, 260, 16,
                              dialog, (HMENU) 1000, g_hinstance, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND edit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 12, 35,
                             190, 21, dialog, (HMENU) 1001, g_hinstance, NULL);
    SendMessage(edit, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND ok_button = CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 210, 35,
                                  70, 23, dialog, (HMENU) IDOK, g_hinstance, NULL);
    SendMessage(ok_button, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND cancel_button = CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      210, 65, 70, 23, dialog, (HMENU) IDCANCEL, g_hinstance, NULL);
    SendMessage(cancel_button, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    // Set up dialog data
    InputBoxData data = { buffer, buffer_size, prompt };
    SetWindowLongPtr(dialog, GWLP_USERDATA, (LONG_PTR) &data);

    // Set dialog proc
    SetWindowLongPtr(dialog, DWLP_DLGPROC, (LONG_PTR) InputBoxProc);

    // Initialize dialog
    SendMessage(dialog, WM_INITDIALOG, 0, (LPARAM) &data);

    // Run modal loop
    MSG msg;
    bool result = false;
    bool done = false;

    if (parent) {
        EnableWindow(parent, FALSE);
    }

    while (!done && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            done = true;
            result = false;
        } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessage(dialog, WM_COMMAND, IDOK, 0);
            done = true;
            result = true;
        } else if (msg.message == WM_COMMAND) {
            if (msg.hwnd == dialog) {
                if (LOWORD(msg.wParam) == IDOK) {
                    GetWindowText(edit, buffer, buffer_size);
                    done = true;
                    result = true;
                } else if (LOWORD(msg.wParam) == IDCANCEL) {
                    done = true;
                    result = false;
                }
            }
        } else if (msg.message == WM_CLOSE && msg.hwnd == dialog) {
            done = true;
            result = false;
        }

        if (!done) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    DestroyWindow(dialog);

    return result;
}

static void resize_controls(Window *window) {
    if (!window || !window->hwnd || !window->edit_hwnd || !window->status_hwnd)
        return;

    RECT rect;
    GetClientRect(window->hwnd, &rect);

    // Resize status bar first - it will auto-position itself at bottom
    SendMessage(window->status_hwnd, WM_SIZE, 0, 0);

    // Get status bar height if it's visible
    int status_height = 0;
    if (window->status_bar_visible) {
        RECT status_rect;
        GetWindowRect(window->status_hwnd, &status_rect);
        status_height = status_rect.bottom - status_rect.top;
    }

    // Position edit control above status bar (or use full height if status bar is hidden)
    SetWindowPos(window->edit_hwnd, NULL, 0, 0, rect.right, rect.bottom - status_height,
                 SWP_NOZORDER);

    // Update scrollbars after resize
    update_scrollbars(window);
}