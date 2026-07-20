/*
 * npad - List operations implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "list_ops.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// --- Small helpers ---------------------------------------------------------

// Encode a Unicode code point as UTF-8 into out (up to 4 bytes); returns the
// byte count. Local copy so this module stays independent of file_ops.
static size_t lo_utf8_encode(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char) cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char) (0xF0 | (cp >> 18));
    out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char) (0x80 | (cp & 0x3F));
    return 4;
}

// A line span within the original text (no NUL involved; length-delimited).
typedef struct {
    const char *start;
    size_t len;
} Line;

// Split text into '\n'-delimited lines. If the text ends with '\n' that final
// terminator is recorded in *trailing_newline and NOT emitted as an empty
// trailing line, so joins can restore it. Returns a malloc'd array (caller
// frees) and sets *count; NULL on allocation failure.
static Line *split_lines(const char *text, size_t *count, bool *trailing_newline) {
    size_t len = strlen(text);
    *trailing_newline = (len > 0 && text[len - 1] == '\n');

    size_t cap = 8, n = 0;
    Line *lines = malloc(cap * sizeof(Line));
    if (!lines)
        return NULL;

    const char *p = text;
    const char *end = text + len;
    while (p <= end) {
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        size_t line_len = nl ? (size_t) (nl - p) : (size_t) (end - p);
        if (n == cap) {
            size_t new_cap = cap * 2;
            Line *grown = realloc(lines, new_cap * sizeof(Line));
            if (!grown) {
                free(lines);
                return NULL;
            }
            lines = grown;
            cap = new_cap;
        }
        lines[n].start = p;
        lines[n].len = line_len;
        n++;
        if (!nl)
            break;
        p = nl + 1;
        // A trailing '\n' terminates the last real line; don't emit an empty
        // line after it (it's restored via *trailing_newline on join).
        if (p == end)
            break;
    }

    *count = n;
    return lines;
}

// Join lines with '\n', appending a trailing '\n' when requested. Returns a
// malloc'd string; NULL on failure.
static char *join_lines(Line *lines, size_t count, bool trailing_newline) {
    size_t total = 0;
    for (size_t i = 0; i < count; i++)
        total += lines[i].len + 1; // + separator
    total += 2;                    // trailing newline + NUL headroom

    char *out = malloc(total);
    if (!out)
        return NULL;

    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(out + pos, lines[i].start, lines[i].len);
        pos += lines[i].len;
        if (i + 1 < count)
            out[pos++] = '\n';
    }
    if (trailing_newline)
        out[pos++] = '\n';
    out[pos] = '\0';
    return out;
}

static char lo_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static int line_compare(const Line *a, const Line *b, bool case_sensitive) {
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char) a->start[i];
        unsigned char cb = (unsigned char) b->start[i];
        if (!case_sensitive) {
            ca = (unsigned char) lo_lower((char) ca);
            cb = (unsigned char) lo_lower((char) cb);
        }
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
    }
    if (a->len == b->len)
        return 0;
    return (a->len < b->len) ? -1 : 1;
}

// --- Sort ------------------------------------------------------------------

// Stable insertion sort keyed on (compare, original index) so equal lines keep
// their input order. Line counts here are modest (interactive selections).
char *list_sort_lines(const char *text, bool descending, bool case_sensitive) {
    if (!text)
        return NULL;

    size_t count;
    bool trailing;
    Line *lines = split_lines(text, &count, &trailing);
    if (!lines)
        return NULL;

    for (size_t i = 1; i < count; i++) {
        Line key = lines[i];
        size_t j = i;
        while (j > 0) {
            int cmp = line_compare(&lines[j - 1], &key, case_sensitive);
            if (descending)
                cmp = -cmp;
            if (cmp <= 0) // <= keeps equal elements stable
                break;
            lines[j] = lines[j - 1];
            j--;
        }
        lines[j] = key;
    }

    char *out = join_lines(lines, count, trailing);
    free(lines);
    return out;
}

// --- Unique ----------------------------------------------------------------

char *list_unique_lines(const char *text) {
    if (!text)
        return NULL;

    size_t count;
    bool trailing;
    Line *lines = split_lines(text, &count, &trailing);
    if (!lines)
        return NULL;

    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        bool dup = false;
        for (size_t j = 0; j < kept; j++) {
            if (lines[j].len == lines[i].len &&
                memcmp(lines[j].start, lines[i].start, lines[i].len) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup)
            lines[kept++] = lines[i];
    }

    char *out = join_lines(lines, kept, trailing);
    free(lines);
    return out;
}

// --- Unescape --------------------------------------------------------------

static int hex_val(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

char *list_unescape(const char *s) {
    if (!s)
        return NULL;

    size_t len = strlen(s);
    char *out = malloc(len + 1); // Escapes only shrink or keep length
    if (!out)
        return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len;) {
        if (s[i] != '\\') {
            out[o++] = s[i++];
            continue;
        }
        // Backslash
        if (i + 1 >= len) {
            out[o++] = '\\'; // Trailing backslash kept literally
            break;
        }
        char e = s[i + 1];
        switch (e) {
            case 'n':
                out[o++] = '\n';
                i += 2;
                break;
            case 'r':
                out[o++] = '\r';
                i += 2;
                break;
            case 't':
                out[o++] = '\t';
                i += 2;
                break;
            case '0':
                out[o++] = '\0';
                i += 2;
                break;
            case '\\':
                out[o++] = '\\';
                i += 2;
                break;
            case 'u': {
                if (i + 5 < len) {
                    int h0 = hex_val(s[i + 2]), h1 = hex_val(s[i + 3]);
                    int h2 = hex_val(s[i + 4]), h3 = hex_val(s[i + 5]);
                    if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                        uint32_t cp = (uint32_t) ((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                        o += lo_utf8_encode(cp, out + o);
                        i += 6;
                        break;
                    }
                }
                out[o++] = e; // Malformed \u: emit the 'u' literally
                i += 2;
                break;
            }
            default:
                out[o++] = e; // Unknown escape: the following char, literally
                i += 2;
                break;
        }
    }
    out[o] = '\0';
    return out;
}

// --- Replace all -----------------------------------------------------------

char *list_replace_all(const char *text, const char *from, const char *to) {
    if (!text)
        return NULL;
    if (!from || from[0] == '\0') {
        char *copy = malloc(strlen(text) + 1);
        if (copy)
            strcpy(copy, text);
        return copy;
    }
    if (!to)
        to = "";

    size_t text_len = strlen(text);
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);

    // Count occurrences to size the buffer exactly
    size_t occurrences = 0;
    for (const char *p = text; (p = strstr(p, from)) != NULL; p += from_len)
        occurrences++;

    size_t out_len = text_len + occurrences * (to_len >= from_len ? to_len - from_len : 0) -
                     occurrences * (from_len > to_len ? from_len - to_len : 0);
    char *out = malloc(out_len + 1);
    if (!out)
        return NULL;

    size_t o = 0;
    const char *p = text;
    while (*p) {
        const char *hit = strstr(p, from);
        if (!hit) {
            size_t rest = strlen(p);
            memcpy(out + o, p, rest);
            o += rest;
            break;
        }
        size_t before = (size_t) (hit - p);
        memcpy(out + o, p, before);
        o += before;
        memcpy(out + o, to, to_len);
        o += to_len;
        p = hit + from_len;
    }
    out[o] = '\0';
    return out;
}

// --- Indent / unindent -----------------------------------------------------

static const char *indent_prefix(ListIndentFormat fmt) {
    switch (fmt) {
        case LIST_INDENT_SPACES:
            return "    ";
        case LIST_INDENT_TAB:
            return "\t";
        case LIST_INDENT_ASTERISK:
            return "* ";
        case LIST_INDENT_HYPHEN:
            return "- ";
        case LIST_INDENT_ASTERISK_LSP:
            return " * ";
        case LIST_INDENT_HYPHEN_LSP:
            return " - ";
        case LIST_INDENT_CUSTOM:
            break; // Caller-supplied; resolved by effective_prefix
    }
    return "    ";
}

// The prefix actually in effect: the custom string for LIST_INDENT_CUSTOM,
// otherwise the built-in table above.
static const char *effective_prefix(ListIndentFormat fmt, const char *custom) {
    if (fmt == LIST_INDENT_CUSTOM)
        return custom ? custom : "";
    return indent_prefix(fmt);
}

static bool prefix_is_whitespace(const char *p) {
    for (; *p; p++) {
        if (*p != ' ' && *p != '\t')
            return false;
    }
    return true; // Empty counts as whitespace-only
}

// Marker formats get markdown-style nesting; whitespace formats (and
// whitespace-only custom prefixes) stack literally.
static bool is_marker_format(ListIndentFormat fmt, const char *custom) {
    if (fmt == LIST_INDENT_SPACES || fmt == LIST_INDENT_TAB)
        return false;
    if (fmt == LIST_INDENT_CUSTOM)
        return custom && *custom && !prefix_is_whitespace(custom);
    return true;
}

#define NEST "  " // Two spaces: markdown nesting step for an already-marked line

// Length of the bullet marker at s (leading whitespace already skipped):
// the custom marker body if it matches, else the built-in "* " / "- " -- or
// their space-less forms when the line ends right after them (empty bullets,
// pre-0.13 documents). ANY marker style counts regardless of the configured
// format, so mixed lists still deepen/unindent instead of stacking a second
// marker. "*emphasis*" is NOT a bullet. 0 when s does not start a bullet.
static size_t marker_at(const char *s, size_t len, const char *custom) {
    if (custom) {
        const char *body = custom;
        while (*body == ' ' || *body == '\t')
            body++;
        size_t bl = strlen(body);
        if (bl > 0) {
            if (len >= bl && memcmp(s, body, bl) == 0)
                return bl;
            size_t tl = bl;
            while (tl > 0 && (body[tl - 1] == ' ' || body[tl - 1] == '\t'))
                tl--;
            if (tl > 0 && len == tl && memcmp(s, body, tl) == 0)
                return tl;
        }
    }
    if (len >= 2 && (s[0] == '*' || s[0] == '-') && s[1] == ' ')
        return 2;
    if (len == 1 && (s[0] == '*' || s[0] == '-'))
        return 1;
    return 0;
}

// Does the line already carry a bullet (optionally behind nesting spaces)?
static bool line_has_bullet(const char *line, size_t len, const char *custom) {
    size_t i = 0;
    while (i < len && line[i] == ' ')
        i++;
    return marker_at(line + i, len - i, custom) > 0;
}

// Build a transformed document by applying fn(line,len,out,fmt,custom) per line.
typedef size_t (*LineXform)(const char *line, size_t len, char *out, ListIndentFormat fmt,
                            const char *custom);

static char *apply_per_line(const char *text, ListIndentFormat fmt, const char *custom,
                            LineXform fn, size_t max_grow) {
    if (!text)
        return NULL;
    size_t count;
    bool trailing;
    Line *lines = split_lines(text, &count, &trailing);
    if (!lines)
        return NULL;

    size_t total = strlen(text) + count * max_grow + 2;
    char *out = malloc(total);
    if (!out) {
        free(lines);
        return NULL;
    }

    size_t o = 0;
    for (size_t i = 0; i < count; i++) {
        o += fn(lines[i].start, lines[i].len, out + o, fmt, custom);
        if (i + 1 < count)
            out[o++] = '\n';
    }
    if (trailing)
        out[o++] = '\n';
    out[o] = '\0';
    free(lines);
    return out;
}

static size_t indent_line(const char *line, size_t len, char *out, ListIndentFormat fmt,
                          const char *custom) {
    if (is_marker_format(fmt, custom) && line_has_bullet(line, len, custom)) {
        // Already a bullet (of any marker style): deepen with nesting
        // spaces, keep the single marker it has
        memcpy(out, NEST, strlen(NEST));
        memcpy(out + strlen(NEST), line, len);
        return strlen(NEST) + len;
    }
    const char *p = effective_prefix(fmt, custom);
    size_t pl = strlen(p);
    memcpy(out, p, pl);
    memcpy(out + pl, line, len);
    return pl + len;
}

static size_t unindent_line(const char *line, size_t len, char *out, ListIndentFormat fmt,
                            const char *custom) {
    if (is_marker_format(fmt, custom)) {
        // Marker-aware: act on whatever bullet the line actually carries,
        // not just the configured format's marker
        size_t ws = 0;
        while (ws < len && line[ws] == ' ')
            ws++;
        size_t m = marker_at(line + ws, len - ws, custom);
        if (m > 0) {
            if (ws >= 2) {
                // Deepened bullet: strip one nesting step (two spaces)
                memcpy(out, line + 2, len - 2);
                return len - 2;
            }
            // Base bullet: strip its lead-in space(s) and the marker
            size_t strip = ws + m;
            memcpy(out, line + strip, len - strip);
            return len - strip;
        }
        memcpy(out, line, len); // No bullet: no-op
        return len;
    }
    if (fmt == LIST_INDENT_CUSTOM) {
        // Whitespace-only custom prefix: strip one literal occurrence
        size_t pl = strlen(custom ? custom : "");
        if (pl > 0 && len >= pl && memcmp(line, custom, pl) == 0) {
            memcpy(out, line + pl, len - pl);
            return len - pl;
        }
        memcpy(out, line, len); // No-op
        return len;
    }
    if (fmt == LIST_INDENT_TAB) {
        if (len >= 1 && line[0] == '\t') {
            memcpy(out, line + 1, len - 1);
            return len - 1;
        }
    }
    // Spaces (or a tab line with leading spaces): remove up to four leading spaces
    size_t strip = 0;
    while (strip < 4 && strip < len && line[strip] == ' ')
        strip++;
    memcpy(out, line + strip, len - strip);
    return len - strip;
}

// A CUSTOM format with no prefix configured is a no-op (the UI prompts before
// it gets here); returning a copy keeps the caller's free() contract uniform.
static char *copy_text(const char *text) {
    if (!text)
        return NULL;
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    if (out)
        memcpy(out, text, len + 1);
    return out;
}

char *list_indent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix) {
    if (fmt == LIST_INDENT_CUSTOM && (!custom_prefix || !*custom_prefix))
        return copy_text(text);
    // Growth per line is the full prefix (NEST is shorter); keep the historic
    // 4-byte floor for the built-in formats
    size_t pl = strlen(effective_prefix(fmt, custom_prefix));
    return apply_per_line(text, fmt, custom_prefix, indent_line, pl > 4 ? pl : 4);
}

char *list_unindent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix) {
    if (fmt == LIST_INDENT_CUSTOM && (!custom_prefix || !*custom_prefix))
        return copy_text(text);
    return apply_per_line(text, fmt, custom_prefix, unindent_line, 0);
}
