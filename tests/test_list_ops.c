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
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_SPACES, NULL), "    one\n    two",
                 "spaces indent prepends four spaces");
}

TEST_CASE(indent_tab) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_TAB, NULL), "\tone\n\ttwo",
                 "tab indent prepends a tab");
}

TEST_CASE(indent_asterisk_base) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_ASTERISK, NULL), "* one\n* two",
                 "asterisk adds '* ' with trailing space");
}

TEST_CASE(indent_asterisk_deepen) {
    ASSERT_XFORM(list_indent_lines("* one", LIST_INDENT_ASTERISK, NULL), "  * one",
                 "deepening an asterisk bullet adds two spaces");
}

TEST_CASE(indent_hyphen_base) {
    ASSERT_XFORM(list_indent_lines("one", LIST_INDENT_HYPHEN, NULL), "- one",
                 "hyphen adds '- ' with trailing space");
}

TEST_CASE(indent_hyphen_lsp_base) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_HYPHEN_LSP, NULL), " - one\n - two",
                 "hyphen-with-leading-space adds ' - '");
}

TEST_CASE(indent_hyphen_lsp_deepen) {
    // Second indent on an already-marked line adds two spaces, not another marker
    ASSERT_XFORM(list_indent_lines(" - one\n - two", LIST_INDENT_HYPHEN_LSP, NULL),
                 "   - one\n   - two", "deepening a bullet adds two spaces, keeps single marker");
}

TEST_CASE(indent_asterisk_lsp_base) {
    ASSERT_XFORM(list_indent_lines("x", LIST_INDENT_ASTERISK_LSP, NULL), " * x",
                 "asterisk-with-leading-space adds ' * '");
}

TEST_CASE(indent_mid_word_marker_not_bullet) {
    // "*one" (no space after the marker, text attached) is emphasis-like, not a
    // bullet: indenting adds a fresh marker rather than deepening
    ASSERT_XFORM(list_indent_lines("*one", LIST_INDENT_ASTERISK, NULL), "* *one",
                 "'*one' is not treated as an existing bullet");
}

TEST_CASE(unindent_asterisk_marker) {
    ASSERT_XFORM(list_unindent_lines("* one", LIST_INDENT_ASTERISK, NULL), "one",
                 "unindent removes '* ' including the trailing space");
}

TEST_CASE(unindent_bare_marker) {
    // An empty bullet is just "*" (or legacy space-less markers): unindent
    // still removes it
    ASSERT_XFORM(list_unindent_lines("*", LIST_INDENT_ASTERISK, NULL), "",
                 "unindent removes a bare marker with no content");
}

TEST_CASE(unindent_hyphen_lsp_marker) {
    ASSERT_XFORM(list_unindent_lines(" - one\n - two", LIST_INDENT_HYPHEN_LSP, NULL), "one\ntwo",
                 "unindent removes ' - ' marker (all three chars)");
}

TEST_CASE(unindent_hyphen_lsp_legacy_spaceless) {
    // Pre-0.13 bullets lacked the trailing space; a bare " -" line still strips
    ASSERT_XFORM(list_unindent_lines(" -", LIST_INDENT_HYPHEN_LSP, NULL), "",
                 "legacy space-less empty bullet strips");
}

TEST_CASE(unindent_hyphen_lsp_deepened) {
    // Reverse of the deepen case: strip one nesting step
    ASSERT_XFORM(list_unindent_lines("   - one", LIST_INDENT_HYPHEN_LSP, NULL), " - one",
                 "unindent a deepened bullet removes two spaces");
}

TEST_CASE(unindent_spaces) {
    ASSERT_XFORM(list_unindent_lines("        x", LIST_INDENT_SPACES, NULL), "    x",
                 "unindent removes up to four leading spaces");
}

TEST_CASE(unindent_tab) {
    ASSERT_XFORM(list_unindent_lines("\tx", LIST_INDENT_TAB, NULL), "x",
                 "unindent removes a leading tab");
}

TEST_CASE(unindent_noop) {
    ASSERT_XFORM(list_unindent_lines("plain", LIST_INDENT_HYPHEN_LSP, NULL), "plain",
                 "unindent with no marker is a no-op");
}

TEST_CASE(unindent_cross_format) {
    // Bullets of one style must unindent under any marker format: with the
    // default set to " - ", lines bulleted "- " (e.g. by Enter-continuation)
    // still strip
    ASSERT_XFORM(list_unindent_lines("- one\n- two", LIST_INDENT_HYPHEN_LSP, NULL), "one\ntwo",
                 "'- ' bullets unindent under the ' - ' format");
    ASSERT_XFORM(list_unindent_lines("* one", LIST_INDENT_HYPHEN, NULL), "one",
                 "'* ' bullets unindent under the '- ' format");
}

TEST_CASE(indent_deepen_cross_format) {
    // Indenting an existing bullet of a different style deepens it rather
    // than stacking a second marker
    ASSERT_XFORM(list_indent_lines("- one", LIST_INDENT_ASTERISK, NULL), "  - one",
                 "indent with '* ' format deepens an existing '- ' bullet");
}

TEST_CASE(custom_bullets_under_builtin_format) {
    // A saved custom prefix joins marker detection for the built-in formats
    ASSERT_XFORM(list_unindent_lines("> one", LIST_INDENT_HYPHEN, "> "), "one",
                 "custom '> ' bullet unindents under the '- ' format");
    ASSERT_XFORM(list_indent_lines("> one", LIST_INDENT_ASTERISK, "> "), "  > one",
                 "custom '> ' bullet deepens under the '* ' format");
}

TEST_CASE(custom_marker_base) {
    ASSERT_XFORM(list_indent_lines("one\ntwo", LIST_INDENT_CUSTOM, "> "), "> one\n> two",
                 "custom marker prefix added once per line");
}

TEST_CASE(custom_marker_deepen) {
    ASSERT_XFORM(list_indent_lines("> one", LIST_INDENT_CUSTOM, "> "), "  > one",
                 "deepening a custom bullet adds two spaces, keeps single marker");
}

TEST_CASE(custom_marker_unindent) {
    ASSERT_XFORM(list_unindent_lines("> one", LIST_INDENT_CUSTOM, "> "), "one",
                 "unindent removes the custom prefix");
}

TEST_CASE(custom_marker_unindent_deepened) {
    ASSERT_XFORM(list_unindent_lines("  > one", LIST_INDENT_CUSTOM, "> "), "> one",
                 "unindent a deepened custom bullet removes two spaces");
}

TEST_CASE(custom_whitespace_stack) {
    // A whitespace-only custom prefix stacks literally (no marker semantics)
    char *once = list_indent_lines("one", LIST_INDENT_CUSTOM, "  ");
    TEST_ASSERT_NOT_NULL(once, "whitespace custom indent (returned NULL)");
    if (once) {
        TEST_ASSERT_STR_EQ("  one", once, "whitespace custom indents literally");
        ASSERT_XFORM(list_indent_lines(once, LIST_INDENT_CUSTOM, "  "), "    one",
                     "whitespace custom stacks on second indent");
        free(once);
    }
}

TEST_CASE(custom_whitespace_strip) {
    ASSERT_XFORM(list_unindent_lines("    one", LIST_INDENT_CUSTOM, "  "), "  one",
                 "whitespace custom unindent strips one occurrence");
}

TEST_CASE(custom_long_prefix_grow) {
    // Prefix longer than the 4-byte built-in growth bound over several lines
    // (overflow regression for the per-line growth sizing)
    ASSERT_XFORM(list_indent_lines("a\nb\nc\nd\ne", LIST_INDENT_CUSTOM, "TODO: "),
                 "TODO: a\nTODO: b\nTODO: c\nTODO: d\nTODO: e",
                 "long custom prefix applied to every line");
}

TEST_CASE(custom_empty_noop) {
    ASSERT_XFORM(list_indent_lines("one", LIST_INDENT_CUSTOM, ""), "one",
                 "empty custom prefix is a no-op copy");
    ASSERT_XFORM(list_unindent_lines("one", LIST_INDENT_CUSTOM, NULL), "one",
                 "NULL custom prefix is a no-op copy");
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
    RUN_TEST(indent_asterisk_base);
    RUN_TEST(indent_asterisk_deepen);
    RUN_TEST(indent_hyphen_base);
    RUN_TEST(indent_hyphen_lsp_base);
    RUN_TEST(indent_hyphen_lsp_deepen);
    RUN_TEST(indent_asterisk_lsp_base);
    RUN_TEST(indent_mid_word_marker_not_bullet);
    RUN_TEST(unindent_asterisk_marker);
    RUN_TEST(unindent_bare_marker);
    RUN_TEST(unindent_hyphen_lsp_marker);
    RUN_TEST(unindent_hyphen_lsp_legacy_spaceless);
    RUN_TEST(unindent_hyphen_lsp_deepened);
    RUN_TEST(unindent_spaces);
    RUN_TEST(unindent_tab);
    RUN_TEST(unindent_noop);
    RUN_TEST(unindent_cross_format);
    RUN_TEST(indent_deepen_cross_format);
    RUN_TEST(custom_bullets_under_builtin_format);
    RUN_TEST(custom_marker_base);
    RUN_TEST(custom_marker_deepen);
    RUN_TEST(custom_marker_unindent);
    RUN_TEST(custom_marker_unindent_deepened);
    RUN_TEST(custom_whitespace_stack);
    RUN_TEST(custom_whitespace_strip);
    RUN_TEST(custom_long_prefix_grow);
    RUN_TEST(custom_empty_noop);

    TEST_SUMMARY();
    return 0;
}
