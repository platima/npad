/*
 * npad - File type associations (Win32)
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "assoc_win32.h"
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLASSES L"Software\\Classes"
#define CAPS L"Software\\Platima\\npad\\Capabilities"
#define CAPS_ASSOC L"Software\\Platima\\npad\\Capabilities\\FileAssociations"

typedef struct {
    const wchar_t *ext;      // ".txt"
    const wchar_t *progid;   // "npad.txt"
    const wchar_t *friendly; // "Text File"
} AssocType;

typedef struct {
    const char *token; // What the elevated command line names it
    const AssocType *types;
    int count;
} AssocGroupDef;

// Verbatim from installer/npad.iss: the same 14 ProgIDs, the same friendly
// names, grouped the same five ways. A mismatch here would leave the pane and
// the installer registering subtly different things for the same tick box.
static const AssocType TYPES_TEXT[] = {
    { L".txt", L"npad.txt", L"Text File" },
};
static const AssocType TYPES_MARKDOWN[] = {
    { L".md", L"npad.md", L"Markdown Document" },
    { L".markdown", L"npad.markdown", L"Markdown Document" },
};
static const AssocType TYPES_DATA[] = {
    { L".csv", L"npad.csv", L"CSV File" },    { L".tsv", L"npad.tsv", L"TSV File" },
    { L".json", L"npad.json", L"JSON File" }, { L".xml", L"npad.xml", L"XML File" },
    { L".yaml", L"npad.yaml", L"YAML File" }, { L".yml", L"npad.yml", L"YAML File" },
    { L".toml", L"npad.toml", L"TOML File" },
};
static const AssocType TYPES_CONFIG[] = {
    { L".ini", L"npad.ini", L"INI Configuration File" },
    { L".cfg", L"npad.cfg", L"Config File" },
    { L".conf", L"npad.conf", L"Config File" },
};
static const AssocType TYPES_LOG[] = {
    { L".log", L"npad.log", L"Log File" },
};

#define COUNT_OF(a) ((int) (sizeof(a) / sizeof((a)[0])))

static const AssocGroupDef GROUPS[ASSOC_GROUP_COUNT] = {
    { "text", TYPES_TEXT, COUNT_OF(TYPES_TEXT) },
    { "markdown", TYPES_MARKDOWN, COUNT_OF(TYPES_MARKDOWN) },
    { "data", TYPES_DATA, COUNT_OF(TYPES_DATA) },
    { "config", TYPES_CONFIG, COUNT_OF(TYPES_CONFIG) },
    { "log", TYPES_LOG, COUNT_OF(TYPES_LOG) },
};

// ---------------------------------------------------------------------------
// Registry helpers
//
// Registry APIs return their error directly and do NOT set GetLastError, so
// these check the returned LSTATUS. HKLM is always opened with
// KEY_WOW64_64KEY, so a future 32-bit build cannot land in Wow6432Node.
// ---------------------------------------------------------------------------

static REGSAM sam_for(HKEY root) {
    return (root == HKEY_LOCAL_MACHINE) ? KEY_WOW64_64KEY : 0;
}

static bool reg_set_sz(HKEY root, const wchar_t *subkey, const wchar_t *name, const wchar_t *data) {
    HKEY key = NULL;
    if (RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_SET_VALUE | sam_for(root), NULL, &key,
                        NULL) != ERROR_SUCCESS) {
        return false;
    }
    // cbData must include the terminating NUL
    DWORD bytes = (DWORD) ((wcslen(data) + 1) * sizeof(wchar_t));
    LSTATUS rc = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *) data, bytes);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

static bool reg_get_sz(HKEY root, const wchar_t *subkey, const wchar_t *name, wchar_t *out,
                       DWORD cch) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | sam_for(root), &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD bytes = cch * (DWORD) sizeof(wchar_t);
    LSTATUS rc = RegQueryValueExW(key, name, NULL, &type, (BYTE *) out, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return false;
    out[cch - 1] = L'\0';
    return true;
}

static bool reg_value_exists(HKEY root, const wchar_t *subkey, const wchar_t *name) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | sam_for(root), &key) != ERROR_SUCCESS)
        return false;
    LSTATUS rc = RegQueryValueExW(key, name, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

static void reg_delete_value(HKEY root, const wchar_t *subkey, const wchar_t *name) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE | sam_for(root), &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, name);
    RegCloseKey(key);
}

// RegDeleteTree(parent, sub) removes sub and everything under it. Only ever
// called on npad's own ProgID keys, which are namespaced by name - nothing
// else can own "npad.txt", so there is no key here that belongs to anyone else.
static void reg_delete_tree(HKEY root, const wchar_t *parent, const wchar_t *sub) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, parent, 0,
                      DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | sam_for(root),
                      &key) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteTreeW(key, sub);
    RegCloseKey(key);
}

static const wchar_t *exe_path(void) {
    static wchar_t path[MAX_PATH];
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            // Truncated: a partial path must never reach the registry
            path[0] = L'\0';
        } else {
            // A short 8.3 path stored here renders wrong in Explorer's UI.
            // GetLongPathNameW returns the REQUIRED size, without writing the
            // buffer, when the long form does not fit - so check the bound.
            wchar_t longer[MAX_PATH];
            DWORD m = GetLongPathNameW(path, longer, MAX_PATH);
            if (m > 0 && m < MAX_PATH)
                wcscpy(path, longer);
        }
    }
    return path;
}

// ---------------------------------------------------------------------------
// One file type
// ---------------------------------------------------------------------------

static bool reg_key_exists(HKEY root, const wchar_t *subkey) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | sam_for(root), &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

// Registered means BOTH halves exist: the extension lists npad's ProgID, and
// that ProgID can actually open something. Uninstall removes the ProgID but
// deliberately leaves the OpenWithProgids entry (Windows ignores an entry
// naming an unregistered ProgID), so the entry alone would make a group the
// user removed read as registered again after a reinstall.
static bool type_is_registered(HKEY root, const AssocType *t) {
    wchar_t sub[256];
    _snwprintf(sub, 255, L"%s\\%s\\OpenWithProgids", CLASSES, t->ext);
    sub[255] = L'\0';
    if (!reg_value_exists(root, sub, t->progid))
        return false;
    _snwprintf(sub, 255, L"%s\\%s\\shell\\open\\command", CLASSES, t->progid);
    sub[255] = L'\0';
    return reg_key_exists(root, sub);
}

static bool type_register(HKEY root, const AssocType *t) {
    const wchar_t *exe = exe_path();
    if (!exe[0])
        return false;

    wchar_t sub[256], data[MAX_PATH + 16];
    bool ok = true;

    _snwprintf(sub, 255, L"%s\\%s", CLASSES, t->progid);
    sub[255] = L'\0';
    ok &= reg_set_sz(root, sub, NULL, t->friendly);

    _snwprintf(sub, 255, L"%s\\%s\\DefaultIcon", CLASSES, t->progid);
    sub[255] = L'\0';
    _snwprintf(data, MAX_PATH + 15, L"%s,0", exe);
    data[MAX_PATH + 15] = L'\0';
    ok &= reg_set_sz(root, sub, NULL, data);

    _snwprintf(sub, 255, L"%s\\%s\\shell\\open\\command", CLASSES, t->progid);
    sub[255] = L'\0';
    // Always quoted: an unquoted %1 breaks on any path containing a space
    _snwprintf(data, MAX_PATH + 15, L"\"%s\" \"%%1\"", exe);
    data[MAX_PATH + 15] = L'\0';
    ok &= reg_set_sz(root, sub, NULL, data);

    // The extension points AT npad without npad claiming to own it: an
    // OpenWithProgids entry (conventionally an empty string value named after
    // the ProgID) lists npad in Open With. The extension's own default value
    // is deliberately left alone - see the header.
    _snwprintf(sub, 255, L"%s\\%s\\OpenWithProgids", CLASSES, t->ext);
    sub[255] = L'\0';
    ok &= reg_set_sz(root, sub, t->progid, L"");

    // Capabilities is what gives npad a page in Windows 11's Default apps,
    // with its own "Set default" button
    ok &= reg_set_sz(root, CAPS_ASSOC, t->ext, t->progid);
    return ok;
}

static void type_unregister(HKEY root, const AssocType *t) {
    wchar_t sub[256], current[256];

    // If the extension's default value names OUR ProgID, remove it - that is
    // the installer's ownership claim, and leaving it pointing at a ProgID we
    // are about to delete would strand the file type with no handler at all.
    // Any other app's ProgID there is left untouched.
    _snwprintf(sub, 255, L"%s\\%s", CLASSES, t->ext);
    sub[255] = L'\0';
    if (reg_get_sz(root, sub, NULL, current, 256) && _wcsicmp(current, t->progid) == 0)
        reg_delete_value(root, sub, NULL);

    _snwprintf(sub, 255, L"%s\\%s\\OpenWithProgids", CLASSES, t->ext);
    sub[255] = L'\0';
    reg_delete_value(root, sub, t->progid);

    reg_delete_value(root, CAPS_ASSOC, t->ext);

    // The ProgID key itself last, so a crash part-way never leaves a reference
    // to a key that is gone
    reg_delete_tree(root, CLASSES, t->progid);

    // The now-empty .ext and OpenWithProgids keys are deliberately left: they
    // are inert, they may be shared, and Microsoft's guidance is that an
    // unregistered ProgID is simply ignored rather than chased.
}

// The ungated block the installer always writes: npad in the Open With list
// for any file, and an entry in Default apps. Written when anything is
// registered, never removed - it is additive, it is what the installer does
// unconditionally, and removing it would undo the installer's own work.
static void write_common(HKEY root) {
    const wchar_t *exe = exe_path();
    if (!exe[0])
        return;
    wchar_t sub[256], data[MAX_PATH + 16];

    _snwprintf(sub, 255, L"%s\\Applications\\npad.exe", CLASSES);
    sub[255] = L'\0';
    reg_set_sz(root, sub, L"FriendlyAppName", L"npad");

    _snwprintf(sub, 255, L"%s\\Applications\\npad.exe\\DefaultIcon", CLASSES);
    sub[255] = L'\0';
    _snwprintf(data, MAX_PATH + 15, L"%s,0", exe);
    data[MAX_PATH + 15] = L'\0';
    reg_set_sz(root, sub, NULL, data);

    _snwprintf(sub, 255, L"%s\\Applications\\npad.exe\\shell\\open\\command", CLASSES);
    sub[255] = L'\0';
    _snwprintf(data, MAX_PATH + 15, L"\"%s\" \"%%1\"", exe);
    data[MAX_PATH + 15] = L'\0';
    reg_set_sz(root, sub, NULL, data);

    // A SUBKEY named after the executable - the a/b/c + MRUList value shape
    // belongs under Explorer\FileExts, not here
    HKEY key = NULL;
    _snwprintf(sub, 255, L"%s\\*\\OpenWithList\\npad.exe", CLASSES);
    sub[255] = L'\0';
    if (RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_SET_VALUE | sam_for(root), NULL, &key, NULL) ==
        ERROR_SUCCESS) {
        RegCloseKey(key);
    }

    reg_set_sz(root, CAPS, L"ApplicationName", L"npad");
    reg_set_sz(root, CAPS, L"ApplicationDescription", L"Lightweight cross-platform text editor");
    _snwprintf(data, MAX_PATH + 15, L"%s,0", exe);
    data[MAX_PATH + 15] = L'\0';
    reg_set_sz(root, CAPS, L"ApplicationIcon", data);
    // The pointer is relative, resolved against the hive it was written to
    reg_set_sz(root, L"Software\\RegisteredApplications", L"npad", CAPS);
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

static void read_state(HKEY root, bool *out_groups) {
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        bool all = true;
        for (int i = 0; i < GROUPS[g].count; i++) {
            if (!type_is_registered(root, &GROUPS[g].types[i])) {
                all = false;
                break;
            }
        }
        out_groups[g] = all;
    }
}

void assoc_read_user_state(bool *out_groups) {
    read_state(HKEY_CURRENT_USER, out_groups);
}

void assoc_read_machine_state(bool *out_groups) {
    read_state(HKEY_LOCAL_MACHINE, out_groups);
}

// change[g]: +1 register, -1 unregister, 0 leave the group exactly as it is.
// A group nobody toggled is never touched - not rewritten, not repointed at
// this copy of npad, and not "tidied" if it happens to be half-registered by
// an older installer that registered extensions one at a time.
static bool apply_changes(HKEY root, const int *change) {
    bool any = false, any_register = false;
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        if (change[g] != 0)
            any = true;
        if (change[g] > 0)
            any_register = true;
    }
    if (!any)
        return true;

    if (any_register)
        write_common(root);

    bool ok = true;
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        for (int i = 0; i < GROUPS[g].count; i++) {
            if (change[g] > 0)
                ok &= type_register(root, &GROUPS[g].types[i]);
            else if (change[g] < 0)
                type_unregister(root, &GROUPS[g].types[i]);
        }
    }

    // One notification for the whole batch. Explorer caches association data
    // per process; without this the change is not visible until it restarts.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

static void diff_states(const bool *before, const bool *after, int *change) {
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        if (after[g] && !before[g])
            change[g] = 1;
        else if (!after[g] && before[g])
            change[g] = -1;
        else
            change[g] = 0;
    }
}

bool assoc_apply_user(const bool *before, const bool *after) {
    int change[ASSOC_GROUP_COUNT];
    diff_states(before, after, change);
    return apply_changes(HKEY_CURRENT_USER, change);
}

// ---------------------------------------------------------------------------
// Machine-wide, via elevation
// ---------------------------------------------------------------------------

// Program Files as the SYSTEM records it, in HKLM - not the ProgramFiles
// environment variable, which any process can set for its children and which
// would let a copy anywhere pass itself off as installed.
static bool path_under_program_files(const wchar_t *path, const wchar_t *value) {
    wchar_t dir[MAX_PATH];
    if (!reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion", value, dir,
                    MAX_PATH)) {
        return false;
    }
    size_t len = wcslen(dir);
    while (len > 0 && dir[len - 1] == L'\\')
        len--;
    return len > 0 && _wcsnicmp(path, dir, len) == 0 && path[len] == L'\\';
}

bool assoc_machine_available(void) {
    const wchar_t *exe = exe_path();
    if (!exe[0])
        return false;
    return path_under_program_files(exe, L"ProgramFilesDir") ||
           path_under_program_files(exe, L"ProgramFilesDir (x86)");
}

// Parse "+text,-log" into change[]. Tokens are split on commas only, and each
// must be exactly a sign followed by a group name from the table: "+textx",
// "+text;rm" and "text" are all ignored rather than half-matched.
static int parse_changes(const wchar_t *changes, int *change) {
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++)
        change[g] = 0;
    if (!changes)
        return 0;

    int valid = 0;
    const wchar_t *p = changes;
    for (;;) {
        const wchar_t *end = wcschr(p, L',');
        size_t len = end ? (size_t) (end - p) : wcslen(p);
        if (len >= 2 && len < 32 && (p[0] == L'+' || p[0] == L'-')) {
            for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
                wchar_t token[32];
                _snwprintf(token, 31, L"%hs", GROUPS[g].token);
                token[31] = L'\0';
                if (wcslen(token) == len - 1 && wcsncmp(p + 1, token, len - 1) == 0) {
                    change[g] = (p[0] == L'+') ? 1 : -1;
                    valid++;
                    break;
                }
            }
        }
        if (!end)
            break;
        p = end + 1;
    }
    return valid;
}

bool assoc_apply_machine(HWND owner, const bool *before, const bool *after, bool *cancelled) {
    if (cancelled)
        *cancelled = false;

    int change[ASSOC_GROUP_COUNT];
    diff_states(before, after, change);

    // The change set, as signed group tokens. Nothing user-supplied crosses
    // the elevation boundary: the child validates every token against its own
    // compiled-in table and ignores anything else.
    wchar_t params[256];
    int used = _snwprintf(params, 255, L"--register-machine ");
    if (used < 0)
        return false;
    bool any = false;
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        if (change[g] == 0)
            continue;
        int wrote = _snwprintf(params + used, (size_t) (255 - used), L"%s%lc%hs", any ? L"," : L"",
                               change[g] > 0 ? L'+' : L'-', GROUPS[g].token);
        if (wrote < 0)
            return false;
        used += wrote;
        any = true;
    }
    params[255] = L'\0';
    if (!any)
        return true; // Nothing changed: no prompt at all

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    // NOCLOSEPROCESS is what populates hProcess, without which we cannot wait
    // or read the exit code; NOASYNC because the caller may return promptly
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = owner;
    sei.lpVerb = L"runas"; // Elevates regardless of the manifest
    sei.lpFile = exe_path();
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        if (cancelled && GetLastError() == ERROR_CANCELLED)
            *cancelled = true; // The user declined the prompt; not a failure
        return false;
    }
    if (!sei.hProcess)
        return false; // No process to wait on - nothing was run

    // Wait without freezing: behave like a modal dialog, but keep pumping so
    // the UI still paints. A WM_QUIT seen here is re-posted afterwards rather
    // than swallowed, so the application still exits when it should.
    if (owner)
        EnableWindow(owner, FALSE);
    bool quit = false;
    int quit_code = 0;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &sei.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1)
            break; // The child finished - or the wait itself failed; never spin
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                quit_code = (int) msg.wParam;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);
    if (quit)
        PostQuitMessage(quit_code);
    return exit_code == 0;
}

int assoc_machine_apply_changes(const wchar_t *changes) {
    int change[ASSOC_GROUP_COUNT];
    parse_changes(changes, change);
    // HKLM only, never HKCU: under a standard user the consent prompt asks for
    // a DIFFERENT account's credentials, so this process's HKEY_CURRENT_USER
    // is the administrator's hive, not the hive of the user who asked.
    return apply_changes(HKEY_LOCAL_MACHINE, change) ? 0 : 1;
}

int assoc_machine_cli(const wchar_t *changes) {
    // Re-checked here, in the elevated process, rather than trusted from the
    // unelevated side that launched it: a machine-wide open command must never
    // point at a copy of npad its owner could later replace.
    if (!assoc_machine_available())
        return 2;
    return assoc_machine_apply_changes(changes);
}

// ---------------------------------------------------------------------------
// Installer hand-off
// ---------------------------------------------------------------------------

// Inno writes this under whichever hive the install went to. The AppId is
// installer/npad.iss's, with Inno's "{{" escape removed.
#define INNO_UNINSTALL_KEY                                                                         \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"                                   \
    L"{B7E5A2C4-9D31-4F8E-A6C0-3D2E71540A9B}_is1"

bool assoc_installer_merge_tasks(wchar_t *out, size_t cap) {
    if (!out || cap == 0)
        return false;
    out[0] = L'\0';

    // Inno's UsePreviousTasks re-applies the ticks from the original install on
    // every upgrade, which would quietly re-register a group the user has since
    // unticked on the Associations page. So tell setup which groups are NOT
    // registered - and only that.
    //
    // Never positively select a task: the installer's association tasks do
    // more than this page does - they also set the extension's default value,
    // taking ownership of the type. Selecting one because the page registered
    // that group would, on the next update, overwrite whatever other app owned
    // those extensions. Groups that are registered are left to setup's own
    // record of what the installer chose.
    bool in_user = reg_key_exists(HKEY_CURRENT_USER, INNO_UNINSTALL_KEY);
    bool in_machine = reg_key_exists(HKEY_LOCAL_MACHINE, INNO_UNINSTALL_KEY);
    if (in_user == in_machine) {
        // Neither: portable, MSI, or never installed - leave setup's defaults.
        // Both: setup will ASK whether to upgrade the per-user or the
        // all-users installation, so there is no knowing whose state applies.
        return false;
    }

    bool state[ASSOC_GROUP_COUNT];
    read_state(in_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, state);

    int w = _snwprintf(out, cap - 1, L"/MERGETASKS=\"");
    if (w < 0)
        return false;
    size_t used = (size_t) w;
    bool any = false;
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++) {
        if (state[g])
            continue;
        w = _snwprintf(out + used, cap - 1 - used, L"%s!assoc\\%hs", any ? L"," : L"",
                       GROUPS[g].token);
        if (w < 0) {
            out[0] = L'\0';
            return false;
        }
        used += (size_t) w;
        any = true;
    }
    if (!any) {
        out[0] = L'\0'; // Everything registered: nothing to deselect
        return false;
    }
    w = _snwprintf(out + used, cap - 1 - used, L"\"");
    if (w < 0) {
        out[0] = L'\0';
        return false;
    }
    out[cap - 1] = L'\0';
    return true;
}

// Whether npad has any file types recorded in this hive's Capabilities
static bool capabilities_have_types(HKEY root) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, CAPS_ASSOC, 0, KEY_QUERY_VALUE | sam_for(root), &key) != ERROR_SUCCESS)
        return false;
    DWORD values = 0;
    LSTATUS rc =
        RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && values > 0;
}

void assoc_open_default_apps(HWND owner) {
    // Windows 11 jumps straight to npad's own page - but the parameter must
    // name the hive whose registration actually lists file types. A per-user
    // RegisteredApplications entry outlives the types it once listed (that
    // block is never removed), so the entry alone is not proof: prefer the
    // hive with types, then whichever has an entry at all. Windows 10 ignores
    // the parameter and shows the general page, the right fallback anyway.
    const wchar_t *uri = L"ms-settings:defaultapps";
    if (capabilities_have_types(HKEY_CURRENT_USER))
        uri = L"ms-settings:defaultapps?registeredAppUser=npad";
    else if (capabilities_have_types(HKEY_LOCAL_MACHINE))
        uri = L"ms-settings:defaultapps?registeredAppMachine=npad";
    else if (reg_value_exists(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"npad"))
        uri = L"ms-settings:defaultapps?registeredAppUser=npad";
    else if (reg_value_exists(HKEY_LOCAL_MACHINE, L"Software\\RegisteredApplications", L"npad"))
        uri = L"ms-settings:defaultapps?registeredAppMachine=npad";

    HINSTANCE r = ShellExecuteW(owner, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR) r <= 32)
        ShellExecuteW(owner, L"open", L"ms-settings:defaultapps", NULL, NULL, SW_SHOWNORMAL);
}
