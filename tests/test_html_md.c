/*
 * npad - HTML to Markdown/text conversion tests
 * Unit tests for html_to_markdown and html_cf_extract_fragment (rich-text paste)
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/html_md.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Assert a converter result equals expected, freeing the malloc'd result.
#define ASSERT_MD(actual, expected, msg)                                                           \
    do {                                                                                           \
        char *_r = (actual);                                                                       \
        TEST_ASSERT_NOT_NULL(_r, msg " (returned NULL)");                                          \
        if (_r) {                                                                                  \
            TEST_ASSERT_STR_EQ((expected), _r, msg);                                               \
            free(_r);                                                                              \
        }                                                                                          \
    } while (0)

// ----------------------------- LISTS mode --------------------------------

TEST_CASE(lists_flat) {
    ASSERT_MD(html_to_markdown("<ul><li>a</li><li>b</li></ul>", HTML_MD_LISTS, "- "), "- a\n- b",
              "flat unordered list");
}

TEST_CASE(lists_nested) {
    ASSERT_MD(html_to_markdown("<ul><li>a<ul><li>b</li></ul></li></ul>", HTML_MD_LISTS, "- "),
              "- a\n  - b", "nested list, two-space indent");
}

TEST_CASE(lists_marker_override) {
    ASSERT_MD(html_to_markdown("<ul><li>a</li></ul>", HTML_MD_LISTS, "* "), "* a",
              "custom bullet marker");
}

TEST_CASE(lists_ordered_becomes_marker) {
    // In LISTS mode ordered lists use the current bullet, not numbers.
    ASSERT_MD(html_to_markdown("<ol><li>x</li><li>y</li></ol>", HTML_MD_LISTS, "- "), "- x\n- y",
              "ordered list -> bullets in LISTS mode");
}

TEST_CASE(lists_strip_inline) {
    // Inline formatting is stripped in LISTS mode.
    ASSERT_MD(html_to_markdown("<ul><li><b>bold</b> and <i>it</i></li></ul>", HTML_MD_LISTS, "- "),
              "- bold and it", "inline stripped in LISTS mode");
}

TEST_CASE(lists_null_marker_defaults) {
    ASSERT_MD(html_to_markdown("<ul><li>a</li></ul>", HTML_MD_LISTS, NULL), "- a",
              "NULL marker defaults to '- '");
}

// ------------------------------ FULL mode --------------------------------

TEST_CASE(full_ordered_numbered) {
    ASSERT_MD(html_to_markdown("<ol><li>x</li><li>y</li></ol>", HTML_MD_FULL, "- "), "1. x\n2. y",
              "ordered list numbered in FULL mode");
}

TEST_CASE(full_heading) {
    ASSERT_MD(html_to_markdown("<h1>Title</h1>", HTML_MD_FULL, "- "), "# Title", "h1 heading");
}

TEST_CASE(full_heading_level3) {
    ASSERT_MD(html_to_markdown("<h3>Sub</h3>", HTML_MD_FULL, "- "), "### Sub", "h3 heading");
}

TEST_CASE(full_bold_italic_code) {
    ASSERT_MD(html_to_markdown("<b>a</b> <i>b</i> <code>c</code>", HTML_MD_FULL, "- "),
              "**a** *b* `c`", "bold/italic/inline-code");
}

TEST_CASE(full_strong_em_aliases) {
    ASSERT_MD(html_to_markdown("<strong>a</strong><em>b</em>", HTML_MD_FULL, "- "), "**a***b*",
              "strong/em aliases");
}

TEST_CASE(full_link) {
    ASSERT_MD(html_to_markdown("<a href=\"http://x.com\">text</a>", HTML_MD_FULL, "- "),
              "[text](http://x.com)", "link");
}

TEST_CASE(full_link_entity_in_href) {
    ASSERT_MD(html_to_markdown("<a href=\"a?b=1&amp;c=2\">t</a>", HTML_MD_FULL, "- "),
              "[t](a?b=1&c=2)", "entity-decoded href");
}

TEST_CASE(full_image) {
    ASSERT_MD(html_to_markdown("<img src=\"a.png\" alt=\"cat\">", HTML_MD_FULL, "- "),
              "![cat](a.png)", "image with alt");
}

TEST_CASE(full_blockquote) {
    ASSERT_MD(html_to_markdown("<blockquote>quote</blockquote>", HTML_MD_FULL, "- "), "> quote",
              "blockquote");
}

TEST_CASE(full_pre_preserves_whitespace) {
    ASSERT_MD(html_to_markdown("<pre>line1\n  line2</pre>", HTML_MD_FULL, "- "),
              "```\nline1\n  line2\n```", "pre -> fenced code, whitespace preserved");
}

TEST_CASE(full_hr) {
    ASSERT_MD(html_to_markdown("<hr>", HTML_MD_FULL, "- "), "---", "horizontal rule");
}

TEST_CASE(full_paragraphs_blank_line) {
    ASSERT_MD(html_to_markdown("<p>a</p><p>b</p>", HTML_MD_FULL, "- "), "a\n\nb",
              "paragraphs separated by a blank line");
}

TEST_CASE(full_div_single_break) {
    ASSERT_MD(html_to_markdown("<div>a</div><div>b</div>", HTML_MD_FULL, "- "), "a\nb",
              "divs separated by a single newline");
}

TEST_CASE(full_br) {
    ASSERT_MD(html_to_markdown("a<br>b", HTML_MD_FULL, "- "), "a\nb", "br is a line break");
}

// ----------------------------- Entities ----------------------------------

TEST_CASE(entities_basic) {
    ASSERT_MD(html_to_markdown("a &amp; b &lt;c&gt; &#39;x&#39;", HTML_MD_FULL, "- "),
              "a & b <c> 'x'", "named and numeric entities");
}

TEST_CASE(entities_hex_and_nbsp) {
    // &#x41; = 'A'; &nbsp; collapses to a normal space.
    ASSERT_MD(html_to_markdown("&#x41;&nbsp;B", HTML_MD_FULL, "- "), "A B", "hex entity + nbsp");
}

TEST_CASE(entities_unknown_literal) {
    // Unknown entity is left literal.
    ASSERT_MD(html_to_markdown("x &bogus; y", HTML_MD_FULL, "- "), "x &bogus; y",
              "unknown entity kept literal");
}

// ------------------------- Whitespace / text -----------------------------

TEST_CASE(whitespace_collapse) {
    ASSERT_MD(html_to_markdown("  a   b  ", HTML_MD_FULL, "- "), "a b", "runs of whitespace collapse");
}

TEST_CASE(whitespace_newlines_collapse) {
    ASSERT_MD(html_to_markdown("a\n\n\tb", HTML_MD_FULL, "- "), "a b",
              "source newlines/tabs collapse to a space");
}

// ------------------------- Robustness / safety ---------------------------

TEST_CASE(safety_script_stripped) {
    ASSERT_MD(html_to_markdown("x<script>alert(1)</script>y", HTML_MD_FULL, "- "), "xy",
              "script content stripped");
}

TEST_CASE(safety_style_stripped) {
    ASSERT_MD(html_to_markdown("x<style>.a{color:red}</style>y", HTML_MD_FULL, "- "), "xy",
              "style content stripped");
}

TEST_CASE(safety_comment_stripped) {
    ASSERT_MD(html_to_markdown("a<!-- hidden -->b", HTML_MD_FULL, "- "), "ab",
              "HTML comment stripped");
}

TEST_CASE(safety_unclosed_bold_autoclosed) {
    ASSERT_MD(html_to_markdown("<b>bold", HTML_MD_FULL, "- "), "**bold**",
              "unclosed bold auto-closed at end");
}

TEST_CASE(safety_stray_lt) {
    ASSERT_MD(html_to_markdown("a < b", HTML_MD_FULL, "- "), "a < b",
              "stray '<' with no tag kept literal");
}

TEST_CASE(safety_unknown_tags_ignored) {
    ASSERT_MD(html_to_markdown("<span>a</span><font>b</font>", HTML_MD_FULL, "- "), "ab",
              "unknown tags ignored, text kept");
}

TEST_CASE(safety_deep_nesting_no_crash) {
    // 200 nested <ul> - past the depth clamp; must not crash and must produce
    // a bounded result.
    char in[4096];
    size_t p = 0;
    for (int i = 0; i < 200; i++) {
        memcpy(in + p, "<ul><li>", 8);
        p += 8;
    }
    memcpy(in + p, "x", 1);
    p += 1;
    for (int i = 0; i < 200; i++) {
        memcpy(in + p, "</li></ul>", 10);
        p += 10;
    }
    in[p] = '\0';
    char *r = html_to_markdown(in, HTML_MD_LISTS, "- ");
    TEST_ASSERT_NOT_NULL(r, "deep nesting returns non-NULL");
    if (r) {
        TEST_ASSERT(strstr(r, "x") != NULL, "deep nesting still emits the item text");
        free(r);
    }
}

// ------------------------------ Edge cases -------------------------------

TEST_CASE(edge_null_input) {
    TEST_ASSERT_NULL(html_to_markdown(NULL, HTML_MD_FULL, "- "), "NULL input -> NULL");
}

TEST_CASE(edge_empty_input) {
    ASSERT_MD(html_to_markdown("", HTML_MD_FULL, "- "), "", "empty input -> empty string");
}

TEST_CASE(edge_plain_mode_lists) {
    // PLAIN mode: no markers, but items still on separate lines.
    ASSERT_MD(html_to_markdown("<ul><li>a</li><li>b</li></ul>", HTML_MD_PLAIN, "- "), "a\nb",
              "plain mode list items line-separated, no marker");
}

// --------------------- CF_HTML fragment extraction -----------------------

TEST_CASE(cf_extract_offsets) {
    const char *frag = "<p>Hi</p>";
    // %010d gives a constant-width header, so the prefix length (measured with
    // placeholder zeros) is exactly the fragment's byte offset.
    int prefix_len =
        snprintf(NULL, 0, "StartFragment:%010d\r\nEndFragment:%010d\r\n<html><body>", 0, 0);
    int sf = prefix_len;
    int ef = sf + (int) strlen(frag);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "StartFragment:%010d\r\nEndFragment:%010d\r\n<html><body>%s</body></html>", sf, ef,
             frag);
    ASSERT_MD(html_cf_extract_fragment(buf), frag, "fragment via StartFragment/EndFragment offsets");
}

TEST_CASE(cf_extract_comment_markers) {
    const char *blob = "Version:0.9\r\n<html><body>"
                       "<!--StartFragment--><p>Hi</p><!--EndFragment-->"
                       "</body></html>";
    ASSERT_MD(html_cf_extract_fragment(blob), "<p>Hi</p>", "fragment via comment markers");
}

TEST_CASE(cf_extract_whole_fallback) {
    const char *blob = "<p>plain</p>";
    ASSERT_MD(html_cf_extract_fragment(blob), "<p>plain</p>", "no header/comments -> whole input");
}

TEST_CASE(cf_extract_null) {
    TEST_ASSERT_NULL(html_cf_extract_fragment(NULL), "NULL CF_HTML -> NULL");
}

TEST_CASE(cf_then_convert_pipeline) {
    const char *blob = "Version:0.9\r\n<html><body>"
                       "<!--StartFragment--><ul><li>one</li><li>two</li></ul><!--EndFragment-->"
                       "</body></html>";
    char *frag = html_cf_extract_fragment(blob);
    TEST_ASSERT_NOT_NULL(frag, "pipeline extract");
    if (frag) {
        ASSERT_MD(html_to_markdown(frag, HTML_MD_LISTS, "- "), "- one\n- two",
                  "extract then convert");
        free(frag);
    }
}

// ---- Regression tests for the adversarial-review findings (v0.19.0) ----

TEST_CASE(rev_img_alt_only_no_src) {
    // Was a heap over-read: alt Buf emitted as a C string. Now emitted by length.
    ASSERT_MD(html_to_markdown("<img alt=\"cat\">", HTML_MD_FULL, "- "), "cat",
              "img with alt but no src -> alt text (no OOB)");
}

TEST_CASE(rev_img_empty) {
    ASSERT_MD(html_to_markdown("<img>", HTML_MD_FULL, "- "), "", "empty img -> nothing, no crash");
}

TEST_CASE(rev_emphasis_trailing_space) {
    ASSERT_MD(html_to_markdown("<b>a </b>b", HTML_MD_FULL, "- "), "**a** b",
              "trailing space inside bold moves outside the delimiter");
}

TEST_CASE(rev_emphasis_italic_trailing_space) {
    ASSERT_MD(html_to_markdown("<i>a </i>b", HTML_MD_FULL, "- "), "*a* b",
              "trailing space inside italic moves outside");
}

TEST_CASE(rev_br_double_blank_line) {
    ASSERT_MD(html_to_markdown("a<br><br>b", HTML_MD_FULL, "- "), "a\n\nb",
              "consecutive <br> produce a blank line");
}

TEST_CASE(rev_pre_internal_blank_line) {
    ASSERT_MD(html_to_markdown("<pre>a\n\nb</pre>", HTML_MD_FULL, "- "), "```\na\n\nb\n```",
              "blank line inside <pre> is preserved");
}

TEST_CASE(rev_bare_li_lists) {
    ASSERT_MD(html_to_markdown("<li>a</li><li>b</li>", HTML_MD_LISTS, "- "), "- a\n- b",
              "bare <li> (no <ul>) still gets markers and line breaks");
}

TEST_CASE(rev_bare_li_plain) {
    ASSERT_MD(html_to_markdown("<li>a</li><li>b</li>", HTML_MD_PLAIN, "- "), "a\nb",
              "bare <li> in plain mode is line-separated");
}

TEST_CASE(rev_blockquote_two_paragraphs) {
    ASSERT_MD(html_to_markdown("<blockquote><p>a</p><p>b</p></blockquote>", HTML_MD_FULL, "- "),
              "> a\n>\n> b", "two quoted paragraphs keep a blank quoted separator");
}

TEST_CASE(rev_li_leading_space_trimmed) {
    ASSERT_MD(html_to_markdown("<ul><li> a</li></ul>", HTML_MD_LISTS, "- "), "- a",
              "leading space inside a list item does not double the marker space");
}

TEST_CASE(rev_nested_anchor_balanced) {
    ASSERT_MD(
        html_to_markdown("<a href=\"x\">foo <a href=\"y\">bar</a> baz</a>", HTML_MD_FULL, "- "),
        "[foo bar](x) baz", "nested <a> is ignored, outer link stays balanced");
}

TEST_CASE(rev_cf_strips_header_on_bad_offsets) {
    // Malformed offsets (EndFragment < StartFragment) + no comment markers: the
    // CF_HTML header must not be pasted as text.
    const char *blob = "Version:0.9\r\nStartFragment:99\r\nEndFragment:5\r\n"
                       "<html><body><p>x</p></body></html>";
    char *r = html_cf_extract_fragment(blob);
    TEST_ASSERT_NOT_NULL(r, "header-strip returns non-NULL");
    if (r) {
        TEST_ASSERT(strstr(r, "Version") == NULL, "CF_HTML header stripped from fallback");
        TEST_ASSERT(strncmp(r, "<html>", 6) == 0, "fallback starts at the first tag");
        free(r);
    }
}

int main(void) {
    TEST_INIT();

    RUN_TEST(lists_flat);
    RUN_TEST(lists_nested);
    RUN_TEST(lists_marker_override);
    RUN_TEST(lists_ordered_becomes_marker);
    RUN_TEST(lists_strip_inline);
    RUN_TEST(lists_null_marker_defaults);

    RUN_TEST(full_ordered_numbered);
    RUN_TEST(full_heading);
    RUN_TEST(full_heading_level3);
    RUN_TEST(full_bold_italic_code);
    RUN_TEST(full_strong_em_aliases);
    RUN_TEST(full_link);
    RUN_TEST(full_link_entity_in_href);
    RUN_TEST(full_image);
    RUN_TEST(full_blockquote);
    RUN_TEST(full_pre_preserves_whitespace);
    RUN_TEST(full_hr);
    RUN_TEST(full_paragraphs_blank_line);
    RUN_TEST(full_div_single_break);
    RUN_TEST(full_br);

    RUN_TEST(entities_basic);
    RUN_TEST(entities_hex_and_nbsp);
    RUN_TEST(entities_unknown_literal);

    RUN_TEST(whitespace_collapse);
    RUN_TEST(whitespace_newlines_collapse);

    RUN_TEST(safety_script_stripped);
    RUN_TEST(safety_style_stripped);
    RUN_TEST(safety_comment_stripped);
    RUN_TEST(safety_unclosed_bold_autoclosed);
    RUN_TEST(safety_stray_lt);
    RUN_TEST(safety_unknown_tags_ignored);
    RUN_TEST(safety_deep_nesting_no_crash);

    RUN_TEST(edge_null_input);
    RUN_TEST(edge_empty_input);
    RUN_TEST(edge_plain_mode_lists);

    RUN_TEST(cf_extract_offsets);
    RUN_TEST(cf_extract_comment_markers);
    RUN_TEST(cf_extract_whole_fallback);
    RUN_TEST(cf_extract_null);
    RUN_TEST(cf_then_convert_pipeline);

    RUN_TEST(rev_img_alt_only_no_src);
    RUN_TEST(rev_img_empty);
    RUN_TEST(rev_emphasis_trailing_space);
    RUN_TEST(rev_emphasis_italic_trailing_space);
    RUN_TEST(rev_br_double_blank_line);
    RUN_TEST(rev_pre_internal_blank_line);
    RUN_TEST(rev_bare_li_lists);
    RUN_TEST(rev_bare_li_plain);
    RUN_TEST(rev_blockquote_two_paragraphs);
    RUN_TEST(rev_li_leading_space_trimmed);
    RUN_TEST(rev_nested_anchor_balanced);
    RUN_TEST(rev_cf_strips_header_on_bad_offsets);

    TEST_SUMMARY();
    return 0;
}
