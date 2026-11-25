/*
 * npad - List Operations Tests
 * Unit tests for sort, unique, unescape, delimiter replace, and indent/unindent
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/list_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    ASSERT_XFORM(list_replace_all("a.b.c", ".", "___"), "a___b___c",
                 "replacement longer than match");
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

TEST_CASE(spaces_format_follows_configured_width) {
    ASSERT_XFORM(list_indent_lines_w("one\ntwo", LIST_INDENT_SPACES, NULL, 2), "  one\n  two",
                 "width 2 indents by two spaces");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_SPACES, NULL, 8), "        one",
                 "width 8 indents by eight spaces");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_SPACES, NULL, 4), "    one",
                 "width 4 matches the historic default");
    ASSERT_XFORM(list_unindent_lines_w("        x", LIST_INDENT_SPACES, NULL, 2), "      x",
                 "width 2 unindent strips two");
    ASSERT_XFORM(list_unindent_lines_w("   x", LIST_INDENT_SPACES, NULL, 8), "x",
                 "unindent strips only the spaces there are, up to the width");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_SPACES, NULL, 0), " one",
                 "width below 1 clamps to 1");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_SPACES, NULL, 99), "                one",
                 "width above 16 clamps to 16");
}

TEST_CASE(other_formats_ignore_the_width) {
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_TAB, NULL, 2), "\tone", "tab stays a tab");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_HYPHEN, NULL, 2), "- one",
                 "marker formats keep their marker");
    ASSERT_XFORM(list_indent_lines_w("- one", LIST_INDENT_HYPHEN, NULL, 8), "  - one",
                 "markdown nesting stays two spaces");
    ASSERT_XFORM(list_unindent_lines_w("  - one", LIST_INDENT_HYPHEN, NULL, 8), "- one",
                 "and unindent strips two");
    ASSERT_XFORM(list_indent_lines_w("one", LIST_INDENT_CUSTOM, "> ", 8), "> one",
                 "custom stays as typed");
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

TEST_CASE(bullet_marker_builtin) {
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_HYPHEN, NULL), "- ", "hyphen bullet");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_ASTERISK, NULL), "* ", "asterisk bullet");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_ASTERISK_LSP, NULL), " * ", "asterisk LSP bullet");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_HYPHEN_LSP, NULL), " - ", "hyphen LSP bullet");
}

TEST_CASE(bullet_marker_whitespace_fallback) {
    // Whitespace formats are not bullets: fall back to "- ".
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_SPACES, NULL), "- ", "spaces -> '- ' fallback");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_TAB, NULL), "- ", "tab -> '- ' fallback");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_CUSTOM, "   "), "- ",
                 "whitespace custom -> '- ' fallback");
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_CUSTOM, NULL), "- ",
                 "empty custom -> '- ' fallback");
}

TEST_CASE(bullet_marker_custom) {
    ASSERT_XFORM(list_bullet_marker(LIST_INDENT_CUSTOM, ">> "), ">> ", "custom bullet preserved");
}

TEST_CASE(marker_length_builtin) {
    TEST_ASSERT_EQ(2, (int) list_marker_length("- item", 6, NULL, false), "hyphen marker");
    TEST_ASSERT_EQ(2, (int) list_marker_length("* item", 6, NULL, false), "asterisk marker");
    TEST_ASSERT_EQ(0, (int) list_marker_length("-item", 5, NULL, false), "no space, no marker");
    TEST_ASSERT_EQ(0, (int) list_marker_length("*emph*", 6, NULL, false), "emphasis is not one");
    TEST_ASSERT_EQ(0, (int) list_marker_length("item", 4, NULL, false), "plain text");
}

TEST_CASE(marker_length_bare) {
    TEST_ASSERT_EQ(0, (int) list_marker_length("-", 1, NULL, false), "lone hyphen is still typing");
    TEST_ASSERT_EQ(1, (int) list_marker_length("-", 1, NULL, true), "lone hyphen, bare allowed");
    TEST_ASSERT_EQ(0, (int) list_marker_length("-x", 2, NULL, true), "bare needs the line to end");
}

TEST_CASE(marker_length_custom) {
    TEST_ASSERT_EQ(3, (int) list_marker_length(">> x", 4, ">> ", false), "custom body");
    TEST_ASSERT_EQ(3, (int) list_marker_length(">> x", 4, "  >> ", false), "leading space ignored");
    TEST_ASSERT_EQ(0, (int) list_marker_length(">>", 2, ">> ", false), "bare custom, not allowed");
    TEST_ASSERT_EQ(2, (int) list_marker_length(">>", 2, ">> ", true), "bare custom, allowed");
    TEST_ASSERT_EQ(2, (int) list_marker_length("- x", 3, ">> ", false), "built-ins still count");
}

// ---- Review round (NP-022, NP-051, NP-052, NP-143) ----

TEST_CASE(sort_stable_both_directions) {
    // Case-insensitive ties keep input order ascending AND descending
    ASSERT_XFORM(list_sort_lines("B\nb\na\nA", false, false), "a\nA\nB\nb",
                 "ascending ties stable");
    ASSERT_XFORM(list_sort_lines("B\nb\na\nA", true, false), "B\nb\na\nA",
                 "descending ties stable");
    ASSERT_XFORM(list_sort_lines("", false, false), "", "empty text");
    ASSERT_XFORM(list_sort_lines("\n", false, false), "\n", "a lone newline");
    ASSERT_XFORM(list_sort_lines("only", true, true), "only", "one line");
    ASSERT_XFORM(list_sort_lines("b\n\na\n", false, false), "\na\nb\n", "blank lines sort first");
}

// A tiny deterministic generator, so failures reproduce.
static unsigned lcg_next(unsigned *s) {
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7FFF;
}

// Random lines over a small alphabet (lots of duplicates and case ties).
static char *random_lines(unsigned *seed, size_t lines, size_t *out_len) {
    char *t = malloc(lines * 8 + 2);
    if (!t)
        return NULL;
    size_t p = 0;
    for (size_t i = 0; i < lines; i++) {
        size_t len = lcg_next(seed) % 4;
        for (size_t k = 0; k < len; k++)
            t[p++] = "aAbB"[lcg_next(seed) % 4];
        if (i + 1 < lines || lcg_next(seed) % 2)
            t[p++] = '\n';
    }
    t[p] = '\0';
    *out_len = p;
    return t;
}

// Reference implementations: the obvious quadratic algorithms.
static int ref_cmp(const char *a, size_t al, const char *b, size_t bl, bool cs) {
    size_t n = al < bl ? al : bl;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char) a[i], cb = (unsigned char) b[i];
        if (!cs) {
            ca = (unsigned char) ((ca >= 'A' && ca <= 'Z') ? ca + 32 : ca);
            cb = (unsigned char) ((cb >= 'A' && cb <= 'Z') ? cb + 32 : cb);
        }
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

typedef struct {
    const char *s;
    size_t n;
} RefLine;

static size_t ref_split(const char *t, RefLine *v, bool *trail) {
    size_t len = strlen(t), n = 0;
    *trail = len > 0 && t[len - 1] == '\n';
    size_t end = *trail ? len - 1 : len, st = 0;
    for (size_t i = 0; i <= end; i++) {
        if (i == end || t[i] == '\n') {
            v[n].s = t + st;
            v[n].n = i - st;
            n++;
            st = i + 1;
        }
    }
    return n;
}

static char *ref_join(const RefLine *v, size_t n, bool trail) {
    size_t total = 2;
    for (size_t i = 0; i < n; i++)
        total += v[i].n + 1;
    char *o = malloc(total), *q = o;
    if (!o)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        memcpy(q, v[i].s, v[i].n);
        q += v[i].n;
        if (i + 1 < n)
            *q++ = '\n';
    }
    if (trail)
        *q++ = '\n';
    *q = '\0';
    return o;
}

TEST_CASE(sort_unique_match_reference) {
    unsigned seed = 12345;
    RefLine *v = malloc(400 * sizeof(RefLine));
    TEST_ASSERT_NOT_NULL(v, "allocate reference lines");
    for (int trial = 0; trial < 300; trial++) {
        size_t len;
        char *t = random_lines(&seed, 1 + lcg_next(&seed) % 300, &len);
        TEST_ASSERT_NOT_NULL(t, "allocate random text");
        bool desc = trial % 2, cs = (trial / 2) % 2, trail;
        size_t n = ref_split(t, v, &trail);
        for (size_t i = 1; i < n; i++) { // stable insertion sort
            RefLine key = v[i];
            size_t j = i;
            while (j > 0) {
                int c = ref_cmp(v[j - 1].s, v[j - 1].n, key.s, key.n, cs);
                if ((desc ? -c : c) <= 0)
                    break;
                v[j] = v[j - 1];
                j--;
            }
            v[j] = key;
        }
        char *want = ref_join(v, n, trail);
        char *got = list_sort_lines(t, desc, cs);
        int same = want && got && strcmp(want, got) == 0;
        free(want);
        free(got);

        n = ref_split(t, v, &trail);
        size_t kept = 0;
        for (size_t i = 0; i < n; i++) {
            bool dup = false;
            for (size_t j = 0; j < kept && !dup; j++)
                dup = v[j].n == v[i].n && memcmp(v[j].s, v[i].s, v[i].n) == 0;
            if (!dup)
                v[kept++] = v[i];
        }
        want = ref_join(v, kept, trail);
        got = list_unique_lines(t);
        int same_u = want && got && strcmp(want, got) == 0;
        free(want);
        free(got);
        free(t);
        TEST_ASSERT(same, "merge sort matches a stable reference sort");
        TEST_ASSERT(same_u, "hash unique matches the reference");
    }
    free(v);
}

// NP-022 / NP-143: Sort and Unique on a 200k-line document. The bound is
// generous (seconds against milliseconds); the old quadratic code took
// minutes, so it still catches a regression.
TEST_CASE(sort_unique_scale_200k_lines) {
    const size_t lines = 200000;
    char *t = malloc(lines * 12 + 1);
    TEST_ASSERT_NOT_NULL(t, "allocate the large document");
    size_t p = 0;
    for (size_t i = 0; i < lines; i++)
        p += (size_t) sprintf(t + p, "line%06zu\n", i); // already ascending, all distinct
    clock_t t0 = clock();
    char *desc = list_sort_lines(t, true, false); // the old worst case
    char *asc = desc ? list_sort_lines(desc, false, true) : NULL;
    char *uniq = list_unique_lines(t);
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    int ok_desc = desc && strncmp(desc, "line199999\nline199998\n", 22) == 0;
    int ok_asc = asc && strcmp(asc, t) == 0;
    int ok_uniq = uniq && strcmp(uniq, t) == 0;
    free(desc);
    free(asc);
    free(uniq);
    free(t);
    TEST_ASSERT(ok_desc, "descending sort of sorted input");
    TEST_ASSERT(ok_asc, "sorting back restores the input");
    TEST_ASSERT(ok_uniq, "unique of distinct lines is the identity");
    TEST_ASSERT(secs < 10.0, "200k-line sort x2 + unique well under ten seconds");

    // Unique on many duplicates
    char *d = malloc(lines * 3 + 1);
    TEST_ASSERT_NOT_NULL(d, "allocate the duplicate document");
    for (size_t i = 0; i < lines; i++) {
        d[i * 3] = (char) ('a' + i % 26);
        d[i * 3 + 1] = (char) ('a' + (i / 26) % 26);
        d[i * 3 + 2] = '\n';
    }
    d[lines * 3] = '\0';
    char *u = list_unique_lines(d);
    TEST_ASSERT_NOT_NULL(u, "unique of duplicates");
    if (u)
        TEST_ASSERT_EQ(676 * 3, (int) strlen(u), "676 distinct two-letter lines kept");
    free(u);
    free(d);
}

// NP-051
TEST_CASE(unescape_surrogates_and_nul) {
    ASSERT_XFORM(list_unescape("\\uD83D\\uDE00"), "\xF0\x9F\x98\x80",
                 "a surrogate pair is one code point (4-byte UTF-8)");
    ASSERT_XFORM(list_unescape("\\ud83d\\ude00!"), "\xF0\x9F\x98\x80!", "lowercase hex pair");
    ASSERT_XFORM(list_unescape("\\uD800x"), "\xEF\xBF\xBDx", "lone high surrogate -> U+FFFD");
    ASSERT_XFORM(list_unescape("\\uDC00"), "\xEF\xBF\xBD", "lone low surrogate -> U+FFFD");
    ASSERT_XFORM(list_unescape("\\uD83D\\u0041"),
                 "\xEF\xBF\xBD"
                 "A",
                 "high surrogate followed by a non-surrogate");
    ASSERT_XFORM(list_unescape("\\uD83D"), "\xEF\xBF\xBD", "high surrogate at the end");
    ASSERT_XFORM(list_unescape("a\\u0000b"),
                 "a\xEF\xBF\xBD"
                 "b",
                 "\\u0000 does not cut the text");
    ASSERT_XFORM(list_unescape(";\\0x"), ";0x", "\\0 is not an escape: the field is not truncated");
    ASSERT_XFORM(list_unescape("\\uZZZZ"), "uZZZZ", "malformed \\u still literal");
}

// NP-052
TEST_CASE(custom_prefix_with_leading_whitespace) {
    const char *prefixes[] = { "  - ", "\t* ", "> ", " + ", "TODO: ", "\t\t- " };
    const char *lines[] = { "item", "two words", "x", "" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        for (size_t j = 0; j < sizeof(lines) / sizeof(lines[0]); j++) {
            char *once = list_indent_lines(lines[j], LIST_INDENT_CUSTOM, prefixes[i]);
            TEST_ASSERT_NOT_NULL(once, "indent");
            char *twice = list_indent_lines(once, LIST_INDENT_CUSTOM, prefixes[i]);
            TEST_ASSERT_NOT_NULL(twice, "indent again");
            char *back1 = list_unindent_lines(twice, LIST_INDENT_CUSTOM, prefixes[i]);
            TEST_ASSERT_NOT_NULL(back1, "unindent");
            char *back0 = list_unindent_lines(back1, LIST_INDENT_CUSTOM, prefixes[i]);
            TEST_ASSERT_NOT_NULL(back0, "unindent again");
            int ok1 = strcmp(back1, once) == 0, ok0 = strcmp(back0, lines[j]) == 0;
            free(once);
            free(twice);
            free(back1);
            free(back0);
            TEST_ASSERT(ok1, "unindent undoes the deepen step");
            TEST_ASSERT(ok0, "unindent undoes the custom prefix, lead-in included");
        }
    }
    ASSERT_XFORM(list_unindent_lines("  - item", LIST_INDENT_CUSTOM, "  - "), "item",
                 "'  - ' comes off whole");
    ASSERT_XFORM(list_unindent_lines("\t* item", LIST_INDENT_CUSTOM, "\t* "), "item",
                 "'\\t* ' comes off whole");
    ASSERT_XFORM(list_unindent_lines("\t- x", LIST_INDENT_HYPHEN, NULL), "- x",
                 "a tab-nested bullet loses one tab");
    ASSERT_XFORM(list_indent_lines("\t- x", LIST_INDENT_HYPHEN, NULL), "  \t- x",
                 "a tab-nested bullet deepens rather than taking a second marker");
    ASSERT_XFORM(list_indent_lines("  x", LIST_INDENT_HYPHEN, "  "), "-   x",
                 "a whitespace-only custom prefix is not a bullet");
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
    RUN_TEST(spaces_format_follows_configured_width);
    RUN_TEST(other_formats_ignore_the_width);
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
    RUN_TEST(bullet_marker_builtin);
    RUN_TEST(bullet_marker_whitespace_fallback);
    RUN_TEST(bullet_marker_custom);
    RUN_TEST(marker_length_builtin);
    RUN_TEST(marker_length_bare);
    RUN_TEST(marker_length_custom);
    RUN_TEST(sort_stable_both_directions);
    RUN_TEST(sort_unique_match_reference);
    RUN_TEST(sort_unique_scale_200k_lines);
    RUN_TEST(unescape_surrogates_and_nul);
    RUN_TEST(custom_prefix_with_leading_whitespace);

    TEST_SUMMARY();
    return 0;
}
