/*
 * npad - Win32 UI Implementation
 * Windows-specific UI implementation using Win32 API
 * 
 * Author: Platima
 * https://github.com/platima/npad
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <richedit.h>

#include "../ui_interface.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Window class name
#define NPAD_WINDOW_CLASS "NpadMainWindow"

// Control IDs
#define ID_EDIT_CONTROL 1001
#define ID_FILE_NEW     2001
#define ID_FILE_OPEN    2002
#define ID_FILE_SAVE    2003
#define ID_FILE_SAVE_AS 2004
#define ID_FILE_EXIT    2005
#define ID_EDIT_UNDO    2101
#define ID_EDIT_REDO    2102
#define ID_EDIT_CUT     2103
#define ID_EDIT_COPY    2104
#define ID_EDIT_PASTE   2105
#define ID_EDIT_SELECT_ALL 2106
#define ID_EDIT_FIND    2107
#define ID_EDIT_REPLACE 2108
#define ID_VIEW_DARK_MODE 2201
#define ID_HELP_ABOUT   2301

// Window structure
typedef struct Window {
    HWND hwnd;
    HWND edit_hwnd;
    HMENU hmenu;
    bool is_modified;
    char* current_file;
} Window;

// Dialog structure
typedef struct Dialog {
    HWND hwnd;
    Window* parent;
} Dialog;

// Global variables
static HINSTANCE g_hinstance = NULL;
static bool g_dark_mode = false;
static Window* g_main_window = NULL;

// Forward declarations
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static void create_menu(Window* window);
static void handle_command(Window* window, WORD command);
static void update_title(Window* window);
static bool register_window_class(void);
static void apply_theme(Window* window);

// Platform initialization
bool ui_platform_init(void)
{
    g_hinstance = GetModuleHandle(NULL);
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
    
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
                     "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegQueryValueEx(hkey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
        g_dark_mode = (value == 0);
        RegCloseKey(hkey);
    }
    
    return true;
}

void ui_platform_cleanup(void)
{
    if (g_main_window) {
        if (g_main_window->current_file) {
            free(g_main_window->current_file);
        }
        free(g_main_window);
        g_main_window = NULL;
    }
}

int ui_platform_message_loop(void)
{
    MSG msg;
    
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}

void ui_platform_quit(void)
{
    PostQuitMessage(0);
}

Window* ui_platform_create_main_window(void)
{
    Window* window = malloc(sizeof(Window));
    if (!window) return NULL;
    
    memset(window, 0, sizeof(Window));
    
    // Create main window
    window->hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        NPAD_WINDOW_CLASS,
        "npad",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL, g_hinstance, window
    );
    
    if (!window->hwnd) {
        free(window);
        return NULL;
    }
    
    // Create edit control
    window->edit_hwnd = CreateWindowEx(
        0,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        window->hwnd,
        (HMENU)ID_EDIT_CONTROL,
        g_hinstance,
        NULL
    );
    
    if (!window->edit_hwnd) {
        DestroyWindow(window->hwnd);
        free(window);
        return NULL;
    }
    
    // Set font to system fixed font (like original Notepad)
    HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    SendMessage(window->edit_hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    
    // Create menu
    create_menu(window);
    
    // Apply theme
    apply_theme(window);
    
    // Set as main window
    g_main_window = window;
    
    return window;
}

void ui_platform_destroy_window(Window* window)
{
    if (!window) return;
    
    if (window->current_file) {
        free(window->current_file);
    }
    
    if (window->hwnd) {
        DestroyWindow(window->hwnd);
    }
    
    free(window);
    
    if (window == g_main_window) {
        g_main_window = NULL;
    }
}

void ui_platform_show_window(Window* window)
{
    if (window && window->hwnd) {
        ShowWindow(window->hwnd, SW_SHOW);
        UpdateWindow(window->hwnd);
    }
}

void ui_platform_hide_window(Window* window)
{
    if (window && window->hwnd) {
        ShowWindow(window->hwnd, SW_HIDE);
    }
}

void ui_platform_set_window_title(Window* window, const char* title)
{
    if (window && window->hwnd && title) {
        SetWindowText(window->hwnd, title);
    }
}

void ui_platform_get_window_size(Window* window, int* width, int* height)
{
    if (window && window->hwnd && width && height) {
        RECT rect;
        GetClientRect(window->hwnd, &rect);
        *width = rect.right - rect.left;
        *height = rect.bottom - rect.top;
    }
}

void ui_platform_set_window_size(Window* window, int width, int height)
{
    if (window && window->hwnd) {
        SetWindowPos(window->hwnd, NULL, 0, 0, width, height, 
                     SWP_NOMOVE | SWP_NOZORDER);
    }
}

void ui_platform_get_window_position(Window* window, int* x, int* y)
{
    if (window && window->hwnd && x && y) {
        RECT rect;
        GetWindowRect(window->hwnd, &rect);
        *x = rect.left;
        *y = rect.top;
    }
}

void ui_platform_set_window_position(Window* window, int x, int y)
{
    if (window && window->hwnd) {
        SetWindowPos(window->hwnd, NULL, x, y, 0, 0, 
                     SWP_NOSIZE | SWP_NOZORDER);
    }
}

void ui_platform_set_text(Window* window, const char* text)
{
    if (window && window->edit_hwnd && text) {
        SetWindowText(window->edit_hwnd, text);
        window->is_modified = false;
        update_title(window);
    }
}

char* ui_platform_get_text(Window* window)
{
    if (!window || !window->edit_hwnd) return NULL;
    
    int length = GetWindowTextLength(window->edit_hwnd);
    if (length == 0) {
        char* empty = malloc(1);
        empty[0] = '\0';
        return empty;
    }
    
    char* text = malloc(length + 1);
    if (!text) return NULL;
    
    GetWindowText(window->edit_hwnd, text, length + 1);
    return text;
}

void ui_platform_clear_text(Window* window)
{
    if (window && window->edit_hwnd) {
        SetWindowText(window->edit_hwnd, "");
        window->is_modified = false;
        update_title(window);
    }
}

bool ui_platform_has_selection(Window* window)
{
    if (!window || !window->edit_hwnd) return false;
    
    DWORD start, end;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    return start != end;
}

char* ui_platform_get_selected_text(Window* window)
{
    if (!window || !window->edit_hwnd) return NULL;
    
    DWORD start, end;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    
    if (start == end) return NULL;
    
    int length = end - start;
    char* text = malloc(length + 1);
    if (!text) return NULL;
    
    SendMessage(window->edit_hwnd, EM_GETSELTEXT, 0, (LPARAM)text);
    return text;
}

void ui_platform_select_all(Window* window)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_SETSEL, 0, -1);
    }
}

void ui_platform_set_cursor_position(Window* window, int position)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_SETSEL, position, position);
    }
}

int ui_platform_get_cursor_position(Window* window)
{
    if (!window || !window->edit_hwnd) return 0;
    
    DWORD start, end;
    SendMessage(window->edit_hwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    return (int)start;
}

void ui_platform_cut(Window* window)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_CUT, 0, 0);
        window->is_modified = true;
        update_title(window);
    }
}

void ui_platform_copy(Window* window)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_COPY, 0, 0);
    }
}

void ui_platform_paste(Window* window)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, WM_PASTE, 0, 0);
        window->is_modified = true;
        update_title(window);
    }
}

void ui_platform_undo(Window* window)
{
    if (window && window->edit_hwnd) {
        SendMessage(window->edit_hwnd, EM_UNDO, 0, 0);
    }
}

void ui_platform_redo(Window* window)
{
    // Standard EDIT control doesn't support redo
    // Would need RichEdit control for full undo/redo stack
}

bool ui_platform_can_undo(Window* window)
{
    if (!window || !window->edit_hwnd) return false;
    return SendMessage(window->edit_hwnd, EM_CANUNDO, 0, 0) != 0;
}

bool ui_platform_can_redo(Window* window)
{
    // Standard EDIT control doesn't support redo
    return false;
}

char* ui_platform_show_open_dialog(Window* parent, const FileDialogParams* params)
{
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileName(&ofn)) {
        char* result = malloc(strlen(filename) + 1);
        if (result) {
            strcpy(result, filename);
        }
        return result;
    }
    
    return NULL;
}

char* ui_platform_show_save_dialog(Window* parent, const FileDialogParams* params)
{
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
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    
    if (GetSaveFileName(&ofn)) {
        char* result = malloc(strlen(filename) + 1);
        if (result) {
            strcpy(result, filename);
        }
        return result;
    }
    
    return NULL;
}

bool ui_platform_show_message_box(Window* parent, const char* title, const char* message, bool is_question)
{
    HWND hwnd = parent ? parent->hwnd : NULL;
    UINT type = is_question ? (MB_YESNO | MB_ICONQUESTION) : (MB_OK | MB_ICONINFORMATION);
    
    int result = MessageBox(hwnd, message, title, type);
    return is_question ? (result == IDYES) : true;
}

void ui_platform_show_about_dialog(Window* parent)
{
    const char* message = "npad v0.1.0\n\n"
                         "A lightweight, cross-platform text editor\n"
                         "inspired by classic Windows Notepad.\n\n"
                         "Author: Platima\n"
                         "https://github.com/platima/npad";
    
    ui_platform_show_message_box(parent, "About npad", message, false);
}

Dialog* ui_platform_show_find_dialog(Window* parent)
{
    // TODO: Implement find dialog
    return NULL;
}

Dialog* ui_platform_show_replace_dialog(Window* parent)
{
    // TODO: Implement replace dialog
    return NULL;
}

void ui_platform_close_dialog(Dialog* dialog)
{
    if (dialog) {
        if (dialog->hwnd) {
            DestroyWindow(dialog->hwnd);
        }
        free(dialog);
    }
}

void ui_platform_set_dark_mode(bool enabled)
{
    g_dark_mode = enabled;
    if (g_main_window) {
        apply_theme(g_main_window);
    }
}

bool ui_platform_is_dark_mode(void)
{
    return g_dark_mode;
}

bool ui_platform_system_supports_dark_mode(void)
{
    // Windows 10 version 1809 and later support dark mode
    return true;
}

bool ui_platform_is_text_modified(Window* window)
{
    return window ? window->is_modified : false;
}

void ui_platform_set_text_modified(Window* window, bool modified)
{
    if (window) {
        window->is_modified = modified;
        update_title(window);
    }
}

int ui_platform_get_line_count(Window* window)
{
    if (!window || !window->edit_hwnd) return 0;
    return (int)SendMessage(window->edit_hwnd, EM_GETLINECOUNT, 0, 0);
}

void ui_platform_get_cursor_line_column(Window* window, int* line, int* column)
{
    if (!window || !window->edit_hwnd || !line || !column) return;
    
    int pos = ui_platform_get_cursor_position(window);
    *line = (int)SendMessage(window->edit_hwnd, EM_LINEFROMCHAR, pos, 0) + 1;
    int line_start = (int)SendMessage(window->edit_hwnd, EM_LINEINDEX, *line - 1, 0);
    *column = pos - line_start + 1;
}

void* ui_platform_get_native_handle(Window* window)
{
    return window ? window->hwnd : NULL;
}

// Helper functions

static bool register_window_class(void)
{
    WNDCLASSEX wc;
    
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NPAD_WINDOW_CLASS;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    return RegisterClassEx(&wc) != 0;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Window* window = (Window*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (msg) {
        case WM_CREATE:
        {
            CREATESTRUCT* cs = (CREATESTRUCT*)lparam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }
        
        case WM_SIZE:
        {
            if (window && window->edit_hwnd) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                SetWindowPos(window->edit_hwnd, NULL, 0, 0, 
                           rect.right, rect.bottom, SWP_NOZORDER);
            }
            return 0;
        }
        
        case WM_COMMAND:
        {
            if (window) {
                if (HIWORD(wparam) == EN_CHANGE && LOWORD(wparam) == ID_EDIT_CONTROL) {
                    window->is_modified = true;
                    update_title(window);
                    ui_post_event(UI_EVENT_TEXT_CHANGED, window, NULL);
                } else {
                    handle_command(window, LOWORD(wparam));
                }
            }
            return 0;
        }
        
        case WM_CLOSE:
        {
            if (window && window->is_modified) {
                int result = MessageBox(hwnd, 
                    "Do you want to save changes to this document?", 
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
        
        case WM_DESTROY:
        {
            if (window == g_main_window) {
                PostQuitMessage(0);
            }
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void create_menu(Window* window)
{
    HMENU hmenu = CreateMenu();
    HMENU hfile = CreatePopupMenu();
    HMENU hedit = CreatePopupMenu();
    HMENU hview = CreatePopupMenu();
    HMENU hhelp = CreatePopupMenu();
    
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
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, ID_EDIT_SELECT_ALL, "Select &All\tCtrl+A");
    AppendMenu(hedit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hedit, MF_STRING, ID_EDIT_FIND, "&Find...\tCtrl+F");
    AppendMenu(hedit, MF_STRING, ID_EDIT_REPLACE, "&Replace...\tCtrl+H");
    
    // View menu
    AppendMenu(hview, MF_STRING, ID_VIEW_DARK_MODE, "&Dark Mode");
    
    // Help menu
    AppendMenu(hhelp, MF_STRING, ID_HELP_ABOUT, "&About npad");
    
    // Add to main menu
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR)hfile, "&File");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR)hedit, "&Edit");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR)hview, "&View");
    AppendMenu(hmenu, MF_STRING | MF_POPUP, (UINT_PTR)hhelp, "&Help");
    
    SetMenu(window->hwnd, hmenu);
    window->hmenu = hmenu;
}

static void handle_command(Window* window, WORD command)
{
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
        case ID_VIEW_DARK_MODE:
            ui_post_event(UI_EVENT_VIEW_TOGGLE_DARK_MODE, window, NULL);
            break;
        case ID_HELP_ABOUT:
            ui_platform_show_about_dialog(window);
            break;
    }
}

static void update_title(Window* window)
{
    if (!window) return;
    
    char title[512];
    const char* filename = window->current_file ? 
        strrchr(window->current_file, '\\') + 1 : "Untitled";
    
    if (!filename || filename == (char*)1) {
        filename = window->current_file ? window->current_file : "Untitled";
    }
    
    snprintf(title, sizeof(title), "%s%s - npad", 
             window->is_modified ? "*" : "", filename);
    
    SetWindowText(window->hwnd, title);
}

static void apply_theme(Window* window)
{
    if (!window) return;
    
    // Basic theme support - in a real implementation, this would be more sophisticated
    if (g_dark_mode) {
        // Set dark background/foreground colors
        // This is a simplified implementation
    } else {
        // Set light background/foreground colors
    }
    
    // Update menu checkmark
    if (window->hmenu) {
        CheckMenuItem(window->hmenu, ID_VIEW_DARK_MODE, 
                     g_dark_mode ? MF_CHECKED : MF_UNCHECKED);
    }
}
    