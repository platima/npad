/*
 * npad - HTML to Markdown/text conversion (rich-text paste)
 *
 * Platform-independent, self-contained (no windows.h). Converts a UTF-8 HTML
 * fragment - such as the one carried by the Windows "HTML Format" clipboard
 * type - into UTF-8 markdown or plain text, and extracts that fragment from a
 * raw CF_HTML clipboard blob. Every returned string is malloc'd; the caller
 * frees. NULL input yields NULL.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef HTML_MD_H
#define HTML_MD_H

#include <stdbool.h>

// Conversion depth.
typedef enum {
    // Strip every tag: entities decoded, block elements become line breaks,
    // all inline formatting discarded. (Rarely used directly - a "plain"
    // paste just inserts the clipboard's CF_UNICODETEXT.)
    HTML_MD_PLAIN = 0,
    // Like PLAIN, but <ul>/<ol>/<li> (and Word's mso-list paragraphs) become
    // list lines using list_marker with two-space nesting per level. All
    // other formatting is stripped. A fragment with no list item at all
    // converts to an empty string: the caller then pastes the clipboard's
    // own plain text, which keeps code indentation and table columns.
    HTML_MD_LISTS = 1,
    // Full markdown: headings (#), bold (**), italic (*), inline code (`),
    // links [text](url), images ![alt](src), fenced code blocks (``` - also
    // for a <div> styled white-space:pre, as code editors and terminals
    // copy), blockquotes (>), horizontal rules (---), GitHub-style pipe
    // tables, and both list kinds (unordered use list_marker; ordered are
    // numbered from the list's start=, children indented to the parent's
    // text). Text that would read as Markdown syntax is backslash-escaped.
    HTML_MD_FULL = 2
} HtmlMdMode;

// Convert a UTF-8 HTML fragment to UTF-8 text/markdown per mode.
//   html_utf8   : the HTML fragment (borrowed; not freed here). NULL -> NULL.
//   mode        : HTML_MD_PLAIN | HTML_MD_LISTS | HTML_MD_FULL.
//   list_marker : bullet prefix for unordered / lists-mode items, e.g. "- ".
//                 NULL defaults to "- ". Line endings in the result are '\n'.
// Returns a malloc'd UTF-8 string (caller frees), or NULL on NULL input / OOM.
// An empty result means there was nothing to convert (see HTML_MD_LISTS): a
// paste should then fall back to the plain text.
char *html_to_markdown(const char *html_utf8, HtmlMdMode mode, const char *list_marker);

// Extract the copied fragment from a raw CF_HTML clipboard blob (the UTF-8
// payload of the Windows "HTML Format" clipboard type). Honours the
// StartFragment/EndFragment byte offsets in the header, falling back to the
// <!--StartFragment-->/<!--EndFragment--> comment markers, then to the whole
// input. Returns a malloc'd UTF-8 string (caller frees), or NULL on NULL
// input / OOM.
char *html_cf_extract_fragment(const char *cf_html);

#endif // HTML_MD_H
