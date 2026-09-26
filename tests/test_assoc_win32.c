/*
 * npad - File type association tests (Windows only)
 *
 * Runs the REAL src/platform/assoc_win32.c against a sandbox. Before any test,
 * RegOverridePredefKey redirects HKEY_CURRENT_USER and HKEY_LOCAL_MACHINE to
 * two scratch keys under HKCU\Software\npad-assoc-selftest - for this process
 * only - so every write the module makes lands in the sandbox and nothing the
 * machine actually uses is touched. The sandbox is deleted at the end.
 *
 * The one visible side effect is SHChangeNotify(SHCNE_ASSOCCHANGED), which
 * asks Explorer to refresh its association cache. It changes nothing.
 *
 * Build and run on Windows:  make test-assoc-win32  (cross-compiles), then
 * run tests\test_assoc_win32.exe. Not part of `make test`, which is native.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/platform/assoc_win32.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define SANDBOX L"Software\\npad-assoc-selftest"
#define CLASSES L"Software\\Classes"
#define CAPS_ASSOC L"Software\\Platima\\npad\\Capabilities\\FileAssociations"
#define INNO_UNINSTALL                                                                             \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"                                  \
    L"{B7E5A2C4-9D31-4F8E-A6C0-3D2E71540A9B}_is1"

enum { G_TEXT, G_MARKDOWN, G_DATA, G_CONFIG, G_LOG };

static wchar_t g_exe[MAX_PATH];

// --- Registry probes, through the (overridden) predefined keys --------------

static bool get_sz(HKEY root, const wchar_t *sub, const wchar_t *name, wchar_t *out, DWORD cch) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, bytes = cch * (DWORD) sizeof(wchar_t);
    LSTATUS rc = RegQueryValueExW(key, name, NULL, &type, (BYTE *) out, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ)
        return false;
    out[cch - 1] = L'\0';
    return true;
}

static bool value_exists(HKEY root, const wchar_t *sub, const wchar_t *name) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LSTATUS rc = RegQueryValueExW(key, name, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

static bool key_exists(HKEY root, const wchar_t *sub) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

static void set_sz(HKEY root, const wchar_t *sub, const wchar_t *name, const wchar_t *data) {
    HKEY key = NULL;
    if (RegCreateKeyExW(root, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *) data,
                       (DWORD) ((wcslen(data) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

static void delete_value(HKEY root, const wchar_t *sub, const wchar_t *name) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
}

static void wipe(HKEY root) {
    // Clear everything the module could have written, between tests
    RegDeleteTreeW(root, L"Software");
}

static void set_groups(bool *w, bool text, bool md, bool data, bool config, bool log) {
    w[G_TEXT] = text;
    w[G_MARKDOWN] = md;
    w[G_DATA] = data;
    w[G_CONFIG] = config;
    w[G_LOG] = log;
}

// Force every group to the given state: "before" is the exact opposite, so
// every group counts as toggled
static bool apply_full(const bool *w) {
    bool before[ASSOC_GROUP_COUNT];
    for (int g = 0; g < ASSOC_GROUP_COUNT; g++)
        before[g] = !w[g];
    return assoc_apply_user(before, w);
}

// --- Per-user registration ---------------------------------------------------

TEST_CASE(register_writes_the_installer_shape) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT];
    set_groups(w, true, false, false, false, false);
    TEST_ASSERT(apply_full(w), "registration should succeed");

    wchar_t v[MAX_PATH + 32], expect[MAX_PATH + 32];

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.txt", NULL, v, MAX_PATH + 32),
                "ProgID default value present");
    TEST_ASSERT(wcscmp(v, L"Text File") == 0, "ProgID friendly name matches the installer");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.txt\\DefaultIcon", NULL, v,
                       MAX_PATH + 32),
                "DefaultIcon present");
    _snwprintf(expect, MAX_PATH + 31, L"%s,0", g_exe);
    expect[MAX_PATH + 31] = L'\0';
    TEST_ASSERT(wcscmp(v, expect) == 0, "DefaultIcon is <exe>,0");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.txt\\shell\\open\\command", NULL, v,
                       MAX_PATH + 32),
                "open command present");
    _snwprintf(expect, MAX_PATH + 31, L"\"%s\" \"%%1\"", g_exe);
    expect[MAX_PATH + 31] = L'\0';
    TEST_ASSERT(wcscmp(v, expect) == 0, "open command quotes both the exe and %1");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\.txt\\OpenWithProgids", L"npad.txt", v,
                       MAX_PATH + 32),
                "OpenWithProgids entry present");
    TEST_ASSERT(v[0] == L'\0', "OpenWithProgids entry is the conventional empty string");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CAPS_ASSOC, L".txt", v, MAX_PATH + 32),
                "Capabilities\\FileAssociations entry present");
    TEST_ASSERT(wcscmp(v, L"npad.txt") == 0, "Capabilities maps .txt to npad.txt");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\Applications\\npad.exe", L"FriendlyAppName",
                       v, MAX_PATH + 32),
                "Applications\\npad.exe registered");
    TEST_ASSERT(wcscmp(v, L"npad") == 0, "FriendlyAppName is lowercase npad");

    TEST_ASSERT(key_exists(HKEY_CURRENT_USER, CLASSES L"\\*\\OpenWithList\\npad.exe"),
                "OpenWithList entry is a SUBKEY named after the exe");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"npad", v,
                       MAX_PATH + 32),
                "RegisteredApplications entry present");
    TEST_ASSERT(wcscmp(v, L"Software\\Platima\\npad\\Capabilities") == 0,
                "RegisteredApplications points at Capabilities, relatively");
}

TEST_CASE(never_claims_ownership_of_the_extension) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT];
    set_groups(w, true, true, true, true, true);
    TEST_ASSERT(apply_full(w), "registration should succeed");
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CLASSES L"\\.txt", NULL),
                ".txt default value must NOT be written - that is taking ownership");
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CLASSES L"\\.md", NULL),
                ".md default value must NOT be written");
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CLASSES L"\\.log", NULL),
                ".log default value must NOT be written");
}

TEST_CASE(read_state_reflects_the_registry) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT], s[ASSOC_GROUP_COUNT];
    set_groups(w, true, false, true, false, false);
    TEST_ASSERT(apply_full(w), "registration should succeed");
    assoc_read_user_state(s);
    TEST_ASSERT(s[G_TEXT] && !s[G_MARKDOWN] && s[G_DATA] && !s[G_CONFIG] && !s[G_LOG],
                "state is exactly what was applied");
}

TEST_CASE(a_group_needs_every_extension) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT], s[ASSOC_GROUP_COUNT];
    set_groups(w, false, false, true, false, false);
    TEST_ASSERT(apply_full(w), "registration should succeed");
    delete_value(HKEY_CURRENT_USER, CLASSES L"\\.toml\\OpenWithProgids", L"npad.toml");
    assoc_read_user_state(s);
    TEST_ASSERT(!s[G_DATA], "Data with one of its seven extensions missing reads as off");
}

TEST_CASE(unregister_removes_only_what_is_npads) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT], off[ASSOC_GROUP_COUNT];
    set_groups(w, true, true, false, false, false);
    TEST_ASSERT(apply_full(w), "registration should succeed");

    // As the installer leaves it: .txt claimed by npad
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.txt", NULL, L"npad.txt");
    // Another application owns .md, and is also listed for it
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.md", NULL, L"OtherApp.md");
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.md\\OpenWithProgids", L"OtherApp.md", L"");

    set_groups(off, false, false, false, false, false);
    assoc_apply_user(w, off);

    wchar_t v[256];
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CLASSES L"\\.txt", NULL),
                "npad's own ownership claim on .txt is removed");
    TEST_ASSERT(!key_exists(HKEY_CURRENT_USER, CLASSES L"\\npad.txt"), "npad.txt ProgID removed");
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CLASSES L"\\.txt\\OpenWithProgids", L"npad.txt"),
                "npad.txt OpenWithProgids entry removed");
    TEST_ASSERT(!value_exists(HKEY_CURRENT_USER, CAPS_ASSOC, L".txt"),
                "Capabilities .txt entry removed");
    TEST_ASSERT(!key_exists(HKEY_CURRENT_USER, CLASSES L"\\npad.md"), "npad.md ProgID removed");

    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\.md", NULL, v, 256) &&
                    wcscmp(v, L"OtherApp.md") == 0,
                "ANOTHER app's claim on .md is left exactly as it was");
    TEST_ASSERT(value_exists(HKEY_CURRENT_USER, CLASSES L"\\.md\\OpenWithProgids", L"OtherApp.md"),
                "another app's OpenWithProgids entry survives");
}

TEST_CASE(the_common_block_survives_unregistering) {
    wipe(HKEY_CURRENT_USER);
    bool w[ASSOC_GROUP_COUNT], off[ASSOC_GROUP_COUNT];
    set_groups(w, true, false, false, false, false);
    TEST_ASSERT(apply_full(w), "registration should succeed");
    set_groups(off, false, false, false, false, false);
    assoc_apply_user(w, off);
    TEST_ASSERT(key_exists(HKEY_CURRENT_USER, CLASSES L"\\Applications\\npad.exe"),
                "Applications\\npad.exe is kept: the installer writes it unconditionally");
}

TEST_CASE(untouched_groups_are_left_exactly_alone) {
    // The review's worst finding: Apply ran for a merely-visited page and
    // rewrote or deleted groups nobody touched. Only toggled groups may move.
    wipe(HKEY_CURRENT_USER);

    // Text registered, pointing at some OTHER copy of npad
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.txt\\shell\\open\\command", NULL,
           L"\"C:\\Elsewhere\\npad.exe\" \"%1\"");
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.txt\\OpenWithProgids", L"npad.txt", L"");
    // Config half-registered, as a per-extension legacy installer left it
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.ini", NULL, L"INI Configuration File");
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.ini\\OpenWithProgids", L"npad.ini", L"");

    bool before[ASSOC_GROUP_COUNT], after[ASSOC_GROUP_COUNT];
    assoc_read_user_state(before); // text on, config reads off (partial)
    memcpy(after, before, sizeof(after));
    after[G_LOG] = true; // The only thing the user did

    TEST_ASSERT(assoc_apply_user(before, after), "applying the one change should succeed");

    wchar_t v[MAX_PATH + 32];
    TEST_ASSERT(get_sz(HKEY_CURRENT_USER, CLASSES L"\\npad.txt\\shell\\open\\command", NULL, v,
                       MAX_PATH + 32) &&
                    wcscmp(v, L"\"C:\\Elsewhere\\npad.exe\" \"%1\"") == 0,
                "untouched Text is not repointed at this copy of npad");
    TEST_ASSERT(value_exists(HKEY_CURRENT_USER, CLASSES L"\\.ini\\OpenWithProgids", L"npad.ini"),
                "untouched, half-registered Config is not deleted");
    TEST_ASSERT(key_exists(HKEY_CURRENT_USER, CLASSES L"\\npad.log"), "the toggled group applied");
}

TEST_CASE(no_change_writes_nothing) {
    wipe(HKEY_CURRENT_USER);
    bool s[ASSOC_GROUP_COUNT];
    set_groups(s, false, false, false, false, false);
    TEST_ASSERT(assoc_apply_user(s, s), "an empty change set succeeds");
    TEST_ASSERT(!key_exists(HKEY_CURRENT_USER, CLASSES L"\\Applications\\npad.exe"),
                "nothing at all is written when nothing changed");
}

// --- Machine-wide ------------------------------------------------------------

TEST_CASE(machine_changes_accept_only_exact_tokens) {
    wipe(HKEY_LOCAL_MACHINE);
    bool s[ASSOC_GROUP_COUNT];
    // Unknown names, a token glued to shell punctuation, a token with a
    // suffix, and an unsigned token must all be ignored
    TEST_ASSERT_EQ(0, assoc_machine_apply_changes(L"+text,+evil,+markdown;rm -rf,+datafoo,+log,config"),
                   "the elevated change interpreter should succeed");
    assoc_read_machine_state(s);
    TEST_ASSERT(s[G_TEXT], "+text accepted");
    TEST_ASSERT(!s[G_MARKDOWN], "+markdown glued to ';' rejected");
    TEST_ASSERT(!s[G_DATA], "+datafoo does not match data");
    TEST_ASSERT(!s[G_CONFIG], "an unsigned token is not an instruction");
    TEST_ASSERT(s[G_LOG], "+log accepted");
    TEST_ASSERT(!value_exists(HKEY_LOCAL_MACHINE, CLASSES L"\\.txt", NULL),
                "machine-wide registration never claims ownership either");
}

TEST_CASE(machine_changes_touch_only_named_groups) {
    wipe(HKEY_LOCAL_MACHINE);
    bool s[ASSOC_GROUP_COUNT];
    assoc_machine_apply_changes(L"+text,+markdown,+data,+config,+log");
    assoc_read_machine_state(s);
    TEST_ASSERT(s[G_TEXT] && s[G_MARKDOWN] && s[G_DATA] && s[G_CONFIG] && s[G_LOG],
                "every group registered machine-wide");
    TEST_ASSERT_EQ(0, assoc_machine_apply_changes(L"-markdown,-log"), "removal should succeed");
    assoc_read_machine_state(s);
    TEST_ASSERT(s[G_TEXT] && !s[G_MARKDOWN] && s[G_DATA] && s[G_CONFIG] && !s[G_LOG],
                "only the two named groups were removed");
}

TEST_CASE(empty_or_garbage_changes_do_nothing) {
    // A missing argument used to mean "unregister everything"
    wipe(HKEY_LOCAL_MACHINE);
    bool s[ASSOC_GROUP_COUNT];
    assoc_machine_apply_changes(L"+log");
    TEST_ASSERT_EQ(0, assoc_machine_apply_changes(L""), "an empty list is a no-op");
    TEST_ASSERT_EQ(0, assoc_machine_apply_changes(NULL), "a missing list is a no-op");
    TEST_ASSERT_EQ(0, assoc_machine_apply_changes(L"none,+,-,log,+logs,--log"), "garbage is a no-op");
    assoc_read_machine_state(s);
    TEST_ASSERT(s[G_LOG], "log is still registered after all of that");
}

TEST_CASE(machine_cli_refuses_outside_program_files) {
    wipe(HKEY_LOCAL_MACHINE);
    bool s[ASSOC_GROUP_COUNT];
    // This test binary lives in the repo or a temp folder, never Program Files
    TEST_ASSERT(!assoc_machine_available(),
                "an exe outside Program Files must not offer all-users registration");

    // Pointing the ProgramFiles environment variable at our own folder - which
    // any launching process can do - must not change that answer
    wchar_t dir[MAX_PATH];
    wcscpy(dir, g_exe);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = L'\0';
    SetEnvironmentVariableW(L"ProgramFiles", dir);
    SetEnvironmentVariableW(L"ProgramW6432", dir);
    TEST_ASSERT(!assoc_machine_available(),
                "a spoofed ProgramFiles environment variable is not believed");

    TEST_ASSERT_EQ(2, assoc_machine_cli(L"+text,+log"),
                   "the elevated entry point refuses to run from here");
    assoc_read_machine_state(s);
    TEST_ASSERT(!s[G_TEXT] && !s[G_LOG], "and it wrote nothing");
}

TEST_CASE(user_and_machine_hives_stay_separate) {
    wipe(HKEY_CURRENT_USER);
    wipe(HKEY_LOCAL_MACHINE);
    bool w[ASSOC_GROUP_COUNT], u[ASSOC_GROUP_COUNT], m[ASSOC_GROUP_COUNT];
    set_groups(w, true, false, false, false, false);
    apply_full(w);
    assoc_machine_apply_changes(L"+log");
    assoc_read_user_state(u);
    assoc_read_machine_state(m);
    TEST_ASSERT(u[G_TEXT] && !u[G_LOG], "per-user has text, not log");
    TEST_ASSERT(m[G_LOG] && !m[G_TEXT], "machine has log, not text");
}

// --- Installer hand-off ------------------------------------------------------

TEST_CASE(merge_tasks_only_ever_deselect) {
    wipe(HKEY_CURRENT_USER);
    wipe(HKEY_LOCAL_MACHINE);
    wchar_t out[200];

    TEST_ASSERT(!assoc_installer_merge_tasks(out, 200), "no Inno install: nothing to pass");
    TEST_ASSERT(out[0] == L'\0', "and the argument is empty");

    set_sz(HKEY_CURRENT_USER, INNO_UNINSTALL, L"DisplayName", L"npad");
    bool w[ASSOC_GROUP_COUNT];
    set_groups(w, true, false, true, false, false);
    apply_full(w);
    TEST_ASSERT(assoc_installer_merge_tasks(out, 200), "a per-user Inno install is found");
    // Registered groups must NOT be selected: the installer's tasks also set
    // the extension's default value, so selecting one would take ownership of
    // types the page only registered. Only unregistered groups are named.
    TEST_ASSERT(wcscmp(out, L"/MERGETASKS=\"!assoc\\markdown,!assoc\\config,!assoc\\log\"") == 0,
                "only the unregistered groups are named, each deselected");

    set_groups(w, true, true, true, true, true);
    apply_full(w);
    TEST_ASSERT(!assoc_installer_merge_tasks(out, 200) && out[0] == L'\0',
                "everything registered: nothing to deselect, nothing passed");
}

TEST_CASE(merge_tasks_skip_when_both_installs_exist) {
    // Setup ASKS which one to upgrade when both exist, so neither hive's state
    // can be assumed to apply
    wipe(HKEY_CURRENT_USER);
    wipe(HKEY_LOCAL_MACHINE);
    set_sz(HKEY_CURRENT_USER, INNO_UNINSTALL, L"DisplayName", L"npad");
    set_sz(HKEY_LOCAL_MACHINE, INNO_UNINSTALL, L"DisplayName", L"npad");
    wchar_t out[200];
    TEST_ASSERT(!assoc_installer_merge_tasks(out, 200) && out[0] == L'\0',
                "per-user and all-users installs both present: pass nothing");
}

TEST_CASE(an_orphaned_openwithprogids_entry_is_not_a_registration) {
    // Uninstall removes the ProgID but leaves the OpenWithProgids entry, which
    // Windows ignores. It must not make the group read as registered.
    wipe(HKEY_CURRENT_USER);
    set_sz(HKEY_CURRENT_USER, CLASSES L"\\.log\\OpenWithProgids", L"npad.log", L"");
    bool s[ASSOC_GROUP_COUNT];
    assoc_read_user_state(s);
    TEST_ASSERT(!s[G_LOG], "an entry naming a missing ProgID reads as unregistered");
}

// --- Sandbox -----------------------------------------------------------------

int main(void) {
    TEST_INIT();

    DWORD n = GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        printf("could not resolve own path\n");
        return 1;
    }
    wchar_t longer[MAX_PATH];
    DWORD m = GetLongPathNameW(g_exe, longer, MAX_PATH);
    if (m > 0 && m < MAX_PATH)
        wcscpy(g_exe, longer);

    // Open both sandbox roots on the REAL HKCU before redirecting anything
    HKEY user = NULL, machine = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SANDBOX L"\\user", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                        &user, NULL) != ERROR_SUCCESS ||
        RegCreateKeyExW(HKEY_CURRENT_USER, SANDBOX L"\\machine", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                        &machine, NULL) != ERROR_SUCCESS) {
        printf("could not create the sandbox\n");
        return 1;
    }

    // assoc_machine_available reads Program Files from HKLM, which is about to
    // be redirected: seed the sandbox with the real values so that check sees
    // the same answer it would on this machine
    wchar_t pf[MAX_PATH], pf86[MAX_PATH];
    bool have_pf = get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                          L"ProgramFilesDir", pf, MAX_PATH);
    bool have_pf86 = get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                            L"ProgramFilesDir (x86)", pf86, MAX_PATH);

    if (RegOverridePredefKey(HKEY_CURRENT_USER, user) != ERROR_SUCCESS ||
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine) != ERROR_SUCCESS) {
        printf("could not redirect the predefined keys\n");
        return 1;
    }

    RUN_TEST(register_writes_the_installer_shape);
    RUN_TEST(never_claims_ownership_of_the_extension);
    RUN_TEST(read_state_reflects_the_registry);
    RUN_TEST(a_group_needs_every_extension);
    RUN_TEST(unregister_removes_only_what_is_npads);
    RUN_TEST(the_common_block_survives_unregistering);
    RUN_TEST(untouched_groups_are_left_exactly_alone);
    RUN_TEST(no_change_writes_nothing);
    RUN_TEST(machine_changes_accept_only_exact_tokens);
    RUN_TEST(machine_changes_touch_only_named_groups);
    RUN_TEST(empty_or_garbage_changes_do_nothing);

    // Re-seed Program Files for the location check (the tests above wiped it)
    if (have_pf)
        set_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
               L"ProgramFilesDir", pf);
    if (have_pf86)
        set_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
               L"ProgramFilesDir (x86)", pf86);
    RUN_TEST(machine_cli_refuses_outside_program_files);

    RUN_TEST(user_and_machine_hives_stay_separate);
    RUN_TEST(merge_tasks_only_ever_deselect);
    RUN_TEST(merge_tasks_skip_when_both_installs_exist);
    RUN_TEST(an_orphaned_openwithprogids_entry_is_not_a_registration);

    // Restore the real predefined keys, then remove the sandbox from the real
    // HKCU. Nothing outside it was ever written.
    RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
    RegOverridePredefKey(HKEY_LOCAL_MACHINE, NULL);
    RegCloseKey(user);
    RegCloseKey(machine);
    RegDeleteTreeW(HKEY_CURRENT_USER, SANDBOX);

    TEST_SUMMARY();
    return 0;
}
