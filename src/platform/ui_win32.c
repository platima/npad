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

#include "../ui_interface.h"
#include "../main.h"
#include "../core/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define ID_VIEW_DARK_MODE 2201
#define ID_VIEW_WORD_WRAP 2202
#define ID_HELP_ABOUT 2301

// Window structure
typedef struct Window {
    HWND hwnd;
    HWND edit_hwnd;
    HWND status_hwnd;
    HMENU hmenu;
    HACCEL haccel;
    bool is_modified;
    char *current_file;
    bool word_wrap_enabled;
    int zoom_level;
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

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window *window);
static void handle_command(Window *window, WORD command);
static void update_title(Window *window);
static void update_status_bar(Window *window);
static bool register_window_class(void);
static void apply_theme(Window *window);
static bool InputBox(HWND parent, const char *title, const char *prompt, char *buffer,
                     int buffer_size);

// Platform initialization
bool ui_platform_init(void) {
    g_hinstance = GetModuleHandle(NULL);

    // DPI awareness is now handled early in main.c before any UI initialization

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&icex)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                        "Failed to initialize common controls");
        return false;
    }

    // Load Rich Edit library
    HMODULE richedit_lib = LoadLibrary(TEXT("riched20.dll"));
    if (!richedit_lib) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                          "Failed to load Rich Edit library - falling back to standard edit control");
    }

    // Register window class
    if (!register_window_class()) {
        return false;
    }

    // Check for system dark mode support
    // This is a simplified check - real implementation would be more robust
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

    // Create main window - FIXED: Separated EX flags from regular flags
    window->hwnd =
        CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_ACCEPTFILES | WS_EX_WINDOWEDGE, 
                       NPAD_WINDOW_CLASS, "npad",
                       WS_OVERLAPPEDWINDOW, 
                       CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, 
                       NULL, NULL, g_hinstance, window);

    if (!window->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Window creation",
                        "Failed to create main window");
        free(window);
        return NULL;
    }

    // Create rich edit control (but keep it in plain text mode)
    window->edit_hwnd =
        CreateWindowEx(0, RICHEDIT_CLASS, "",
                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                           ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                       0, 0, 0, 0, window->hwnd, (HMENU) ID_EDIT_CONTROL, g_hinstance, NULL);

    if (!window->edit_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Edit control creation",
                        "Failed to create edit control");
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    // Set font to system default GUI font (TrueType)
    HFONT font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    if (!font) {
        // Fallback to system font
        font = (HFONT) GetStockObject(SYSTEM_FONT);
    }
    SendMessage(window->edit_hwnd, WM_SETFONT, (WPARAM) font, TRUE);

    // Configure RichEdit for plain text behavior
    SendMessage(window->edit_hwnd, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessage(window->edit_hwnd, EM_SETOPTIONS, ECOOP_OR, ECO_NOHIDESEL);

    // Set unlimited text length
    SendMessage(window->edit_hwnd, EM_LIMITTEXT, 0, 0);

    // Create status bar
    window->status_hwnd =
        CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                       window->hwnd, (HMENU) ID_STATUS_BAR, g_hinstance, NULL);

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

    // Initialize status bar text
    SendMessage(window->status_hwnd, SB_SETTEXT, 0, (LPARAM) "Ready");
    SendMessage(window->status_hwnd, SB_SETTEXT, 1, (LPARAM) "Ln 1, Col 1");
    SendMessage(window->status_hwnd, SB_SETTEXT, 2, (LPARAM) "100%");
    SendMessage(window->status_hwnd, SB_SETTEXT, 3, (LPARAM) "UTF-8");

    // Force status bar to be visible and properly sized
    ShowWindow(window->status_hwnd, SW_SHOW);
    UpdateWindow(window->status_hwnd);

    // Initialize window state
    window->word_wrap_enabled = false;
    window->zoom_level = 100;

    // Create menu and accelerators
    create_menu(window);

    // Create accelerator table - FIXED: Added proper error handling
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
                      { FALT | FVIRTKEY, 'Z', ID_VIEW_WORD_WRAP } };
    window->haccel = CreateAcceleratorTable(accel, sizeof(accel) / sizeof(accel[0]));

    // FIXED: Better error handling for accelerator table
    if (!window->haccel) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "Accelerator table creation",
                          "Failed to create accelerator table - keyboard shortcuts will not work");
        // Continue without accelerators rather than failing completely
    }

    // Apply theme
    apply_theme(window);

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
        SetWindowTextA(window->hwnd, title);
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
        SetWindowTextA(window->edit_hwnd, text);
        window->is_modified = false;
        update_title(window);
    }
}

char *ui_platform_get_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    int length = GetWindowTextLengthA(window->edit_hwnd);
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

    int actual_length = GetWindowTextA(window->edit_hwnd, text, length + 1);
    if (actual_length != length) {
        // Unexpected length mismatch - handle gracefully
        text[actual_length] = '\0';
    }
    return text;
}

void ui_platform_clear_text(Window *window) {
    if (window && window->edit_hwnd) {
        SetWindowTextA(window->edit_hwnd, "");
        window->is_modified = false;
        update_title(window);
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
    }
}

void ui_platform_undo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_UNDO, 0, 0);
    }
}

void ui_platform_redo(Window *window) {
    (void) window;
    // Standard EDIT control doesn't support redo
    // Would need RichEdit control for full undo/redo stack
}

bool ui_platform_can_undo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessage(window->edit_hwnd, EM_CANUNDO, 0, 0) != 0;
}

bool ui_platform_can_redo(Window *window) {
    (void) window;
    // Standard EDIT control doesn't support redo
    return false;
}

char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) params; // Use default parameters for now
    OPENFILENAMEA ofn;
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        char *result = malloc(strlen(filename) + 1);
        if (result) {
            strcpy(result, filename);
        }
        return result;
    }

    return NULL;
}

// Dialog hook procedure for save dialog to add encoding dropdown
static UINT_PTR CALLBACK SaveDialogHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
    (void) wParam; // Unused parameter
    (void) lParam; // Unused parameter
    static HWND hComboEncoding = NULL;

    switch (uiMsg) {
        case WM_INITDIALOG: {
            // Get the parent dialog dimensions
            RECT dlgRect;
            GetClientRect(hdlg, &dlgRect);

            // Create encoding label and combobox below the file controls
            HWND hLabelEncoding =
                CreateWindow("STATIC", "Encoding:", WS_CHILD | WS_VISIBLE | SS_LEFT, 12,
                             dlgRect.bottom - 60, 60, 16, hdlg, (HMENU) 2001, g_hinstance, NULL);
            (void) hLabelEncoding; // Used for display only

            hComboEncoding = CreateWindow(
                "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 80,
                dlgRect.bottom - 62, 120, 100, hdlg, (HMENU) 2002, g_hinstance, NULL);

            if (hComboEncoding) {
                // Add encoding options
                SendMessage(hComboEncoding, CB_ADDSTRING, 0, (LPARAM) "UTF-8");
                SendMessage(hComboEncoding, CB_ADDSTRING, 0, (LPARAM) "UTF-8 with BOM");
                SendMessage(hComboEncoding, CB_ADDSTRING, 0, (LPARAM) "UTF-16 LE");
                SendMessage(hComboEncoding, CB_ADDSTRING, 0, (LPARAM) "UTF-16 BE");
                SendMessage(hComboEncoding, CB_ADDSTRING, 0, (LPARAM) "ANSI");

                // Default to UTF-8
                SendMessage(hComboEncoding, CB_SETCURSEL, 0, 0);
            }
            break;
        }

        case WM_SIZE: {
            // Reposition controls when dialog is resized
            if (hComboEncoding) {
                RECT dlgRect;
                GetClientRect(hdlg, &dlgRect);
                SetWindowPos(GetDlgItem(hdlg, 2001), NULL, 12, dlgRect.bottom - 60, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
                SetWindowPos(hComboEncoding, NULL, 80, dlgRect.bottom - 62, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            }
            break;
        }
    }
    return 0;
}

char *ui_platform_show_save_dialog(Window *parent, const FileDialogParams *params) {
    (void) params; // Use default parameters for now
    OPENFILENAMEA ofn;
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLEHOOK | OFN_EXPLORER;
    ofn.lpfnHook = SaveDialogHookProc;

    if (GetSaveFileNameA(&ofn)) {
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

    int result = MessageBoxA(hwnd, message, title, type);
    return is_question ? (result == IDYES) : true;
}

void ui_platform_show_about_dialog(Window *parent) {
    const char *message = "npad " NPAD_VERSION "\n\n"
                          "A lightweight, cross-platform text editor\n"
                          "inspired by classic Windows Notepad.\n\n"
                          "Author: Platima\n"
                          "https://github.com/platima/npad";

    ui_platform_show_message_box(parent, "About npad", message, false);
}

Dialog *ui_platform_show_find_dialog(Window *parent) {
    Dialog *dialog = malloc(sizeof(Dialog));
    if (!dialog)
        return NULL;

    dialog->parent = parent;

    // Create a simple modal dialog for find
    dialog->hwnd =
        CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                       "STATIC", // Using STATIC class as placeholder
                       "Find", WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                       300, 100, parent ? parent->hwnd : NULL, NULL, g_hinstance, NULL);

    if (!dialog->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Find dialog creation",
                        "Failed to create find dialog");
        free(dialog);
        return NULL;
    }

    // Show the dialog
    ShowWindow(dialog->hwnd, SW_SHOW);

    return dialog;
}

Dialog *ui_platform_show_replace_dialog(Window *parent) {
    (void) parent;
    // TODO: Implement replace dialog
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
    // Windows 10 version 1809 and later support dark mode
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
        if (line) *line = 0;
        if (column) *column = 0;
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
    WNDCLASSEXA wc;

    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NPAD_WINDOW_CLASS;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    ATOM result = RegisterClassExA(&wc);
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
        }

        case WM_SIZE: {
            if (window && window->edit_hwnd && window->status_hwnd) {
                RECT rect;
                GetClientRect(hwnd, &rect);

                // Resize status bar first - it will auto-position itself at bottom
                SendMessage(window->status_hwnd, WM_SIZE, 0, 0);

                // Get status bar height
                RECT status_rect;
                GetWindowRect(window->status_hwnd, &status_rect);
                int status_height = status_rect.bottom - status_rect.top;

                // Position edit control above status bar
                SetWindowPos(window->edit_hwnd, NULL, 0, 0, rect.right, rect.bottom - status_height,
                             SWP_NOZORDER);
            }
            return 0;
        }

        case WM_COMMAND: {
            if (window) {
                if (HIWORD(wparam) == EN_CHANGE && LOWORD(wparam) == ID_EDIT_CONTROL) {
                    // Use atomic operation to prevent race condition
                    bool was_modified = window->is_modified;
                    window->is_modified = true;
                    if (!was_modified) {
                        update_title(window);
                    }
                    update_status_bar(window);
                    ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                } else if (HIWORD(wparam) == EN_SELCHANGE && LOWORD(wparam) == ID_EDIT_CONTROL) {
                    // Update status bar when cursor position changes
                    update_status_bar(window);
                } else {
                    handle_command(window, LOWORD(wparam));
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            if (window && window->is_modified) {
                int result = MessageBoxA(hwnd, "Do you want to save changes to this document?",
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
    HMENU hview = CreatePopupMenu();
    HMENU hhelp = CreatePopupMenu();

    if (!hmenu || !hfile || !hedit || !hview || !hhelp) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Menu creation",
                        "Failed to create menu components");
        return;
    }

    // File menu
    AppendMenuA(hfile, MF_STRING, ID_FILE_NEW, "&New\tCtrl+N");
    AppendMenuA(hfile, MF_STRING, ID_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenuA(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hfile, MF_STRING, ID_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenuA(hfile, MF_STRING, ID_FILE_SAVE_AS, "Save &As...");
    AppendMenuA(hfile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hfile, MF_STRING, ID_FILE_EXIT, "E&xit");

    // Edit menu
    AppendMenuA(hedit, MF_STRING, ID_EDIT_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hedit, MF_STRING, ID_EDIT_CUT, "Cu&t\tCtrl+X");
    AppendMenuA(hedit, MF_STRING, ID_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenuA(hedit, MF_STRING, ID_EDIT_PASTE, "&Paste\tCtrl+V");
    AppendMenuA(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hedit, MF_STRING, ID_EDIT_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenuA(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hedit, MF_STRING, ID_EDIT_FIND, "&Find...\tCtrl+F");
    AppendMenuA(hedit, MF_STRING, ID_EDIT_REPLACE, "&Replace...\tCtrl+H");
    AppendMenuA(hedit, MF_STRING, ID_EDIT_GOTO_LINE, "&Go to Line...\tCtrl+G");

    // View menu
    AppendMenuA(hview, MF_STRING, ID_VIEW_WORD_WRAP, "&Word Wrap\tAlt+Z");
    AppendMenuA(hview, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hview, MF_STRING, ID_VIEW_DARK_MODE, "&Dark Mode");

    // Help menu
    AppendMenuA(hhelp, MF_STRING, ID_HELP_ABOUT, "&About npad");

    // Add to main menu
    AppendMenuA(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hfile, "&File");
    AppendMenuA(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hedit, "&Edit");
    AppendMenuA(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hview, "&View");
    AppendMenuA(hmenu, MF_STRING | MF_POPUP, (UINT_PTR) hhelp, "&Help");

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
                    int char_index = (int)SendMessage(window->edit_hwnd, EM_LINEINDEX, line_index, 0);
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
        }
        case ID_VIEW_WORD_WRAP:
            // Toggle word wrap
            window->word_wrap_enabled = !window->word_wrap_enabled;
            if (window->word_wrap_enabled) {
                // Enable word wrap
                SendMessage(window->edit_hwnd, EM_SETTARGETDEVICE, 0, 0);
            } else {
                // Disable word wrap
                SendMessage(window->edit_hwnd, EM_SETTARGETDEVICE, 0, 1);
            }
            // Update menu checkmark
            if (window->hmenu) {
                CheckMenuItem(window->hmenu, ID_VIEW_WORD_WRAP,
                              window->word_wrap_enabled ? MF_CHECKED : MF_UNCHECKED);
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

        // Validate filename is not empty
        if (!filename || strlen(filename) == 0) {
            filename = "Untitled";
        }
    }

    snprintf(title, sizeof(title), "%s%.400s - npad", window->is_modified ? "*" : "", filename);

    SetWindowTextA(window->hwnd, title);
}

static void apply_theme(Window *window) {
    if (!window)
        return;

    // Basic theme support - in a real implementation, this would be more sophisticated
    if (g_dark_mode) {
        // Set dark background/foreground colors
        // This is a simplified implementation
    } else {
        // Set light background/foreground colors
    }

    // Update menu checkmark
    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_VIEW_DARK_MODE, g_dark_mode ? MF_CHECKED : MF_UNCHECKED);
    }
}

static void update_status_bar(Window *window) {
    if (!window || !window->status_hwnd || !window->edit_hwnd)
        return;

    // Get cursor position
    DWORD start, end;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM) &start, (WPARAM) &end);

    // Calculate line and column
    int line = (int)SendMessage(window->edit_hwnd, EM_LINEFROMCHAR, start, 0) + 1;
    int line_start = (int)SendMessage(window->edit_hwnd, EM_LINEINDEX, line - 1, 0);
    int column = (int)start - line_start + 1;

    // Update line/column display
    char line_col_text[64];
    snprintf(line_col_text, sizeof(line_col_text), "Ln %d, Col %d", line, column);
    SendMessage(window->status_hwnd, SB_SETTEXT, 1, (LPARAM) line_col_text);

    // Update zoom level
    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "%d%%", window->zoom_level);
    SendMessage(window->status_hwnd, SB_SETTEXT, 2, (LPARAM) zoom_text);

    // Update encoding (for now, always UTF-8)
    SendMessage(window->status_hwnd, SB_SETTEXT, 3, (LPARAM) "UTF-8");

    // Update line endings info in the first (leftmost) part
    const char *line_ending = "Windows (CRLF)"; // Default for Windows
    if (window->current_file) {
        // In a real implementation, we'd detect the actual line endings
        // For now, assume Windows line endings
    }

    char status_text[256];
    if (window->is_modified) {
        snprintf(status_text, sizeof(status_text), "Modified - %s", line_ending);
    } else {
        snprintf(status_text, sizeof(status_text), "%s", line_ending);
    }
    SendMessage(window->status_hwnd, SB_SETTEXT, 0, (LPARAM) status_text);
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
            SetDlgItemTextA(hwnd, 1000, data->prompt);

            // Set default value "1" for line number
            SetDlgItemTextA(hwnd, 1001, "1");

            // Focus on the edit control and select all text
            SetFocus(GetDlgItem(hwnd, 1001));
            SendDlgItemMessage(hwnd, 1001, EM_SETSEL, 0, -1);
            return FALSE;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDOK:
                    if (data && data->buffer) {
                        GetDlgItemTextA(hwnd, 1001, data->buffer, data->buffer_size);
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
                        GetDlgItemTextA(hwnd, 1001, temp, sizeof(temp));
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
    HWND dialog = CreateWindowExA(
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
    HWND label = CreateWindowA("STATIC", prompt, WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 12, 260, 16,
                              dialog, (HMENU) 1000, g_hinstance, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 12, 35,
                             190, 21, dialog, (HMENU) 1001, g_hinstance, NULL);
    SendMessage(edit, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND ok_button = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 210, 35,
                                  70, 23, dialog, (HMENU) IDOK, g_hinstance, NULL);
    SendMessage(ok_button, WM_SETFONT, (WPARAM) dialog_font, TRUE);

    HWND cancel_button = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
                    GetWindowTextA(edit, buffer, buffer_size);
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
    char *ui_platform_show_open_dialog(Window *parent, const FileDialogParams *params) {
    (void) params; // Use default parameters for now
    OPENFILENAMEA ofn;
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_/*
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

#include "../ui_interface.h"
#include "../main.h"
#include "../core/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define ID_VIEW_DARK_MODE 2201
#define ID_VIEW_WORD_WRAP 2202
#define ID_HELP_ABOUT 2301

// Window structure
typedef struct Window {
    HWND hwnd;
    HWND edit_hwnd;
    HWND status_hwnd;
    HMENU hmenu;
    HACCEL haccel;
    bool is_modified;
    char *current_file;
    bool word_wrap_enabled;
    int zoom_level;
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

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window *window);
static void handle_command(Window *window, WORD command);
static void update_title(Window *window);
static void update_status_bar(Window *window);
static bool register_window_class(void);
static void apply_theme(Window *window);
static bool InputBox(HWND parent, const char *title, const char *prompt, char *buffer,
                     int buffer_size);

// Platform initialization
bool ui_platform_init(void) {
    g_hinstance = GetModuleHandle(NULL);

    // DPI awareness is now handled early in main.c before any UI initialization

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&icex)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                        "Failed to initialize common controls");
        return false;
    }

    // Load Rich Edit library
    HMODULE richedit_lib = LoadLibrary(TEXT("riched20.dll"));
    if (!richedit_lib) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "UI initialization",
                          "Failed to load Rich Edit library - falling back to standard edit control");
    }

    // Register window class
    if (!register_window_class()) {
        return false;
    }

    // Check for system dark mode support
    // This is a simplified check - real implementation would be more robust
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

    // Create main window - FIXED: Separated EX flags from regular flags
    window->hwnd =
        CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_ACCEPTFILES | WS_EX_WINDOWEDGE, 
                       NPAD_WINDOW_CLASS, "npad",
                       WS_OVERLAPPEDWINDOW, 
                       CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, 
                       NULL, NULL, g_hinstance, window);

    if (!window->hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Window creation",
                        "Failed to create main window");
        free(window);
        return NULL;
    }

    // Create rich edit control (but keep it in plain text mode)
    window->edit_hwnd =
        CreateWindowEx(0, RICHEDIT_CLASS, "",
                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                           ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                       0, 0, 0, 0, window->hwnd, (HMENU) ID_EDIT_CONTROL, g_hinstance, NULL);

    if (!window->edit_hwnd) {
        NPAD_ERROR_ERROR(NPAD_ERROR_UI, GetLastError(), "Edit control creation",
                        "Failed to create edit control");
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }

    // Set font to system default GUI font (TrueType)
    HFONT font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    if (!font) {
        // Fallback to system font
        font = (HFONT) GetStockObject(SYSTEM_FONT);
    }
    SendMessage(window->edit_hwnd, WM_SETFONT, (WPARAM) font, TRUE);

    // Configure RichEdit for plain text behavior
    SendMessage(window->edit_hwnd, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessage(window->edit_hwnd, EM_SETOPTIONS, ECOOP_OR, ECO_NOHIDESEL);

    // Set unlimited text length
    SendMessage(window->edit_hwnd, EM_LIMITTEXT, 0, 0);

    // Create status bar
    window->status_hwnd =
        CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                       window->hwnd, (HMENU) ID_STATUS_BAR, g_hinstance, NULL);

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

    // Initialize status bar text
    SendMessage(window->status_hwnd, SB_SETTEXT, 0, (LPARAM) "Ready");
    SendMessage(window->status_hwnd, SB_SETTEXT, 1, (LPARAM) "Ln 1, Col 1");
    SendMessage(window->status_hwnd, SB_SETTEXT, 2, (LPARAM) "100%");
    SendMessage(window->status_hwnd, SB_SETTEXT, 3, (LPARAM) "UTF-8");

    // Force status bar to be visible and properly sized
    ShowWindow(window->status_hwnd, SW_SHOW);
    UpdateWindow(window->status_hwnd);

    // Initialize window state
    window->word_wrap_enabled = false;
    window->zoom_level = 100;

    // Create menu and accelerators
    create_menu(window);

    // Create accelerator table - FIXED: Added proper error handling
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
                      { FALT | FVIRTKEY, 'Z', ID_VIEW_WORD_WRAP } };
    window->haccel = CreateAcceleratorTable(accel, sizeof(accel) / sizeof(accel[0]));

    // FIXED: Better error handling for accelerator table
    if (!window->haccel) {
        NPAD_ERROR_WARNING(NPAD_ERROR_SYSTEM, GetLastError(), "Accelerator table creation",
                          "Failed to create accelerator table - keyboard shortcuts will not work");
        // Continue without accelerators rather than failing completely
    }

    // Apply theme
    apply_theme(window);

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
        SetWindowTextA(window->hwnd, title);
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
        SetWindowTextA(window->edit_hwnd, text);
        window->is_modified = false;
        update_title(window);
    }
}

char *ui_platform_get_text(Window *window) {
    if (!window || !window->edit_hwnd)
        return NULL;

    int length = GetWindowTextLengthA(window->edit_hwnd);
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

    int actual_length = GetWindowTextA(window->edit_hwnd, text, length + 1);
    if (actual_length != length) {
        // Unexpected length mismatch - handle gracefully
        text[actual_length] = '\0';
    }
    return text;
}

void ui_platform_clear_text(Window *window) {
    if (window && window->edit_hwnd) {
        SetWindowTextA(window->edit_hwnd, "");
        window->is_modified = false;
        update_title(window);
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
    }
}

void ui_platform_undo(Window *window) {
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_UNDO, 0, 0);
    }
}

void ui_platform_redo(Window *window) {
    (void) window;
    // Standard EDIT control doesn't support redo
    // Would need RichEdit control for full undo/redo stack
}

bool ui_platform_can_undo(Window *window) {
    if (!window || !window->edit_hwnd)
        return false;
    return SendMessage(window->edit_hwnd, EM_CANUNDO, 0, 0) != 0;
}

bool ui_platform_can_redo(Window *window) {
    (void) window;
    // Standard EDIT control doesn't support redo
    return false;
}