/*
 * npad - List operations implementation
 *
 * Author: Platima
 * https://npad.dev
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

// Bottom-up merge sort of the line spans: O(n log n) whatever the input order
// (Sort runs on the whole document when nothing is selected, so a 100k-line
// log must not take minutes, and an insertion sort did on already-sorted text
// sorted the other way). Stable: a merge takes from the left run on a tie, so
// equal lines keep their input order in both directions.
char *list_sort_lines(const char *text, bool descending, bool case_sensitive) {
    if (!text)
        return NULL;

    size_t count;
    bool trailing;
    Line *lines = split_lines(text, &count, &trailing);
    if (!lines)
        return NULL;

    Line *tmp = count > 1 ? malloc(count * sizeof(Line)) : NULL;
    if (count > 1 && !tmp) {
        free(lines);
        return NULL;
    }
    Line *src = lines, *dst = tmp;
    size_t width = 1;
    while (width < count) {
        for (size_t lo = 0; lo < count; lo += 2 * width) {
            size_t mid = lo + width < count ? lo + width : count;
            size_t hi = mid + width < count ? mid + width : count;
            size_t a = lo, b = mid, o = lo;
            while (a < mid && b < hi) {
                int cmp = descending ? line_compare(&src[b], &src[a], case_sensitive)
                                     : line_compare(&src[a], &src[b], case_sensitive);
                dst[o++] = (cmp <= 0) ? src[a++] : src[b++]; // <= keeps ties stable
            }
            while (a < mid)
                dst[o++] = src[a++];
            while (b < hi)
                dst[o++] = src[b++];
        }
        Line *swap = src;
        src = dst;
        dst = swap;
        width = (width > count / 2) ? count : width * 2; // never overflows
    }

    char *out = join_lines(src, count, trailing);
    free(lines);
    free(tmp);
    return out;
}

// --- Unique ----------------------------------------------------------------

// FNV-1a over the line's bytes (64-bit arithmetic, folded into size_t).
static size_t line_hash(const Line *l) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < l->len; i++) {
        h ^= (unsigned char) l->start[i];
        h *= 1099511628211ull;
    }
    return (size_t) (h ^ (h >> 32));
}

// Order-preserving dedupe through an open-addressing hash set of the lines
// already kept: O(n) expected, where a scan of every kept line per input line
// was quadratic.
char *list_unique_lines(const char *text) {
    if (!text)
        return NULL;

    size_t count;
    bool trailing;
    Line *lines = split_lines(text, &count, &trailing);
    if (!lines)
        return NULL;

    size_t cap = 16;
    while (cap < count * 2) {
        if (cap > SIZE_MAX / 4 / sizeof(size_t)) {
            free(lines);
            return NULL;
        }
        cap *= 2;
    }
    size_t *slots = malloc(cap * sizeof(size_t)); // index into lines[], SIZE_MAX = empty
    if (!slots) {
        free(lines);
        return NULL;
    }
    for (size_t i = 0; i < cap; i++)
        slots[i] = SIZE_MAX;

    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        bool dup = false;
        size_t h = line_hash(&lines[i]) & (cap - 1);
        while (slots[h] != SIZE_MAX) {
            const Line *k = &lines[slots[h]];
            if (k->len == lines[i].len && memcmp(k->start, lines[i].start, k->len) == 0) {
                dup = true;
                break;
            }
            h = (h + 1) & (cap - 1);
        }
        if (!dup) {
            lines[kept] = lines[i];
            slots[h] = kept++;
        }
    }
    free(slots);

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

// The four hex digits of a \uXXXX escape starting at s[i] (the backslash), or
// -1 when they are not all there.
static long hex4_at(const char *s, size_t len, size_t i) {
    if (i + 5 >= len || s[i + 1] != 'u')
        return -1;
    long v = 0;
    for (size_t k = 2; k < 6; k++) {
        int h = hex_val(s[i + k]);
        if (h < 0)
            return -1;
        v = (v << 4) | h;
    }
    return v;
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
            // There is no \0: the result is a C string, so a NUL would end it
            // and silently drop the rest of the field. "\0" takes the
            // unknown-escape path below and yields a literal '0'.
            case '\\':
                out[o++] = '\\';
                i += 2;
                break;
            case 'u': {
                long v = hex4_at(s, len, i);
                if (v < 0) {
                    out[o++] = e; // Malformed \u: emit the 'u' literally
                    i += 2;
                    break;
                }
                uint32_t cp = (uint32_t) v;
                i += 6;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // A high surrogate pairs with a following \uDC00-\uDFFF
                    // into one supplementary code point (😀 is one
                    // emoji); encoding the halves separately gave CESU-8,
                    // which no UTF-8 consumer accepts
                    long lo = (i < len && s[i] == '\\') ? hex4_at(s, len, i) : -1;
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t) lo - 0xDC00);
                        i += 6;
                    } else {
                        cp = 0xFFFD; // A lone high surrogate
                    }
                } else if ((cp >= 0xDC00 && cp <= 0xDFFF) || cp == 0) {
                    // A lone low surrogate is not a character, and \u0000
                    // would end the string like \0 did: both become U+FFFD
                    cp = 0xFFFD;
                }
                o += lo_utf8_encode(cp, out + o);
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

#define MAX_SPACE_WIDTH 16
static const char SPACE_RUN[MAX_SPACE_WIDTH + 1] = "                "; // 16 spaces

// The spaces format's indent unit: space_width spaces, clamped to 1..16
// (LIST_INDENT_SPACES_DEFAULT_WIDTH is the historic four).
static int clamp_space_width(int w) {
    return w < 1 ? 1 : (w > MAX_SPACE_WIDTH ? MAX_SPACE_WIDTH : w);
}

static const char *indent_prefix(ListIndentFormat fmt, int space_width) {
    switch (fmt) {
        case LIST_INDENT_SPACES:
            return SPACE_RUN + (MAX_SPACE_WIDTH - clamp_space_width(space_width));
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
static const char *effective_prefix(ListIndentFormat fmt, const char *custom, int space_width) {
    if (fmt == LIST_INDENT_CUSTOM)
        return custom ? custom : "";
    return indent_prefix(fmt, space_width);
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

// The list-bullet marker for a format, as a fresh malloc'd string (caller
// frees). Whitespace formats (spaces/tab, or a whitespace-only custom prefix)
// are not bullets, so a sensible "- " default is returned instead. Used by the
// rich-text paste converter to stamp pasted list items in the user's style.
// NULL on OOM.
char *list_bullet_marker(ListIndentFormat fmt, const char *custom_prefix) {
    const char *p = is_marker_format(fmt, custom_prefix)
                        ? effective_prefix(fmt, custom_prefix, LIST_INDENT_SPACES_DEFAULT_WIDTH)
                        : "- ";
    size_t n = strlen(p);
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, p, n + 1);
    return out;
}

#define NEST "  " // Two spaces: markdown nesting step for an already-marked line

// Length of the bullet marker at s (leading whitespace already skipped):
// the custom marker body if it matches, else the built-in "* " / "- ". With
// allow_bare the space-less forms also count when the text ends right after
// them (empty bullets, pre-0.13 documents); without it a lone "-" the user
// is still typing is not yet a bullet. ANY marker style counts regardless of
// the configured format, so mixed lists still deepen/unindent instead of
// stacking a second marker. "*emphasis*" is NOT a bullet. 0 when s does not
// start a bullet.
size_t list_marker_length(const char *s, size_t len, const char *custom, bool allow_bare) {
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
            if (allow_bare && tl > 0 && len == tl && memcmp(s, body, tl) == 0)
                return tl;
        }
    }
    if (len >= 2 && (s[0] == '*' || s[0] == '-') && s[1] == ' ')
        return 2;
    if (allow_bare && len == 1 && (s[0] == '*' || s[0] == '-'))
        return 1;
    return 0;
}

static size_t marker_at(const char *s, size_t len, const char *custom) {
    return list_marker_length(s, len, custom, true);
}

// The leading run of spaces and tabs (nesting, or a custom prefix's own
// lead-in such as "\t* ").
static size_t lead_ws(const char *line, size_t len) {
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    return i;
}

// Does the line start with the exact custom marker prefix (its lead-in
// included)? A whitespace-only custom prefix is not a marker.
static bool starts_with_custom(const char *line, size_t len, const char *custom) {
    size_t pl = custom ? strlen(custom) : 0;
    return pl > 0 && !prefix_is_whitespace(custom) && len >= pl && memcmp(line, custom, pl) == 0;
}

// Does the line already carry a bullet (optionally behind nesting spaces)?
static bool line_has_bullet(const char *line, size_t len, const char *custom) {
    if (starts_with_custom(line, len, custom))
        return true;
    size_t i = lead_ws(line, len);
    return marker_at(line + i, len - i, custom) > 0;
}

// Build a transformed document by applying fn(line,len,out,fmt,custom) per line.
typedef size_t (*LineXform)(const char *line, size_t len, char *out, ListIndentFormat fmt,
                            const char *custom, int space_width);

static char *apply_per_line(const char *text, ListIndentFormat fmt, const char *custom,
                            int space_width, LineXform fn, size_t max_grow) {
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
        o += fn(lines[i].start, lines[i].len, out + o, fmt, custom, space_width);
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
                          const char *custom, int space_width) {
    if (is_marker_format(fmt, custom) && line_has_bullet(line, len, custom)) {
        // Already a bullet (of any marker style): deepen with nesting
        // spaces, keep the single marker it has
        memcpy(out, NEST, strlen(NEST));
        memcpy(out + strlen(NEST), line, len);
        return strlen(NEST) + len;
    }
    const char *p = effective_prefix(fmt, custom, space_width);
    size_t pl = strlen(p);
    memcpy(out, p, pl);
    memcpy(out + pl, line, len);
    return pl + len;
}

static size_t unindent_line(const char *line, size_t len, char *out, ListIndentFormat fmt,
                            const char *custom, int space_width) {
    if (is_marker_format(fmt, custom)) {
        // The Custom format's own prefix comes off whole first, lead-in and
        // all: Indent added exactly that, so a prefix such as "  - " or
        // "\t* " must not be read as a deepened "- " / an unknown line
        if (fmt == LIST_INDENT_CUSTOM && starts_with_custom(line, len, custom)) {
            size_t pl = strlen(custom);
            memcpy(out, line + pl, len - pl);
            return len - pl;
        }
        // Marker-aware: act on whatever bullet the line actually carries,
        // not just the configured format's marker
        size_t ws = lead_ws(line, len);
        size_t m = marker_at(line + ws, len - ws, custom);
        if (m > 0) {
            if (ws >= 2 && line[0] == ' ' && line[1] == ' ') {
                // Deepened bullet: strip one nesting step (two spaces)
                memcpy(out, line + 2, len - 2);
                return len - 2;
            }
            if (line[0] == '\t') {
                // Tab-nested bullet: one tab is one level
                memcpy(out, line + 1, len - 1);
                return len - 1;
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
    // Spaces (or a tab line with leading spaces): remove up to one unit
    // (four by default) of leading spaces
    size_t unit = (size_t) clamp_space_width(space_width);
    size_t strip = 0;
    while (strip < unit && strip < len && line[strip] == ' ')
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

char *list_indent_lines_w(const char *text, ListIndentFormat fmt, const char *custom_prefix,
                          int space_width) {
    if (fmt == LIST_INDENT_CUSTOM && (!custom_prefix || !*custom_prefix))
        return copy_text(text);
    // Growth per line is the full prefix (NEST is shorter); keep the historic
    // 4-byte floor for the built-in formats
    size_t pl = strlen(effective_prefix(fmt, custom_prefix, space_width));
    return apply_per_line(text, fmt, custom_prefix, space_width, indent_line, pl > 4 ? pl : 4);
}

char *list_unindent_lines_w(const char *text, ListIndentFormat fmt, const char *custom_prefix,
                            int space_width) {
    if (fmt == LIST_INDENT_CUSTOM && (!custom_prefix || !*custom_prefix))
        return copy_text(text);
    return apply_per_line(text, fmt, custom_prefix, space_width, unindent_line, 0);
}

char *list_indent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix) {
    return list_indent_lines_w(text, fmt, custom_prefix, LIST_INDENT_SPACES_DEFAULT_WIDTH);
}

char *list_unindent_lines(const char *text, ListIndentFormat fmt, const char *custom_prefix) {
    return list_unindent_lines_w(text, fmt, custom_prefix, LIST_INDENT_SPACES_DEFAULT_WIDTH);
}
