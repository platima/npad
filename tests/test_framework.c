/*
 * npad - Testing Framework Implementation
 * Lightweight unit testing framework for npad
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
// mkdtemp, setenv, lstat, opendir (c99 mode hides them otherwise)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Global test statistics
test_stats_t g_test_stats = { 0 };
bool g_test_current_failed = false;

// The suite's private working directory, and where the process started
static char *g_work_dir_utf8 = NULL;
static bool g_begun = false;
static bool g_ended = false;

#ifdef _WIN32
static wchar_t *g_work_dir_w = NULL;
static wchar_t *g_start_dir_w = NULL;

// UTF-8 -> malloc'd UTF-16 (NULL on failure)
static wchar_t *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t *w = malloc((size_t) n * sizeof(wchar_t));
    if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

// UTF-16 -> malloc'd UTF-8 (NULL on failure)
static char *to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0)
        return NULL;
    char *s = malloc((size_t) n);
    if (s && WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) != n) {
        free(s);
        s = NULL;
    }
    return s;
}

// a + "\" + b, malloc'd
static wchar_t *join_w(const wchar_t *a, const wchar_t *b) {
    size_t la = wcslen(a), lb = wcslen(b);
    wchar_t *r = malloc((la + lb + 2) * sizeof(wchar_t));
    if (!r)
        return NULL;
    memcpy(r, a, la * sizeof(wchar_t));
    size_t at = la;
    if (at == 0 || (r[at - 1] != L'\\' && r[at - 1] != L'/'))
        r[at++] = L'\\';
    memcpy(r + at, b, (lb + 1) * sizeof(wchar_t));
    return r;
}

static bool remove_tree_w(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if (attr & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(path, attr & ~(DWORD) FILE_ATTRIBUTE_READONLY);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileW(path) != 0;

    // A junction or directory symlink is removed itself, never descended into
    if (!(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        wchar_t *pattern = join_w(path, L"*");
        if (pattern) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                        continue;
                    wchar_t *child = join_w(path, fd.cFileName);
                    if (child) {
                        remove_tree_w(child);
                        free(child);
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            free(pattern);
        }
    }
    return RemoveDirectoryW(path) != 0;
}

// Append the decimal digits of v. Wide printf is avoided on purpose: MinGW's
// swprintf and _snwprintf disagree on what %s means for a wide string.
static void append_uint_w(wchar_t *buf, unsigned long v) {
    wchar_t rev[24];
    int n = 0;
    do {
        rev[n++] = (wchar_t) (L'0' + (int) (v % 10));
        v /= 10;
    } while (v && n < 23);
    size_t at = wcslen(buf);
    while (n)
        buf[at++] = rev[--n];
    buf[at] = L'\0';
}

static FILE *open_utf8(const char *path, const wchar_t *mode) {
    wchar_t *w = to_wide(path);
    FILE *f = w ? _wfopen(w, mode) : NULL;
    free(w);
    return f;
}
#else
static char *g_start_dir = NULL;

static char *join_path(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *r = malloc(la + lb + 2);
    if (!r)
        return NULL;
    memcpy(r, a, la);
    size_t at = la;
    if (at == 0 || r[at - 1] != '/')
        r[at++] = '/';
    memcpy(r + at, b, lb + 1);
    return r;
}

static bool remove_tree_posix(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(st.st_mode))
        return unlink(path) == 0;

    // A test may have made the directory read-only to provoke a failed save;
    // its entries cannot be removed until it is writable again
    chmod(path, 0700);
    DIR *d = opendir(path);
    if (d) {
        const struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;
            char *child = join_path(path, e->d_name);
            if (child) {
                remove_tree_posix(child);
                free(child);
            }
        }
        closedir(d);
    }
    return rmdir(path) == 0;
}

static char *current_dir(void) {
    size_t cap = 256;
    for (;;) {
        char *buf = malloc(cap);
        if (!buf)
            return NULL;
        if (getcwd(buf, cap))
            return buf;
        free(buf);
        if (errno != ERANGE || cap > 65536)
            return NULL;
        cap *= 2;
    }
}

static bool make_dir_posix(const char *path) {
    if (mkdir(path, 0700) == 0)
        return true;
    struct stat st;
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

static void begin_failed(const char *what) {
    printf("ERROR: test framework could not %s; refusing to run without an isolated "
           "working directory\n",
           what);
    fflush(stdout);
    exit(1);
}

static const char *work_dir_name(void) {
    return g_work_dir_utf8 ? g_work_dir_utf8 : "(unknown)";
}

static void test_framework_end(void) {
    if (!g_begun || g_ended)
        return;
    g_ended = true;
    fflush(stdout);

    const char *keep = getenv("NPAD_TEST_KEEP");
    if (keep && strcmp(keep, "1") == 0) {
        printf("Kept test directory: %s\n", work_dir_name());
        return;
    }

#ifdef _WIN32
    // The working directory cannot be removed while it is current
    if (!g_start_dir_w || !SetCurrentDirectoryW(g_start_dir_w)) {
        wchar_t tmp[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
        if (n > 0 && n <= MAX_PATH)
            SetCurrentDirectoryW(tmp);
    }
    if (g_work_dir_w && !remove_tree_w(g_work_dir_w))
        printf("WARNING: could not remove the test directory %s\n", work_dir_name());
#else
    if (!g_start_dir || chdir(g_start_dir) != 0) {
        if (chdir("/") != 0) {
            // Nothing else to try; removal below still works by absolute path
        }
    }
    if (g_work_dir_utf8 && !remove_tree_posix(g_work_dir_utf8))
        printf("WARNING: could not remove the test directory %s\n", work_dir_name());
#endif
    fflush(stdout);
}

void test_framework_begin(void) {
    if (g_begun)
        return;

#ifdef _WIN32
    wchar_t base[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, base);
    if (n == 0 || n > MAX_PATH)
        begin_failed("find the temporary directory");

    DWORD cwd_len = GetCurrentDirectoryW(0, NULL);
    if (cwd_len > 0) {
        g_start_dir_w = malloc(cwd_len * sizeof(wchar_t));
        if (g_start_dir_w && GetCurrentDirectoryW(cwd_len, g_start_dir_w) >= cwd_len) {
            free(g_start_dir_w);
            g_start_dir_w = NULL;
        }
    }

    for (unsigned i = 0; i < 1000 && !g_work_dir_w; i++) {
        wchar_t name[64] = L"npad-test-";
        append_uint_w(name, (unsigned long) GetCurrentProcessId());
        wcscat(name, L"-");
        append_uint_w(name, i);
        wchar_t *candidate = join_w(base, name);
        if (!candidate)
            break;
        if (CreateDirectoryW(candidate, NULL))
            g_work_dir_w = candidate;
        else
            free(candidate);
    }
    if (!g_work_dir_w)
        begin_failed("create a temporary directory");
    g_work_dir_utf8 = to_utf8(g_work_dir_w);
    g_begun = true;
    atexit(test_framework_end);

    if (!SetCurrentDirectoryW(g_work_dir_w))
        begin_failed("enter its temporary directory");

    // Settings: a test build (-DNPAD_TEST_HOOKS) keeps its settings.json and
    // recovery snapshots under NPAD_TEST_CONFIG_DIR instead of %APPDATA%.
    // _wputenv updates the C runtime's copy (getenv) and the process
    // environment block (GetEnvironmentVariable) alike.
    wchar_t *config = join_w(g_work_dir_w, L"config");
    if (!config || !CreateDirectoryW(config, NULL))
        begin_failed("create the settings directory");
    const wchar_t *prefix = L"NPAD_TEST_CONFIG_DIR=";
    wchar_t *assignment = malloc((wcslen(prefix) + wcslen(config) + 1) * sizeof(wchar_t));
    if (!assignment)
        begin_failed("set NPAD_TEST_CONFIG_DIR");
    wcscpy(assignment, prefix);
    wcscat(assignment, config);
    // Deliberately not freed: not every C runtime copies a putenv string
    if (_wputenv(assignment) != 0 || !SetEnvironmentVariableW(L"NPAD_TEST_CONFIG_DIR", config))
        begin_failed("set NPAD_TEST_CONFIG_DIR");
    free(config);
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] != '/')
        tmp = "/tmp";
    char *template_path = join_path(tmp, "npad-test-XXXXXX");
    if (!template_path)
        begin_failed("allocate a path");
    g_start_dir = current_dir();
    if (!mkdtemp(template_path))
        begin_failed("create a temporary directory");
    g_work_dir_utf8 = template_path;
    g_begun = true;
    atexit(test_framework_end);

    if (chdir(g_work_dir_utf8) != 0)
        begin_failed("enter its temporary directory");

    // Settings: settings_init resolves $XDG_CONFIG_HOME/npad, else
    // $HOME/.config/npad. Point both inside the temporary directory so no test
    // can read or write the real ~/.config/npad.
    char *home = join_path(g_work_dir_utf8, "home");
    char *xdg = home ? join_path(home, ".config") : NULL;
    if (!home || !xdg || !make_dir_posix(home) || !make_dir_posix(xdg))
        begin_failed("create the settings directory");
    if (setenv("HOME", home, 1) != 0 || setenv("XDG_CONFIG_HOME", xdg, 1) != 0)
        begin_failed("set HOME and XDG_CONFIG_HOME");
    // A -DNPAD_TEST_HOOKS build may honour NPAD_TEST_CONFIG_DIR here too; one
    // exported in the developer's shell would send settings outside this
    // directory and bypass the XDG resolution the settings tests exercise
    if (unsetenv("NPAD_TEST_CONFIG_DIR") != 0)
        begin_failed("clear NPAD_TEST_CONFIG_DIR");
    free(home);
    free(xdg);
#endif
}

const char *test_work_dir(void) {
    return g_work_dir_utf8;
}

void test_report_failure(const char *message, const char *file, int line) {
    if (!g_test_current_failed)
        printf("FAILED\n");
    g_test_current_failed = true;
    g_test_stats.assertions_failed++;
    printf("  Assertion failed: %s\n", message ? message : "(no message)");
    printf("  File: %s, Line: %d\n", file, line);
    fflush(stdout);
}

void test_report_eq_values(long long expected, long long actual) {
    // Formatted by hand: msvcrt's printf, which a MinGW build may use, does
    // not understand %lld
    char e[32], a[32];
    const long long vals[2] = { expected, actual };
    char *outs[2] = { e, a };
    for (int k = 0; k < 2; k++) {
        long long v = vals[k];
        unsigned long long mag = v < 0 ? 0ULL - (unsigned long long) v : (unsigned long long) v;
        char rev[32];
        int n = 0;
        do {
            rev[n++] = (char) ('0' + (int) (mag % 10));
            mag /= 10;
        } while (mag && n < 30);
        int at = 0;
        if (v < 0)
            outs[k][at++] = '-';
        while (n)
            outs[k][at++] = rev[--n];
        outs[k][at] = '\0';
    }
    printf("  Expected: %s, Actual: %s\n", e, a);
    fflush(stdout);
}

void test_run_case(const char *name, void (*fn)(void)) {
    printf("Running: %s ... ", name);
    fflush(stdout);
    g_test_current_failed = false;
    g_test_stats.tests_run++;
    fn();
    if (g_test_current_failed) {
        g_test_stats.tests_failed++;
    } else {
        g_test_stats.tests_passed++;
        printf("PASSED\n");
    }
    fflush(stdout);
}

void test_finish(void) {
    printf("\nTest Summary\n");
    printf("============\n");
    printf("Tests run:    %d\n", g_test_stats.tests_run);
    printf("Tests passed: %d\n", g_test_stats.tests_passed);
    printf("Tests failed: %d\n", g_test_stats.tests_failed);
    printf("Assertions:   %d/%d passed\n", g_test_stats.assertions_passed,
           g_test_stats.assertions_run);
    printf("\n");
    int failed = g_test_stats.tests_failed > 0 || g_test_stats.assertions_failed > 0;
    printf(failed ? "SOME TESTS FAILED!\n" : "ALL TESTS PASSED!\n");
    fflush(stdout);
    test_framework_end();
    exit(failed ? 1 : 0);
}

void test_create_temp_file(const char *filename, const char *content) {
    // Binary mode: the bytes on disk are exactly `content`. Text mode turned
    // every "\n" into CRLF on Windows, so sizes and contents read back wrong.
#ifdef _WIN32
    FILE *file = open_utf8(filename, L"wb");
#else
    FILE *file = fopen(filename, "wb");
#endif
    if (!file) {
        printf("ERROR: Failed to create temp file: %s\n", filename);
        exit(1);
    }

    if (content) {
        size_t len = strlen(content);
        if (fwrite(content, 1, len, file) != len) {
            printf("ERROR: Failed to write temp file: %s\n", filename);
            fclose(file);
            exit(1);
        }
    }

    if (fclose(file) != 0) {
        printf("ERROR: Failed to close temp file: %s\n", filename);
        exit(1);
    }
}

void test_cleanup_temp_file(const char *filename) {
    test_remove_tree(filename);
}

bool test_file_exists(const char *filename) {
    if (!filename)
        return false;
#ifdef _WIN32
    wchar_t *w = to_wide(filename);
    bool exists = w && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
    free(w);
    return exists;
#else
    struct stat st;
    return lstat(filename, &st) == 0;
#endif
}

bool test_make_dir(const char *path) {
    if (!path || !path[0])
        return false;
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    if (!w)
        return false;
    bool ok = CreateDirectoryW(w, NULL) != 0;
    if (!ok) {
        DWORD attr = GetFileAttributesW(w);
        ok = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    }
    free(w);
    return ok;
#else
    return make_dir_posix(path);
#endif
}

bool test_remove_tree(const char *path) {
    if (!path || !path[0])
        return false;
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    bool ok = w && remove_tree_w(w);
    free(w);
    return ok;
#else
    return remove_tree_posix(path);
#endif
}
