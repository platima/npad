/*
 * npad - List Operations Tests
 * Unit tests for sort, unique, unescape, delimiter replace, and indent/unindent
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/list_ops.h"
#include <stdlib.h>
#include <string.h>

// Assert a transform produces the expected string, freeing the malloc'd result
#define ASSERT_XFORM(actual, expected, msg)                                                        \
    do {                                                                                           \
        char *_r = (actual);                                                                       \
        TEST_ASSERT_NOT_NULL(_r, msg " (returned NULL)");                                          \
        if (_r) {                                                                                  \
            TEST_ASSERT_STR_EQ((expected), _r, msg);                                               \
            free(_r);                                                                              \
        }                                                                                          \
    } while (0)

TEST_CASE(sort_ascending) {
    ASSERT_XFORM(list_sort_lines("banana\napple\ncherry", false, false), "apple\nbanana\ncherry",
                 "ascending sort");
}

TEST_CASE(sort_descending) {
    ASSERT_XFORM(list_sort_lines("apple\nbanana\ncherry", true, false), "cherry\nbanana\napple",
                 "descending sort");
}

TEST_CASE(sort_case_insensitive_default) {
    // Case-insensitive: Banana and apple compare by folded value (a < b)
    ASSERT_XFORM(list_sort_lines("Banana\napple\nCherry", false, false), "apple\nBanana\nCherry",
                 "case-insensitive ascending");
}

TEST_CASE(sort_case_sensitive) {
    // Case-sensitive: uppercase (0x42 'B', 0x43 'C') sort before lowercase 'a' (0x61)
    ASSERT_XFORM(list_sort_lines("Banana\napple\nCherry", false, true), "Banana\nCherry\napple",
                 "case-sensitive ascending puts uppercase first");
}

TEST_CASE(sort_stable_equal_lines) {
    // Equal keys keep input order (tagged to observe stability)
    ASSERT_XFORM(list_sort_lines("b1\na\nb2", false, false), "a\nb1\nb2", "stable for equal keys");
}

TEST_CASE(sort_preserves_trailing_newline) {
    ASSERT_XFORM(list_sort_lines("b\na\n", false, false), "a\nb\n", "trailing newline preserved");
}

TEST_CASE(unique_order_preserving) {
    ASSERT_XFORM(list_unique_lines("a\nb\na\nc\nb"), "a\nb\nc", "unique keeps first occurrence");
}

TEST_CASE(unique_blank_lines) {
    ASSERT_XFORM(list_unique_lines("a\n\nb\n\n"), "a\n\nb\n",
                 "unique dedupes blank lines, keeps trailing newline");
}

TEST_CASE(unescape_basic) {
    ASSERT_XFORM(list_unescape("a\\nb\\tc"), "a\nb\tc", "unescape \\n and \\t");
}

TEST_CASE(unescape_backslash) {
    ASSERT_XFORM(list_unescape("a\\\\b"), "a\\b", "unescape doubled backslash");
}

TEST_CASE(unescape_unicode) {
    // é = e-acute = UTF-8 C3 A9
    ASSERT_XFORM(list_unescape("caf\\u00e9"), "caf\xC3\xA9", "unescape \\uXXXX to UTF-8");
}

TEST_CASE(unescape_trailing_backslash) {
    ASSERT_XFORM(list_unescape("abc\\"), "abc\\", "trailing backslash kept literally");
}

TEST_CASE(unescape_unknown_escape) {
    ASSERT_XFORM(list_unescape("a\\qb"), "aqb", "unknown escape yields following char");
}

TEST_CASE(replace_all_basic) {
    ASSERT_XFORM(list_replace_all("a,b,c", ",", "\r\n"), "a\r\nb\r\nc", "comma to CRLF");
}

TEST_CASE(replace_all_reverse) {
    ASSERT_XFORM(list_replace_all("a\r\nb\r\nc", "\r\n", ","), "a,b,c", "CRLF to comma");
}

TEST_CASE(replace_all_empty_from) {
    ASSERT_XFORM(list_replace_all("abc", "", "x"), "abc", "empty 'from' returns copy unchanged");
}

TEST_CASE(replace_all_grow) {
    ASSERT_XFORM(list_replace_all("a.b.c", ".", "___"), "a___b___c", "replacement longer than match");
}

TEST_CASE(indent_spaces) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_SPACES), "    one\n    two",
                 "spaces indent prepends four spaces");
}

TEST_CASE(indent_tab) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_TAB), "\tone\n\ttwo",
                 "tab indent prepends a tab");
}

TEST_CASE(indent_hyphen_lsp_base) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_HYPHEN_LSP), " -one\n -two",
                 "hyphen-with-leading-space adds ' -'");
}

TEST_CASE(indent_hyphen_lsp_deepen) {
    // Second indent on an already-marked line adds two spaces, not another marker
    ASSERT_XFORM(list_indent_lines(" -one\n -two", LIST_INDENT_HYPHEN_LSP), "   -one\n   -two",
                 "deepening a bullet adds two spaces, keeps single marker");
}

TEST_CASE(indent_asterisk_lsp_base) {
    ASSERT_XFORM(list_indent_lines("x", LIST_INDENT_ASTERISK_LSP), " *x",
                 "asterisk-with-leading-space adds ' *'");
}

TEST_CASE(unindent_hyphen_lsp_marker) {
    ASSERT_XFORM(list_unindent_lines(" -one\n -two", LIST_INDENT_HYPHEN_LSP), "one\ntwo",
                 "unindent removes ' -' marker (both chars)");
}

TEST_CASE(unindent_hyphen_lsp_deepened) {
    // Reverse of the deepen case: strip one nesting step
    ASSERT_XFORM(list_unindent_lines("   -one", LIST_INDENT_HYPHEN_LSP), " -one",
                 "unindent a deepened bullet removes two spaces");
}

TEST_CASE(unindent_spaces) {
    ASSERT_XFORM(list_unindent_lines("        x", LIST_INDENT_SPACES), "    x",
                 "unindent removes up to four leading spaces");
}

TEST_CASE(unindent_tab) {
    ASSERT_XFORM(list_unindent_lines("\tx", LIST_INDENT_TAB), "x", "unindent removes a leading tab");
}

TEST_CASE(unindent_noop) {
    ASSERT_XFORM(list_unindent_lines("plain", LIST_INDENT_HYPHEN_LSP), "plain",
                 "unindent with no marker is a no-op");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(sort_ascending);
    RUN_TEST(sort_descending);
    RUN_TEST(sort_case_insensitive_default);
    RUN_TEST(sort_case_sensitive);
    RUN_TEST(sort_stable_equal_lines);
    RUN_TEST(sort_preserves_trailing_newline);
    RUN_TEST(unique_order_preserving);
    RUN_TEST(unique_blank_lines);
    RUN_TEST(unescape_basic);
    RUN_TEST(unescape_backslash);
    RUN_TEST(unescape_unicode);
    RUN_TEST(unescape_trailing_backslash);
    RUN_TEST(unescape_unknown_escape);
    RUN_TEST(replace_all_basic);
    RUN_TEST(replace_all_reverse);
    RUN_TEST(replace_all_empty_from);
    RUN_TEST(replace_all_grow);
    RUN_TEST(indent_spaces);
    RUN_TEST(indent_tab);
    RUN_TEST(indent_hyphen_lsp_base);
    RUN_TEST(indent_hyphen_lsp_deepen);
    RUN_TEST(indent_asterisk_lsp_base);
    RUN_TEST(unindent_hyphen_lsp_marker);
    RUN_TEST(unindent_hyphen_lsp_deepened);
    RUN_TEST(unindent_spaces);
    RUN_TEST(unindent_tab);
    RUN_TEST(unindent_noop);

    TEST_SUMMARY();
    return 0;
}
