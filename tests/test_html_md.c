/*
 * npad - HTML to Markdown/text conversion tests
 * Unit tests for html_to_markdown and html_cf_extract_fragment (rich-text paste)
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/html_md.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    ASSERT_MD(html_to_markdown("a &amp; b &lt;c&gt; &#39;x&#39;", HTML_MD_PLAIN, "- "),
              "a & b <c> 'x'", "named and numeric entities");
    // In Markdown a decoded "<c>" would be raw HTML, so its '<' is escaped
    ASSERT_MD(html_to_markdown("a &amp; b &lt;c&gt; &#39;x&#39;", HTML_MD_FULL, "- "),
              "a & b \\<c> 'x'", "decoded tag-like text escaped in Markdown");
}

TEST_CASE(entities_hex_and_nbsp) {
    // &#x41; = 'A'; &nbsp; collapses to a normal space.
    ASSERT_MD(html_to_markdown("&#x41;&nbsp;B", HTML_MD_FULL, "- "), "A B", "hex entity + nbsp");
}

TEST_CASE(entities_unknown_literal) {
    // Unknown entity is left literal.
    ASSERT_MD(html_to_markdown("x &bogus; y", HTML_MD_PLAIN, "- "), "x &bogus; y",
              "unknown entity kept literal");
    // ... and in Markdown its '&' is escaped, so no renderer decodes it
    ASSERT_MD(html_to_markdown("x &bogus; y", HTML_MD_FULL, "- "), "x \\&bogus; y",
              "entity-like text escaped in Markdown");
}

// ------------------------- Whitespace / text -----------------------------

TEST_CASE(whitespace_collapse) {
    ASSERT_MD(html_to_markdown("  a   b  ", HTML_MD_FULL, "- "), "a b",
              "runs of whitespace collapse");
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
    ASSERT_MD(html_cf_extract_fragment(buf), frag,
              "fragment via StartFragment/EndFragment offsets");
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

// ---- Review round (NP-023..026, NP-047..050) ----

// NP-023: Lists mode converts only a fragment that holds a list; for anything
// else the caller pastes the source's own plain text.
TEST_CASE(lists_mode_needs_a_list) {
    ASSERT_MD(html_to_markdown("<p>plain <b>para</b></p>", HTML_MD_LISTS, "- "), "",
              "no list item -> empty (plain-text fallback)");
    ASSERT_MD(
        html_to_markdown("<table><tr><td>Name</td><td>Qty</td></tr></table>", HTML_MD_LISTS, "- "),
        "", "a table is not a list");
    ASSERT_MD(html_to_markdown("<!-- <li> -->x<script>'<li>'</script>", HTML_MD_LISTS, "- "), "",
              "an <li> in a comment or script does not count");
    ASSERT_MD(html_to_markdown("<p>Intro</p><ul><li>a</li></ul>", HTML_MD_LISTS, "- "),
              "Intro\n- a", "a fragment with a list still converts whole");
}

TEST_CASE(block_elements_break_lines) {
    ASSERT_MD(html_to_markdown("<section>a</section><article>b</article><footer>c</footer>",
                               HTML_MD_FULL, "- "),
              "a\nb\nc", "section/article/footer are blocks");
    ASSERT_MD(html_to_markdown("<dl><dt>term</dt><dd>def</dd></dl>", HTML_MD_PLAIN, "- "),
              "term\ndef", "definition list parts are blocks");
}

TEST_CASE(tables_markdown_and_plain) {
    const char *t = "<table><tr><th>Name</th><th>Qty</th></tr>"
                    "<tr><td>Apple</td><td>3</td></tr><tr><td>a|b</td><td></td></tr></table>";
    ASSERT_MD(html_to_markdown(t, HTML_MD_FULL, "- "),
              "| Name | Qty |\n| --- | --- |\n| Apple | 3 |\n| a\\|b |  |",
              "GitHub pipe table, '|' escaped in a cell");
    ASSERT_MD(html_to_markdown(t, HTML_MD_PLAIN, "- "), "Name\tQty\nApple\t3\na|b",
              "plain: tab-separated cells, one row per line (trailing tab trimmed at the end)");
    ASSERT_MD(html_to_markdown("<p>Before</p><table><tr><td><p>x</p><p>y</p></td></tr></table>"
                               "<p>After</p>",
                               HTML_MD_FULL, "- "),
              "Before\n\n| x y |\n| --- |\n\nAfter", "blocks inside a cell become spaces");
    ASSERT_MD(html_to_markdown("<table><tr><td>a<table><tr><td>in</td></tr></table></td>"
                               "<td>b</td></tr></table>",
                               HTML_MD_FULL, "- "),
              "| a in | b |\n| --- | --- |", "a nested table flattens into its cell");
}

TEST_CASE(white_space_pre_block) {
    // VS Code / Windows Terminal: a <div> styled white-space:pre, one <div> or
    // <br> per line
    const char *h = "<div style=\"font-family: Consolas, 'Courier New';white-space: pre;\">"
                    "<div><span>def f(x):</span></div>"
                    "<div><span>&nbsp; &nbsp; return x</span></div></div><p>after</p>";
    ASSERT_MD(html_to_markdown(h, HTML_MD_FULL, "- "), "```\ndef f(x):\n    return x\n```\n\nafter",
              "fenced, indentation kept");
    ASSERT_MD(html_to_markdown(h, HTML_MD_PLAIN, "- "), "def f(x):\n    return x\nafter",
              "plain keeps the indentation too");
    ASSERT_MD(html_to_markdown(h, HTML_MD_LISTS, "- "), "", "Lists mode leaves code to plain text");
    ASSERT_MD(
        html_to_markdown("<p><span style=\"white-space:pre\">a   b</span></p>", HTML_MD_FULL, "- "),
        "a b", "an inline white-space:pre span does not make a code block");
    ASSERT_MD(html_to_markdown("<div style=\"white-space: pre-wrap;\">Hello\nthere</div>",
                               HTML_MD_FULL, "- "),
              "Hello there", "pre-wrap prose is not code");
}

// NP-024: a block opener right after the marker stays on the marker line, and
// continuation lines are indented under the item text.
TEST_CASE(li_block_children) {
    ASSERT_MD(html_to_markdown("<ul><li><p>First</p></li><li><p>Second</p></li></ul>",
                               HTML_MD_LISTS, "- "),
              "- First\n- Second", "li>p (Google Docs, LibreOffice)");
    ASSERT_MD(html_to_markdown("<ul><li><div>First</div></li><li><div>Second</div></li></ul>",
                               HTML_MD_FULL, "- "),
              "- First\n- Second", "li>div");
    ASSERT_MD(html_to_markdown("<ul><li><h3>Title</h3></li></ul>", HTML_MD_FULL, "- "),
              "- ### Title", "li>h3 is a heading inside the item");
    ASSERT_MD(html_to_markdown("<ul><li>a<br>b</li></ul>", HTML_MD_LISTS, "- "), "- a\n  b",
              "<br> inside an item continues under the text");
    ASSERT_MD(html_to_markdown("<ul><li>a<ul><li>b</li></ul>c</li></ul>", HTML_MD_LISTS, "- "),
              "- a\n  - b\n  c", "text after a nested list stays in the outer item");
    ASSERT_MD(html_to_markdown("<ul><li><p>a</p><p>b</p></li></ul>", HTML_MD_FULL, "- "),
              "- a\n\n  b", "a second paragraph in an item is indented (loose item)");
    ASSERT_MD(html_to_markdown("<ul><li>a</li></ul><p>after</p>", HTML_MD_FULL, "- "),
              "- a\n\nafter", "a paragraph after a list does not join its last item");
}

// NP-025: the Google Docs wrapper <b> is not bold; styled spans are.
TEST_CASE(google_docs_wrapper_bold) {
    ASSERT_MD(html_to_markdown("<b style=\"font-weight:normal;\" id=\"docs-internal-guid-1a2b\">"
                               "<p><span>Para one</span></p><p><span>Para two</span></p></b>",
                               HTML_MD_FULL, "- "),
              "Para one\n\nPara two", "no stray ** around the paste");
    ASSERT_MD(html_to_markdown("<span style=\"font-weight:700\">B</span> "
                               "<span style=\"font-style:italic\">I</span> "
                               "<span style=\"font-weight:400\">N</span>",
                               HTML_MD_FULL, "- "),
              "**B** *I* N", "font-weight / font-style on spans");
    ASSERT_MD(html_to_markdown("<b id=\"docs-internal-guid-x\"><span style=\"font-weight:bold\">"
                               "a</span></b>",
                               HTML_MD_FULL, "- "),
              "**a**", "real bold inside the wrapper survives");
}

// NP-026: delimiters are written only around real content.
TEST_CASE(emphasis_written_lazily) {
    ASSERT_MD(html_to_markdown("<b></b>", HTML_MD_FULL, "- "), "", "empty bold writes nothing");
    ASSERT_MD(html_to_markdown("x<b></b>y<i> </i>z<code></code>", HTML_MD_FULL, "- "), "xy z",
              "empty and space-only elements leave no delimiters");
    ASSERT_MD(html_to_markdown("word<b> bold</b> end", HTML_MD_FULL, "- "), "word **bold** end",
              "a leading inner space moves outside");
    ASSERT_MD(html_to_markdown("<b><p>a</p><p>b</p></b>", HTML_MD_FULL, "- "), "**a**\n\n**b**",
              "emphasis is closed and reopened at block breaks");
    ASSERT_MD(html_to_markdown("<b>a<strong>b</strong>c</b>", HTML_MD_FULL, "- "), "**abc**",
              "nested bold counts, does not close early");
    ASSERT_MD(html_to_markdown("<b>a <i>b</i></b>", HTML_MD_FULL, "- "), "**a *b***",
              "bold and italic close innermost first");
    ASSERT_MD(html_to_markdown("<b><a href=\"u\">x</a></b> <a href=\"u\"><i>y</i></a>",
                               HTML_MD_FULL, "- "),
              "**[x](u)** [*y*](u)", "emphasis and links nest either way");
    ASSERT_MD(html_to_markdown("<a href=\"u\"><p>a</p><p>b</p></a>", HTML_MD_FULL, "- "),
              "[a](u)\n\n[b](u)", "a link never spans a paragraph break");
    ASSERT_MD(html_to_markdown("<code>a<b>b</b></code>", HTML_MD_FULL, "- "), "`ab`",
              "nothing opens inside a code span");
    ASSERT_MD(html_to_markdown("<b>a<br>b</b>", HTML_MD_FULL, "- "), "**a**\n**b**",
              "a line break closes and reopens");
}

// NP-047: ordered-list children nest under the item text; start= and value=.
TEST_CASE(ordered_nesting_and_start) {
    ASSERT_MD(
        html_to_markdown("<ol><li>a<ul><li>b</li></ul></li><li>c</li></ol>", HTML_MD_FULL, "- "),
        "1. a\n   - b\n2. c", "children of '1. ' indent three spaces");
    ASSERT_MD(html_to_markdown("<ol><li>a<ol><li>b</li></ol></li></ol>", HTML_MD_FULL, "- "),
              "1. a\n   1. b", "ordered in ordered");
    ASSERT_MD(html_to_markdown("<ol start=\"5\"><li>x</li><li>y</li></ol>", HTML_MD_FULL, "- "),
              "5. x\n6. y", "start= is honoured");
    ASSERT_MD(html_to_markdown("<ol start=0><li>x</li></ol>", HTML_MD_FULL, "- "), "0. x",
              "start=0");
    ASSERT_MD(html_to_markdown("<ol><li>x</li><li value=\"10\">y</li><li>z</li></ol>", HTML_MD_FULL,
                               "- "),
              "1. x\n10. y\n11. z", "li value= renumbers from there");
    ASSERT_MD(html_to_markdown("<ol start=\"99999999999\"><li>x</li></ol>", HTML_MD_FULL, "- "),
              "999999999. x", "start is clamped to nine digits");
    ASSERT_MD(
        html_to_markdown("<ol start=\"5\"><li>x<ul><li>y</li></ul></li></ol>", HTML_MD_LISTS, "- "),
        "- x\n  - y", "Lists mode keeps bullets and two-space nesting");
}

// NP-048: the tag scanner skips quoted attribute values.
TEST_CASE(quote_aware_tags) {
    ASSERT_MD(html_to_markdown("<a title=\"a>b\" href=\"u\">t</a>", HTML_MD_FULL, "- "), "[t](u)",
              "'>' inside a quoted value");
    ASSERT_MD(
        html_to_markdown("<a title=\"see href=evil\" href=\"good\">t</a>", HTML_MD_FULL, "- "),
        "[t](good)", "href= inside another attribute's value");
    ASSERT_MD(html_to_markdown("<img alt='x > y' src=\"s.png\">", HTML_MD_FULL, "- "),
              "![x > y](s.png)", "single-quoted value with '>'");
    ASSERT_MD(html_to_markdown("<p class=\"a>b\">text</p>", HTML_MD_FULL, "- "), "text",
              "'>' in a class");
    ASSERT_MD(html_to_markdown("<a href=x title=it's>t</a>", HTML_MD_FULL, "- "), "[t](x)",
              "an apostrophe inside an unquoted value is not a quote");
    ASSERT_MD(html_to_markdown("<a href=\"x>t</a> more", HTML_MD_FULL, "- "), "[t](x) more",
              "an unterminated quote falls back to the first '>'");
    ASSERT_MD(html_to_markdown("a<3 and 1<2", HTML_MD_PLAIN, "- "), "a<3 and 1<2",
              "'<' before a digit is text");
}

// NP-049: Markdown output is escaped.
TEST_CASE(markdown_escaping) {
    ASSERT_MD(html_to_markdown("<code>a`b</code>", HTML_MD_FULL, "- "), "``a`b``",
              "backtick inside code: longer delimiter");
    ASSERT_MD(html_to_markdown("<code>`x`</code>", HTML_MD_FULL, "- "), "`` `x` ``",
              "code starting/ending with a backtick is padded");
    ASSERT_MD(html_to_markdown("<pre>a\n```\nb</pre>", HTML_MD_FULL, "- "), "````\na\n```\nb\n````",
              "``` inside <pre>: longer fence");
    ASSERT_MD(html_to_markdown("<pre>\nx</pre>", HTML_MD_FULL, "- "), "```\nx\n```",
              "the newline right after <pre> is not content");
    ASSERT_MD(html_to_markdown("<a href=\"u\">the [docs]</a>", HTML_MD_FULL, "- "),
              "[the \\[docs\\]](u)", "brackets in link text");
    ASSERT_MD(html_to_markdown("<img src=\"a.png\" alt=\"v[2]\">", HTML_MD_FULL, "- "),
              "![v\\[2\\]](a.png)", "brackets in alt text");
    ASSERT_MD(
        html_to_markdown("<a href=\"https://x.org/my page (1).html\">t</a>", HTML_MD_FULL, "- "),
        "[t](<https://x.org/my page (1).html>)", "a URL with spaces or parens is wrapped");
    ASSERT_MD(html_to_markdown("<p>*not emphasis* and 2 * 3</p>", HTML_MD_FULL, "- "),
              "\\*not emphasis\\* and 2 * 3", "asterisks");
    ASSERT_MD(html_to_markdown("<p>snake_case _x_</p>", HTML_MD_FULL, "- "), "snake_case \\_x\\_",
              "underscores: intraword left alone");
    ASSERT_MD(html_to_markdown("<p>1. one</p><p>- two</p><p># three</p><p>&gt; four</p>",
                               HTML_MD_FULL, "- "),
              "1\\. one\n\n\\- two\n\n\\# three\n\n\\> four", "block syntax at line start");
    ASSERT_MD(html_to_markdown("<p>2.0 is -5 and #tag</p>", HTML_MD_FULL, "- "),
              "2.0 is -5 and #tag", "look-alikes that are not syntax stay unescaped");
    ASSERT_MD(html_to_markdown("<p>C:\\path a\\*b ~x~</p>", HTML_MD_FULL, "- "),
              "C:\\path a\\\\\\*b \\~x\\~", "backslash only before punctuation; tildes");
    ASSERT_MD(html_to_markdown("<p>&amp;copy; AT&amp;T</p>", HTML_MD_FULL, "- "), "\\&copy; AT&T",
              "an entity-shaped '&' is escaped");
    ASSERT_MD(html_to_markdown("<p>a</p><hr><p>b</p>a<hr>", HTML_MD_FULL, "- "),
              "a\n\n---\n\nb\n\na\n\n---", "a rule never sits under a text line (setext)");
    ASSERT_MD(html_to_markdown("<p>*x* [y]</p>", HTML_MD_LISTS, "- "), "",
              "Lists mode without a list still falls back");
    ASSERT_MD(html_to_markdown("<ul><li>*x*</li></ul>", HTML_MD_LISTS, "- "), "- *x*",
              "Lists mode output is plain text, not escaped");
}

// NP-050: CR/LF/TAB never reach a URL or alt text.
TEST_CASE(attribute_line_breaks) {
    ASSERT_MD(html_to_markdown("<a href=\"http://x.com/a\r\nb\tc\">t</a>", HTML_MD_FULL, "- "),
              "[t](http://x.com/abc)", "CR/LF/TAB dropped from href");
    ASSERT_MD(
        html_to_markdown("<img src=\" a\n.png \" alt=\"line\r\none\ttwo\">", HTML_MD_FULL, "- "),
        "![line one two](a.png)", "src cleaned, alt collapsed to spaces");
    ASSERT_MD(html_to_markdown("<a href=\"\r\n\">t</a>", HTML_MD_FULL, "- "), "t",
              "an href that is only a line break is no link");
    ASSERT_MD(html_to_markdown("<img alt=\"a\nb\">", HTML_MD_FULL, "- "), "a b",
              "alt-only image text collapsed");
}

// NP-143: payloads modelled on what real sources put on the clipboard (the
// markup patterns of each app's HTML export; not live captures).

// A CF_HTML blob the way Windows apps build it: header with byte offsets,
// then the document with the fragment between the comment markers.
static char *cf_wrap(const char *before, const char *frag, const char *after) {
    const char *hdr = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\n"
                      "StartFragment:%010d\r\nEndFragment:%010d\r\n";
    int hl = snprintf(NULL, 0, hdr, 0, 0, 0, 0);
    int sh = hl;
    int sf = sh + (int) strlen(before) + (int) strlen("<!--StartFragment-->");
    int ef = sf + (int) strlen(frag);
    int eh = ef + (int) strlen("<!--EndFragment-->") + (int) strlen(after);
    size_t n = (size_t) eh + 1;
    char *out = malloc(n);
    if (!out)
        return NULL;
    int k = snprintf(out, n, hdr, sh, eh, sf, ef);
    snprintf(out + k, n - (size_t) k, "%s<!--StartFragment-->%s<!--EndFragment-->%s", before, frag,
             after);
    return out;
}

// Extract the fragment from a CF_HTML blob and convert it.
static char *cf_convert(const char *before, const char *frag, const char *after, HtmlMdMode mode) {
    char *cf = cf_wrap(before, frag, after);
    char *f = cf ? html_cf_extract_fragment(cf) : NULL;
    char *md = f ? html_to_markdown(f, mode, "- ") : NULL;
    free(cf);
    free(f);
    return md;
}

#define GD_SPAN                                                                                    \
    "font-size:11pt;font-family:Arial,sans-serif;color:#000000;background-color:"                  \
    "transparent;font-variant:normal;text-decoration:none;vertical-align:baseline;"                \
    "white-space:pre;white-space:pre-wrap;"
#define GD_P "<p dir=\"ltr\" style=\"line-height:1.38;margin-top:0pt;margin-bottom:0pt;\">"
#define GD_LI_P                                                                                    \
    "<p dir=\"ltr\" style=\"line-height:1.38;margin-top:0pt;margin-bottom:0pt;\" "                 \
    "role=\"presentation\">"

TEST_CASE(fixture_google_docs) {
    const char *frag =
        "<meta charset=\"utf-8\"><b style=\"font-weight:normal;\" "
        "id=\"docs-internal-guid-5c1e2f3a-7fff-1a2b-3c4d-5e6f7a8b9c0d\">"
        "<h1 dir=\"ltr\" style=\"line-height:1.38;margin-top:20pt;margin-bottom:6pt;\">"
        "<span style=\"" GD_SPAN
        "font-weight:400;font-style:normal;\">Meeting notes</span></h1>" GD_P
        "<span style=\"" GD_SPAN "font-weight:400;font-style:normal;\">Para one has </span>"
        "<span style=\"" GD_SPAN "font-weight:700;font-style:normal;\">bold</span>"
        "<span style=\"" GD_SPAN "font-weight:400;font-style:normal;\">, </span>"
        "<span style=\"" GD_SPAN "font-weight:400;font-style:italic;\">italic</span>"
        "<span style=\"" GD_SPAN "font-weight:400;font-style:normal;\"> and </span>"
        "<a href=\"https://example.com/a?b=1&amp;c=2\" style=\"text-decoration:none;\">"
        "<span style=\"" GD_SPAN "font-weight:400;color:#1155cc;\">a link</span></a>"
        "<span style=\"" GD_SPAN "font-weight:400;\">.</span></p><br>"
        "<ul style=\"margin-top:0;margin-bottom:0;padding-inline-start:48px;\">"
        "<li dir=\"ltr\" style=\"list-style-type:disc;font-size:11pt;\" aria-level=\"1\">" GD_LI_P
        "<span style=\"" GD_SPAN "font-weight:400;\">First</span></p></li>"
        "<ul style=\"margin-top:0;margin-bottom:0;padding-inline-start:48px;\">"
        "<li dir=\"ltr\" style=\"list-style-type:circle;font-size:11pt;\" aria-level=\"2\">" GD_LI_P
        "<span style=\"" GD_SPAN "font-weight:400;\">Nested</span></p></li></ul>"
        "<li dir=\"ltr\" style=\"list-style-type:disc;font-size:11pt;\" aria-level=\"1\">" GD_LI_P
        "<span style=\"" GD_SPAN "font-weight:400;\">Second</span></p></li></ul><br>" GD_P
        "<span style=\"" GD_SPAN "font-weight:400;\">Para two</span></p></b>"
        "<br class=\"Apple-interchange-newline\">";
    const char *before = "<html><body>\r\n";
    const char *after = "\r\n</body>\r\n</html>";
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_FULL),
              "# Meeting notes\n\nPara one has **bold**, *italic* and "
              "[a link](https://example.com/a?b=1&c=2).\n\n- First\n  - Nested\n- Second\n\n"
              "Para two",
              "Google Docs -> Markdown");
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_LISTS),
              "Meeting notes\nPara one has bold, italic and a link.\n\n- First\n  - Nested\n"
              "- Second\n\nPara two",
              "Google Docs -> Lists");
}

TEST_CASE(fixture_libreoffice) {
    const char *before =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\r\n<html><head>"
        "<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\"/><title></title>"
        "<meta name=\"generator\" content=\"LibreOffice 7.6.4.1 (Windows)\"/>"
        "<style type=\"text/css\">@page { size: 21cm 29.7cm; margin: 2cm } p { line-height: "
        "115%; margin-bottom: 0.25cm; background: transparent }</style></head>"
        "<body lang=\"en-AU\" link=\"#000080\" vlink=\"#800000\" dir=\"ltr\">";
    const char *frag =
        "<p style=\"line-height: 100%; margin-bottom: 0cm\">Intro with <b>bold</b> and "
        "<i>italic</i>.</p>\r\n<ul>\r\n\t<li><p style=\"line-height: 100%; margin-bottom: "
        "0cm\">Alpha</p>\r\n\t<li><p style=\"line-height: 100%; margin-bottom: 0cm\">Beta</p>\r\n"
        "\t<ul>\r\n\t\t<li><p style=\"line-height: 100%; margin-bottom: 0cm\">Beta one</p>\r\n"
        "\t</ul>\r\n</ul>\r\n<ol>\r\n\t<li><p style=\"line-height: 100%; margin-bottom: 0cm\">"
        "Step</p>\r\n\t<li><p style=\"line-height: 100%; margin-bottom: 0cm\">Next</p>\r\n</ol>";
    const char *after = "</body></html>";
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_FULL),
              "Intro with **bold** and *italic*.\n\n- Alpha\n- Beta\n  - Beta one\n\n1. Step\n"
              "2. Next",
              "LibreOffice Writer -> Markdown");
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_LISTS),
              "Intro with bold and italic.\n- Alpha\n- Beta\n  - Beta one\n- Step\n- Next",
              "LibreOffice Writer -> Lists");
}

#define WORD_BULLET(glyph)                                                                         \
    "<![if !supportLists]><span style='font-family:Symbol;mso-fareast-font-family:Symbol;"         \
    "mso-bidi-font-family:Symbol'><span style='mso-list:Ignore'>" glyph                            \
    "<span style='font:7.0pt \"Times New Roman\"'>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; </span>"    \
    "</span></span><![endif]>"

TEST_CASE(fixture_word) {
    const char *before =
        "<html xmlns:v=\"urn:schemas-microsoft-com:vml\" "
        "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
        "xmlns:w=\"urn:schemas-microsoft-com:office:word\" "
        "xmlns=\"http://www.w3.org/TR/REC-html40\"><head><meta http-equiv=Content-Type "
        "content=\"text/html; charset=utf-8\"><meta name=ProgId content=Word.Document>"
        "<meta name=Generator content=\"Microsoft Word 15\"><style><!--\r\n p.MsoNormal\r\n"
        "\t{margin:0cm;font-size:11.0pt;}\r\n--></style></head><body lang=EN-AU "
        "style='tab-interval:36.0pt;word-wrap:break-word'>\r\n";
    const char *frag =
        "\r\n\r\n<p class=MsoNormal>Plain <b>bold</b> <i>italic</i><o:p></o:p></p>\r\n\r\n"
        "<p class=MsoListParagraphCxSpFirst style='text-indent:-18.0pt;mso-list:l0 level1 "
        "lfo1'>" WORD_BULLET(
            "\xC2\xB7") "First<o:p></o:p></p>\r\n\r\n"
                        "<p class=MsoListParagraphCxSpMiddle "
                        "style='margin-left:72.0pt;mso-add-space:auto;"
                        "text-indent:-18.0pt;mso-list:l0 level2 lfo1'>" WORD_BULLET(
                            "o") "Nested<o:p></o:p></p>\r\n"
                                 "\r\n<p class=MsoListParagraphCxSpLast "
                                 "style='text-indent:-18.0pt;mso-list:l0 level1 "
                                 "lfo1'>" WORD_BULLET(
                                     "\xC2\xB7") "Second<o:p></o:p></p>\r\n\r\n"
                                                 "<p class=MsoListParagraphCxSpFirst "
                                                 "style='text-indent:-18.0pt;mso-list:l1 level1 "
                                                 "lfo2'>" WORD_BULLET(
                                                     "1.") "Step one<o:p></o:p></p>\r\n\r\n"
                                                           "<p class=MsoListParagraphCxSpLast "
                                                           "style='text-indent:-18.0pt;mso-list:l1 "
                                                           "level1 "
                                                           "lfo2'>" WORD_BULLET(
                                                               "2.") "Step "
                                                                     "two<o:p></o:p></p>\r\n\r\n"
                                                                     "<p "
                                                                     "class=MsoNormal>After<o:p></"
                                                                     "o:p></p>\r\n\r\n";
    const char *after = "</body>\r\n\r\n</html>";
    ASSERT_MD(
        cf_convert(before, frag, after, HTML_MD_FULL),
        "Plain **bold** *italic*\n\n- First\n  - Nested\n- Second\n1. Step one\n2. Step two\n\n"
        "After",
        "Word -> Markdown (mso-list paragraphs become a list)");
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_LISTS),
              "Plain bold italic\n- First\n  - Nested\n- Second\n- Step one\n- Step two\nAfter",
              "Word -> Lists");
}

TEST_CASE(fixture_excel) {
    const char *before =
        "<html xmlns:v=\"urn:schemas-microsoft-com:vml\" "
        "xmlns:x=\"urn:schemas-microsoft-com:office:excel\" "
        "xmlns=\"http://www.w3.org/TR/REC-html40\">\r\n<head>\r\n<meta name=ProgId "
        "content=Excel.Sheet>\r\n<style>\r\n<!--table\r\n\t{mso-displayed-decimal-separator:\"\\."
        "\";}"
        "\r\n-->\r\n</style>\r\n</head>\r\n<body link=\"#0563C1\" vlink=\"#954F72\">\r\n\r\n"
        "<table border=0 cellpadding=0 cellspacing=0 width=128 style='border-collapse:\r\n"
        " collapse;width:96pt'>\r\n<col width=64 span=2 style='width:48pt'>\r\n";
    // Excel's fragment is bare rows: the <table> tag sits outside it
    const char *frag = "\r\n <tr height=20 style='height:15.0pt'>\r\n  <td height=20 width=64 "
                       "style='height:15.0pt;width:48pt'>Name</td>\r\n  <td width=64 "
                       "style='width:48pt'>Qty</td>\r\n </tr>\r\n <tr height=20 "
                       "style='height:15.0pt'>\r\n  <td height=20 style='height:15.0pt'>Apple</td>"
                       "\r\n  <td align=right>3</td>\r\n </tr>\r\n <tr height=20 "
                       "style='height:15.0pt'>\r\n  <td height=20 style='height:15.0pt'>Pipe|Co"
                       "</td>\r\n  <td align=right>12</td>\r\n </tr>\r\n";
    const char *after = "\r\n</table>\r\n\r\n</body>\r\n\r\n</html>\r\n";
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_FULL),
              "| Name | Qty |\n| --- | --- |\n| Apple | 3 |\n| Pipe\\|Co | 12 |",
              "Excel -> Markdown table");
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_LISTS), "",
              "Excel -> Lists falls back to Excel's own tab-separated text");
    ASSERT_MD(cf_convert(before, frag, after, HTML_MD_PLAIN), "Name\tQty\nApple\t3\nPipe|Co\t12",
              "Excel -> plain");
}

TEST_CASE(fixture_vscode) {
    const char *frag =
        "<meta charset='utf-8'><div style=\"color: #cccccc;background-color: #1f1f1f;"
        "font-family: Consolas, 'Courier New', monospace;font-weight: normal;font-size: 14px;"
        "line-height: 19px;white-space: pre;\"><div><span style=\"color: #569cd6;\">def</span>"
        "<span style=\"color: #cccccc;\"> </span><span style=\"color: #dcdcaa;\">f</span>"
        "<span style=\"color: #cccccc;\">(</span><span style=\"color: #9cdcfe;\">x</span>"
        "<span style=\"color: #cccccc;\">):</span></div><div><span style=\"color: #cccccc;\">"
        "&nbsp; &nbsp; </span><span style=\"color: #c586c0;\">return</span>"
        "<span style=\"color: #cccccc;\"> x </span><span style=\"color: #d4d4d4;\">*</span>"
        "<span style=\"color: #cccccc;\"> </span><span style=\"color: #b5cea8;\">2</span></div>"
        "<br><div><span style=\"color: #6a9955;\"># done `x`</span></div></div>";
    ASSERT_MD(cf_convert("<html><body>", frag, "</body></html>", HTML_MD_FULL),
              "```\ndef f(x):\n    return x * 2\n\n# done `x`\n```", "VS Code -> fenced code");
    ASSERT_MD(cf_convert("<html><body>", frag, "</body></html>", HTML_MD_LISTS), "",
              "VS Code -> Lists falls back to the plain text (indentation intact)");
}

TEST_CASE(fixture_windows_terminal) {
    const char *frag = "<DIV style=\"display:inline-block;white-space:pre;background-color:#0C0C0C;"
                       "font-family:'Cascadia Mono',monospace;font-size:12pt;padding:4px;\">"
                       "<SPAN style=\"color:#CCCCCC;background-color:#0C0C0C;\">PS C:\\&gt; dir"
                       "</SPAN><BR><SPAN style=\"color:#CCCCCC;background-color:#0C0C0C;\">Mode"
                       "    Name *</SPAN><BR></DIV>";
    ASSERT_MD(cf_convert("<HTML><BODY>", frag, "</BODY></HTML>", HTML_MD_FULL),
              "```\nPS C:\\> dir\nMode    Name *\n```", "terminal -> fenced, columns kept");
}

TEST_CASE(fixture_chrome_page) {
    const char *frag =
        "<h2 style=\"box-sizing: border-box; margin-top: 24px; color: rgb(31, 35, 40); "
        "font-family: -apple-system, BlinkMacSystemFont, &quot;Segoe UI&quot;, sans-serif;\">"
        "Install</h2><p style=\"box-sizing: border-box; margin-bottom: 16px;\">Run <code "
        "style=\"padding: 0.2em 0.4em; font-size: 85%;\">npm i `x`</code> then see <a "
        "href=\"https://example.com/my page (1).html\" style=\"color: rgb(9, 105, 218);\">the "
        "[docs]</a>.</p><pre style=\"padding: 16px; overflow: auto;\">\nfirst line\n``` not a "
        "fence\n</pre><p><img src=\"https://example.com/a.png\" alt=\"A\ndiagram [v2]\" "
        "style=\"max-width: 100%;\"></p>";
    ASSERT_MD(cf_convert("<html><body>\r\n", frag, "\r\n</body>\r\n</html>", HTML_MD_FULL),
              "## Install\n\nRun `` npm i `x` `` then see "
              "[the \\[docs\\]](<https://example.com/my page (1).html>).\n\n````\nfirst line\n"
              "``` not a fence\n````\n\n![A diagram \\[v2\\]](https://example.com/a.png)",
              "Chrome page -> Markdown");
}

TEST_CASE(fixture_firefox_page) {
    const char *frag = "<blockquote><p>Quoted <em>text</em></p></blockquote>"
                       "<ol start=\"3\"><li>Third<ul><li>child</li></ul></li><li>Fourth</li></ol>"
                       "<p>2024. A year</p><p># not a heading, * not a bullet</p>";
    ASSERT_MD(cf_convert("<html><body>\n", frag, "\n</body>\n</html>", HTML_MD_FULL),
              "> Quoted *text*\n\n3. Third\n   - child\n4. Fourth\n\n2024\\. A year\n\n"
              "\\# not a heading, * not a bullet",
              "Firefox page -> Markdown");
    ASSERT_MD(cf_convert("<html><body>\n", frag, "\n</body>\n</html>", HTML_MD_LISTS),
              "Quoted text\n- Third\n  - child\n- Fourth\n2024. A year\n"
              "# not a heading, * not a bullet",
              "Firefox page -> Lists");
}

// NP-143: large pastes stay linear. A bound of seconds against work that
// takes milliseconds: a quadratic path would blow far past it.
TEST_CASE(scaling_large_paste) {
    const char *unit = "<li><b>item</b> <span style=\"font-weight:700\">x</span> "
                       "<a href=\"u\">l</a> <code>c`d</code></li>";
    size_t ul = strlen(unit), reps = 20000;
    char *in = malloc(ul * reps + 16);
    TEST_ASSERT_NOT_NULL(in, "allocate the large input");
    size_t p = 0;
    memcpy(in + p, "<ul>", 4);
    p += 4;
    for (size_t i = 0; i < reps; i++) {
        memcpy(in + p, unit, ul);
        p += ul;
    }
    memcpy(in + p, "</ul>", 6);
    clock_t t0 = clock();
    char *full = html_to_markdown(in, HTML_MD_FULL, "- ");
    char *lists = html_to_markdown(in, HTML_MD_LISTS, "- ");
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    free(in);
    TEST_ASSERT_NOT_NULL(full, "large Markdown conversion");
    TEST_ASSERT_NOT_NULL(lists, "large Lists conversion");
    if (full && lists) {
        const char *ef = "- **item** **x** [l](u) ``c`d``\n- **item**";
        const char *el = "- item x l c`d\n- item";
        TEST_ASSERT(strncmp(full, ef, strlen(ef)) == 0, "each item converts");
        TEST_ASSERT(strncmp(lists, el, strlen(el)) == 0, "each item converts (Lists)");
    }
    free(full);
    free(lists);
    TEST_ASSERT(secs < 10.0, "a 2 MB paste converts in well under ten seconds");

    // Deeply nested inline elements past the tracked-stack bound
    size_t depth = 5000;
    char *deep = malloc(depth * 40 + 16);
    TEST_ASSERT_NOT_NULL(deep, "allocate the deep input");
    p = 0;
    for (size_t i = 0; i < depth; i++)
        p += (size_t) sprintf(deep + p, "<span style=\"font-weight:700\">");
    deep[p++] = 'x';
    for (size_t i = 0; i < depth; i++)
        p += (size_t) sprintf(deep + p, "</span>");
    deep[p] = '\0';
    ASSERT_MD(html_to_markdown(deep, HTML_MD_FULL, "- "), "**x**", "5000 nested bold spans");
    free(deep);
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

    RUN_TEST(lists_mode_needs_a_list);
    RUN_TEST(block_elements_break_lines);
    RUN_TEST(tables_markdown_and_plain);
    RUN_TEST(white_space_pre_block);
    RUN_TEST(li_block_children);
    RUN_TEST(google_docs_wrapper_bold);
    RUN_TEST(emphasis_written_lazily);
    RUN_TEST(ordered_nesting_and_start);
    RUN_TEST(quote_aware_tags);
    RUN_TEST(markdown_escaping);
    RUN_TEST(attribute_line_breaks);
    RUN_TEST(fixture_google_docs);
    RUN_TEST(fixture_libreoffice);
    RUN_TEST(fixture_word);
    RUN_TEST(fixture_excel);
    RUN_TEST(fixture_vscode);
    RUN_TEST(fixture_windows_terminal);
    RUN_TEST(fixture_chrome_page);
    RUN_TEST(fixture_firefox_page);
    RUN_TEST(scaling_large_paste);

    TEST_SUMMARY();
    return 0;
}
