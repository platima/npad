/*
 * npad - List operations
 * Line-oriented text transforms for the optional list tools: sort, unique,
 * delimiter conversion, and indent/unindent. All operate on UTF-8 text split
 * on '\n' (the platform layer normalizes CRLF/CR to '\n' on the way in and
 * converts back to the document's line ending on the way out).
 *
 * Every function returns a malloc'd string the caller frees, and preserves a
 * trailing newline if the input had one. NULL input yields NULL.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef LIST_OPS_H
#define LIST_OPS_H

#include <stdbool.h>

// Indent marker/whitespace styles. Order matches the Preferences dropdown and
// the ID_LIST_INDENT_BASE command range.
typedef enum {
    LIST_INDENT_SPACES = 0,       // "    " (four spaces)
    LIST_INDENT_TAB = 1,          // "\t"
    LIST_INDENT_ASTERISK = 2,     // "* "
    LIST_INDENT_HYPHEN = 3,       // "- "
    LIST_INDENT_ASTERISK_LSP = 4, // " * " (leading space)
    LIST_INDENT_HYPHEN_LSP = 5,   // " - " (leading space)
    LIST_INDENT_CUSTOM = 6        // Caller-supplied prefix string
} ListIndentFormat;

// Sort the lines. Stable; ascending unless descending. When not case_sensitive
// the comparison is ASCII-case-insensitive (A-Z folded to a-z); other bytes
// compare by value.
char *list_sort_lines(const char *text, bool descending, bool case_sensitive);

// Remove duplicate lines, keeping the first occurrence (order-preserving).
char *list_unique_lines(const char *text);

// Interpret backslash escapes: \n \r \t \\ \0 and \uXXXX (4 hex digits). An
// unrecognized escape yields the character following the backslash; a trailing
// backslash is kept literally.
char *list_unescape(const char *s);

// Replace every literal occurrence of from with to. from must be non-empty
// (returns a copy of text otherwise).
char *list_replace_all(const char *text, const char *from, const char *to);

// Indent / unindent each line per the format. See list_ops.c for the exact
// model (whitespace formats prepend the prefix; marker formats add the marker
// once then deepen with two spaces, markdown-style; unindent removes one
// unit). Marker detection is style-agnostic: a line carrying ANY bullet
// ("* ", "- ", or the custom body) deepens on indent and strips on unindent,
// whatever format is configured - mixed lists never stack a second marker.
// custom_prefix is the already-unescaped custom prefix (NULL is fine); it is
// the inserted prefix for LIST_INDENT_CUSTOM and joins marker detection for
// every marker format. A whitespace-only custom prefix stacks/strips
// literally; CUSTOM with an empty/NULL prefix is a no-op copy.
char *list_indent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix);
char *list_unindent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix);

#endif // LIST_OPS_H
