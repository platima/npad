/*
 * npad - RichEdit text boundary tests (Windows only)
 *
 * Drives the REAL src/platform/richedit_text.c - the code ui_win32.c loads and
 * saves the document through - against a RichEdit control (msftedit.dll)
 * created in this process. The control's parent is a top-level window that is
 * never shown, so nothing appears on screen; no other window is messaged, no
 * setting or registry value is written, and the process exits by itself. The
 * only files it writes are scratch files in the current directory, removed as
 * it goes.
 *
 * What it pins:
 *   - CR CR LF, which msftedit deletes as a soft-break marker, survives a load
 *     and comes back exactly as the core's own CRLF conversion of the input
 *   - the characters RichEdit rewrites on load (U+2028/2029, BEL, U+FFFD,
 *     U+FFFC, U+FDD0-FDEF, U+FFF9-FFFF) are REPORTED, and a sweep of every BMP
 *     code point proves the report is never wrong in either direction
 *   - VT, which GT_USECRLF used to turn into a line break, now round-trips,
 *     and Shift+Enter still types a CR (never a VT)
 *   - astral characters, RTF-looking text, a 300k-character line and text
 *     longer than one verify chunk round-trip exactly
 *   - whole files (UTF-8, UTF-16 LE, ANSI) round-trip through file_read_text_ex,
 *     the control and file_write_text_ex byte for byte
 *
 * Build: make test-richedit-win32 (cross-compiles tests/test_richedit_boundary.exe);
 * run it on Windows. Not part of `make test`, which runs natively on Linux;
 * CI runs it on windows-latest.
 *
 * Author: Platima
 * https://npad.dev
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>

#include "test_framework.h"
#include "../src/core/error.h"
#include "../src/core/file_ops.h"
#include "../src/core/thread_safety.h"
#include "../src/platform/richedit_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

static HWND g_parent = NULL;
static HWND g_edit = NULL;

// --- Helpers ------------------------------------------------------------------

// Load UTF-8 the way ui_platform_set_text does; returns the "exact" verdict
static bool load_utf8(const char *utf8, bool *ok) {
    size_t len = 0;
    bool replaced = false;
    wchar_t *wide = richedit_text_from_utf8(utf8, &len, &replaced);
    *ok = wide != NULL;
    if (!wide)
        return false;
    bool exact = richedit_load_text(g_edit, wide, len);
    free(wide);
    return exact && !replaced;
}

// The core's own conversion of text to CRLF: what a save would write
static char *core_crlf(const char *utf8) {
    return file_convert_line_endings(utf8, NPAD_EOL_CRLF);
}

static unsigned char *read_all(const char *path, size_t *len) {
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    unsigned char *buf = malloc(cap);
    while (buf) {
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0)
            break;
        if (n == cap) {
            unsigned char *grown = realloc(buf, cap * 2);
            if (!grown) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = grown;
            cap *= 2;
        }
    }
    fclose(f);
    *len = n;
    return buf;
}

static bool write_all(const char *path, const void *bytes, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(bytes, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

// bytes -> file -> core read -> control -> core write: is it what the core
// alone would have written? Returns 1 same, 0 different, -1 setup failure.
static int file_round_trip(const char *name, const void *bytes, size_t len, bool *exact) {
    char in[64], via_core[64], via_control[64];
    snprintf(in, sizeof in, "rtb_%s.in.tmp", name);
    snprintf(via_core, sizeof via_core, "rtb_%s.core.tmp", name);
    snprintf(via_control, sizeof via_control, "rtb_%s.re.tmp", name);
    int result = -1;
    if (!write_all(in, bytes, len))
        return -1;

    TextFileInfo info;
    char *text = file_read_text_ex(in, &info);
    if (text) {
        bool ok = false;
        *exact = load_utf8(text, &ok);
        char *got = ok ? richedit_get_text_utf8(g_edit) : NULL;
        if (got && file_write_text_ex(via_core, text, &info) &&
            file_write_text_ex(via_control, got, &info)) {
            size_t a_len = 0, b_len = 0;
            unsigned char *a = read_all(via_core, &a_len);
            unsigned char *b = read_all(via_control, &b_len);
            if (a && b)
                result = (a_len == b_len && memcmp(a, b, a_len) == 0) ? 1 : 0;
            free(a);
            free(b);
        }
        free(got);
        free(text);
    }
    DeleteFileA(in);
    DeleteFileA(via_core);
    DeleteFileA(via_control);
    return result;
}

// --- Normalisation --------------------------------------------------------------

TEST_CASE(normalize_matches_core_break_rules) {
    wchar_t text[32];
    wcscpy(text, L"a\r\r\nb\n\rc\r\nd\re\nf");
    size_t len = richedit_normalize_breaks(text, wcslen(text));
    TEST_ASSERT(len == wcslen(L"a\r\rb\r\rc\rd\re\rf"), "length after normalising");
    TEST_ASSERT(wcscmp(text, L"a\r\rb\r\rc\rd\re\rf") == 0,
                "every CRLF, lone CR and lone LF is one CR");
    wcscpy(text, L"");
    TEST_ASSERT_EQ(0, (long) richedit_normalize_breaks(text, 0), "empty");
}

TEST_CASE(invalid_utf8_is_reported_as_replaced) {
    size_t len = 0;
    bool replaced = false;
    wchar_t *wide = richedit_text_from_utf8("a\xFF" "b", &len, &replaced);
    TEST_ASSERT_NOT_NULL(wide, "still converts");
    TEST_ASSERT(replaced, "a bad byte is reported");
    TEST_ASSERT(len == 3 && wide[1] == 0xFFFD, "as U+FFFD");
    free(wide);
    wide = richedit_text_from_utf8("caf\xC3\xA9", &len, &replaced);
    TEST_ASSERT(wide && !replaced && len == 4, "valid UTF-8 is not");
    free(wide);
}

// --- Line breaks ----------------------------------------------------------------

TEST_CASE(cr_cr_lf_is_not_deleted) {
    const char *input = "line one\r\r\nline two\r\r\nend\r\n";
    bool ok = false;
    TEST_ASSERT(load_utf8(input, &ok), "loads exactly");
    char *got = richedit_get_text_utf8(g_edit);
    char *want = core_crlf(input);
    TEST_ASSERT_STR_EQ(want, got, "reads back as the core's CRLF conversion");
    free(got);
    free(want);
}

TEST_CASE(mixed_breaks_match_core_conversion) {
    const char *cases[] = { "a\r\nb\rc\nd\r\n\n\re\r\r\nf\r\n",
                            "a\n\rb\n\rc\n",
                            "abc\r",
                            "\r\n",
                            "\r\r\r\n\n\n",
                            "" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bool ok = false;
        TEST_ASSERT(load_utf8(cases[i], &ok), "loads exactly");
        char *got = richedit_get_text_utf8(g_edit);
        char *want = core_crlf(cases[i]);
        TEST_ASSERT_STR_EQ(want, got, "matches file_convert_line_endings");
        free(got);
        free(want);
    }
}

TEST_CASE(replacesel_with_normalised_breaks_keeps_cr_cr_lf) {
    // The paste-above and Ctrl+Drop inserts normalise before EM_REPLACESEL
    bool ok = false;
    load_utf8("", &ok);
    wchar_t ins[16];
    wcscpy(ins, L"x\r\r\ny\r\n");
    richedit_normalize_breaks(ins, wcslen(ins));
    SendMessageW(g_edit, EM_REPLACESEL, TRUE, (LPARAM) ins);
    char *got = richedit_get_text_utf8(g_edit);
    TEST_ASSERT_STR_EQ("x\r\n\r\ny\r\n", got, "both breaks survive the insert");
    free(got);
}

// richedit_get_text reads the stored form, so a VT survives a save as a VT.
// That is only right if typing never stores one: in a rich-text control
// Shift+Enter inserts VT as a soft line break, which GT_USECRLF used to turn
// back into CR LF. Pin that the plain-text control stores Shift+Enter as CR.
// The Shift state is this thread's own keyboard state array (SetKeyboardState),
// and the keys go to this process's hidden control only.
TEST_CASE(shift_enter_stores_a_paragraph_break) {
    bool ok = false;
    load_utf8("ab", &ok);
    BYTE keys[256], saved[256];
    TEST_ASSERT(GetKeyboardState(saved), "keyboard state");
    memcpy(keys, saved, sizeof keys);
    keys[VK_SHIFT] = 0x80;
    keys[VK_LSHIFT] = 0x80;
    SetKeyboardState(keys);

    // Shift+Right must extend the selection, or the Shift state is not seen
    CHARRANGE at = { 0, 0 };
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM) &at);
    SendMessageW(g_edit, WM_KEYDOWN, VK_RIGHT, 0x014D0001);
    SendMessageW(g_edit, WM_KEYUP, VK_RIGHT, 0xC14D0001);
    CHARRANGE sel = { 0, 0 };
    SendMessageW(g_edit, EM_EXGETSEL, 0, (LPARAM) &sel);
    bool shift_seen = sel.cpMin != sel.cpMax;

    at.cpMin = at.cpMax = 1;
    SendMessageW(g_edit, EM_EXSETSEL, 0, (LPARAM) &at);
    SendMessageW(g_edit, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    SendMessageW(g_edit, WM_CHAR, L'\r', 0x001C0001);
    SendMessageW(g_edit, WM_KEYUP, VK_RETURN, 0xC01C0001);
    SetKeyboardState(saved);

    wchar_t raw[16] = { 0 };
    TEXTRANGEW tr = { { 0, 8 }, raw };
    LRESULT n = SendMessageW(g_edit, EM_GETTEXTRANGE, 0, (LPARAM) &tr);
    TEST_ASSERT(shift_seen, "the simulated Shift state reaches the control");
    TEST_ASSERT(n == 3 && raw[0] == L'a' && raw[1] == L'\r' && raw[2] == L'b',
                "Shift+Enter stores CR, never VT");
}

// --- Characters RichEdit rewrites ------------------------------------------------

TEST_CASE(rewritten_characters_are_reported) {
    // Each was seen changed by msftedit on Windows 11 (LS and PS become
    // breaks, the rest spaces). VT is listed too: GT_USECRLF used to turn it
    // into a break on the way out, and the verdict must stay right for it.
    const char *altered[] = {
        "a\vb",             // VT
        "a\xE2\x80\xA8" "b", // U+2028
        "a\xE2\x80\xA9" "b", // U+2029
        "a\x07" "b",        // BEL
        "Jos\xEF\xBF\xBD",  // U+FFFD
        "a\xEF\xBF\xBC" "b", // U+FFFC
        "a\xEF\xBF\xB9" "b", // U+FFF9
        "a\xEF\xBF\xBE" "b", // U+FFFE
        "a\xEF\xBF\xBF" "b", // U+FFFF
        "a\xEF\xB7\x90" "b", // U+FDD0
        "a\xEF\xB7\xAF" "b", // U+FDEF
    };
    for (size_t i = 0; i < sizeof(altered) / sizeof(altered[0]); i++) {
        bool ok = false;
        bool exact = load_utf8(altered[i], &ok);
        TEST_ASSERT(ok, "converted");
        char *got = richedit_get_text_utf8(g_edit);
        TEST_ASSERT_NOT_NULL(got, "read back");
        bool same = strcmp(got, altered[i]) == 0;
        free(got);
        // The verdict must agree with what the control actually holds; if a
        // future msftedit stops rewriting one, that is fine as long as the
        // load then says so
        TEST_ASSERT(exact == same, "the load reports exactly what changed");
    }
}

TEST_CASE(bmp_sweep_verdict_is_never_wrong) {
    int changed = 0, wrong = 0;
    unsigned run_first = 0, run_last = 0;
    printf("\n  rewritten by this msftedit:");
    for (unsigned cp = 1; cp <= 0x10000; cp++) {
        bool same = true;
        if (cp <= 0xFFFF && !(cp >= 0xD800 && cp <= 0xDFFF) && cp != 0x0D && cp != 0x0A) {
            wchar_t in[4] = { L'a', (wchar_t) cp, L'b', 0 };
            bool exact = richedit_load_text(g_edit, in, 3);
            size_t n = 0;
            wchar_t *got = richedit_get_text(g_edit, &n);
            TEST_ASSERT_NOT_NULL(got, "read back");
            same = n == 3 && memcmp(got, in, 3 * sizeof(wchar_t)) == 0;
            free(got);
            if (exact != same) {
                if (wrong++ < 10)
                    printf("\n  U+%04X: load said %s but the control %s", cp,
                           exact ? "exact" : "altered", same ? "kept it" : "changed it");
            }
        }
        // Print the rewritten code points as ranges
        if (!same) {
            changed++;
            if (run_first && cp == run_last + 1) {
                run_last = cp;
                continue;
            }
        }
        if (run_first && (same || cp != run_last + 1)) {
            if (run_first == run_last)
                printf(" U+%04X", run_first);
            else
                printf(" U+%04X-%04X", run_first, run_last);
            run_first = 0;
        }
        if (!same)
            run_first = run_last = cp;
    }
    printf("\n  (%d code points) ", changed);
    TEST_ASSERT(wrong == 0, "load verdict matches the control's content for every code point");
}

TEST_CASE(change_beyond_the_first_verify_chunk_is_reported) {
    size_t n = 100000;
    wchar_t *text = malloc((n + 1) * sizeof(wchar_t));
    TEST_ASSERT_NOT_NULL(text, "alloc");
    for (size_t i = 0; i < n; i++)
        text[i] = L'x';
    text[70000] = 0x2028;
    text[n] = 0;
    bool exact = richedit_load_text(g_edit, text, n);
    free(text);
    TEST_ASSERT(!exact, "a separator past 64k units is still found");
}

// --- Text that must round-trip exactly -------------------------------------------

TEST_CASE(tricky_text_round_trips_exactly) {
    const char *cases[] = {
        "{\\rtf1 Hello}",
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\f0\\b Bold\\b0  text\\par\r\nline2\\par\r\n}\r\n",
        "{\\urtf1 Hello}",
        "x{\\rtf1 Hello}",
        "a\xF0\x9F\x98\x80" "b",          // U+1F600
        "a\xF0\xA0\x80\x80" "b",          // U+20000 (CJK Ext B)
        "a\xF4\x8F\xBF\xBF" "b",          // U+10FFFF
        "\ta\tb\t\tc\r\n\t\r\n",          // tabs
        "a\fb",                           // FF loads as itself via EM_SETTEXTEX
        "a\vb",                           // VT: stored intact, read without GT_USECRLF
        "a\xC2\xA0" "b\xC2\xAD" "c\xC2\x85" "d", // NBSP, SHY, NEL
        "a\xEF\xBB\xBF" "b",              // U+FEFF in the middle
        "caf\xC3\xA9 na\xC3\xAFve",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bool ok = false;
        bool exact = load_utf8(cases[i], &ok);
        char *got = richedit_get_text_utf8(g_edit);
        char *want = core_crlf(cases[i]);
        if (!exact || !got || strcmp(got, want) != 0)
            printf("\n  case %u changed\n", (unsigned) i);
        TEST_ASSERT(exact, "loads exactly");
        TEST_ASSERT_STR_EQ(want, got, "reads back unchanged");
        free(got);
        free(want);
    }
}

TEST_CASE(long_line_and_chunk_boundary_round_trip) {
    // 300k characters on one line
    size_t n = 300000;
    char *line = malloc(n + 8);
    TEST_ASSERT_NOT_NULL(line, "alloc");
    for (size_t i = 0; i < n; i++)
        line[i] = (char) ('a' + (i % 26));
    memcpy(line + n, "\r\nend", 6);
    bool ok = false;
    TEST_ASSERT(load_utf8(line, &ok), "300k-character line loads exactly");
    char *got = richedit_get_text_utf8(g_edit);
    TEST_ASSERT_STR_EQ(line, got, "and reads back unchanged");
    free(got);
    free(line);

    // A surrogate pair straddling the 64k verify chunk boundary
    size_t m = 65535;
    wchar_t *wide = malloc((m + 8) * sizeof(wchar_t));
    TEST_ASSERT_NOT_NULL(wide, "alloc");
    for (size_t i = 0; i < m; i++)
        wide[i] = L'y';
    wide[m] = 0xD83D;
    wide[m + 1] = 0xDE00;
    wide[m + 2] = L'z';
    wide[m + 3] = 0;
    TEST_ASSERT(richedit_load_text(g_edit, wide, m + 3), "pair across a chunk edge is exact");
    size_t back = 0;
    wchar_t *read = richedit_get_text(g_edit, &back);
    TEST_ASSERT(read && back == m + 3 && memcmp(read, wide, back * sizeof(wchar_t)) == 0,
                "and reads back unchanged");
    free(read);
    free(wide);
}

TEST_CASE(empty_document) {
    bool ok = false;
    TEST_ASSERT(load_utf8("", &ok) && ok, "empty loads exactly");
    size_t n = 99;
    wchar_t *got = richedit_get_text(g_edit, &n);
    TEST_ASSERT(got && n == 0 && got[0] == 0, "and reads back empty, not NULL");
    free(got);
}

TEST_CASE(get_text_copies_the_whole_text) {
    // EM_GETTEXTEX's copied count must equal the precise CRLF length for
    // every shape of break, or richedit_get_text refuses (a short copy would
    // be saved over the file)
    const char *cases[] = { "a", "a\r\n", "\r\n\r\n", "a\rb", "\r", "a\r\r\nb", "x\xF0\x9F\x98\x80\r\n" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bool ok = false;
        load_utf8(cases[i], &ok);
        char *got = richedit_get_text_utf8(g_edit);
        TEST_ASSERT_NOT_NULL(got, "whole text copied");
        free(got);
    }
}

// --- Whole files ---------------------------------------------------------------

TEST_CASE(files_round_trip_through_the_control) {
    struct {
        const char *name;
        const char *bytes;
        size_t len;
        bool exact;
    } files[] = {
#define FILE_CASE(name, lit, exact) { name, lit, sizeof(lit) - 1, exact }
        FILE_CASE("crcrlf_utf8", "a\r\r\nb\r\r\nc\r\n", true),
        FILE_CASE("crcrlf_bom", "\xEF\xBB\xBF" "a\r\r\nb\r\n", true),
        FILE_CASE("crcrlf_utf16le", "\xFF\xFE" "a\0\r\0\r\0\n\0b\0\r\0\n\0", true),
        FILE_CASE("crcrlf_ansi", "caf\xE9\r\r\nb\r\n", true),
        FILE_CASE("lf_file", "a\nb\n\nc", true),
        FILE_CASE("rtf_file", "{\\rtf1 Hello}\r\n", true),
        FILE_CASE("emoji_file", "hi \xF0\x9F\x98\x80\r\n", true),
        FILE_CASE("vt_file", "page\vbreak\r\n", true),
        FILE_CASE("ls_file", "{\"a\":\"x\xE2\x80\xA8y\"}\r\n", false),
#undef FILE_CASE
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        bool exact = false;
        int same = file_round_trip(files[i].name, files[i].bytes, files[i].len, &exact);
        if (same != (files[i].exact ? 1 : 0) || exact != files[i].exact)
            printf("\n  %s: same=%d exact=%d\n", files[i].name, same, (int) exact);
        TEST_ASSERT(same >= 0, "pipeline ran");
        TEST_ASSERT(exact == files[i].exact, "load verdict");
        if (files[i].exact)
            TEST_ASSERT(same == 1, "control path writes what the core alone writes");
        else
            TEST_ASSERT(same == 0, "a reported change really is a change");
    }
}

// --- Setup ---------------------------------------------------------------------

static LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static bool create_control(void) {
    if (!LoadLibraryW(L"msftedit.dll"))
        return false;
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = parent_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"npad_richedit_boundary_test";
    if (!RegisterClassW(&wc))
        return false;
    // Never shown: the control is a child of a window that stays hidden
    g_parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600,
                               NULL, NULL, wc.hInstance, NULL);
    if (!g_parent)
        return false;
    // npad's own style with word wrap off
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_DISABLENOSCROLL | ES_MULTILINE |
                  ES_AUTOVSCROLL | ES_NOHIDESEL | ES_WANTRETURN | WS_HSCROLL | ES_AUTOHSCROLL;
    g_edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", style, 0, 0, 780, 560, g_parent,
                             (HMENU) (INT_PTR) 1, wc.hInstance, NULL);
    if (!g_edit)
        return false;
    richedit_configure_plain_text(g_edit);
    SendMessageW(g_edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    LRESULT lang = SendMessageW(g_edit, EM_GETLANGOPTIONS, 0, 0);
    SendMessageW(g_edit, EM_SETLANGOPTIONS, 0, lang | IMF_AUTOFONT);
    return true;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    thread_safety_init();
    npad_error_init();
    TEST_INIT();

    if (!create_control()) {
        printf("Could not create a RichEdit control (msftedit.dll)\n");
        return 1;
    }

    RUN_TEST(normalize_matches_core_break_rules);
    RUN_TEST(invalid_utf8_is_reported_as_replaced);
    RUN_TEST(cr_cr_lf_is_not_deleted);
    RUN_TEST(mixed_breaks_match_core_conversion);
    RUN_TEST(replacesel_with_normalised_breaks_keeps_cr_cr_lf);
    RUN_TEST(shift_enter_stores_a_paragraph_break);
    RUN_TEST(rewritten_characters_are_reported);
    RUN_TEST(bmp_sweep_verdict_is_never_wrong);
    RUN_TEST(change_beyond_the_first_verify_chunk_is_reported);
    RUN_TEST(tricky_text_round_trips_exactly);
    RUN_TEST(long_line_and_chunk_boundary_round_trip);
    RUN_TEST(empty_document);
    RUN_TEST(get_text_copies_the_whole_text);
    RUN_TEST(files_round_trip_through_the_control);

    DestroyWindow(g_edit);
    DestroyWindow(g_parent);
    TEST_SUMMARY();
    return 0;
}
