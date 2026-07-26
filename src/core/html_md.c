/*
 * npad - HTML to Markdown/text conversion (rich-text paste)
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "html_md.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Bounds - a paste conversion must never blow up on hostile/huge input.
// ---------------------------------------------------------------------------
#define HM_MAX_DEPTH 64                     // list nesting clamp
#define HM_MAX_BQ 16                        // blockquote nesting clamp
#define HM_MAX_OUTPUT (16u * 1024u * 1024u) // absolute output byte cap
#define HM_NEST "  "                        // two spaces per list level (matches list_ops)

// ---------------------------------------------------------------------------
// ASCII-only char helpers (locale-independent)
// ---------------------------------------------------------------------------
static int hm_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static char hm_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}
static int hm_is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
// Case-insensitive equality of a lowercase literal against s[0..n).
static int hm_ci_eq(const char *s, size_t n, const char *lower_lit) {
    for (size_t i = 0; i < n; i++) {
        if (hm_lower(s[i]) != lower_lit[i] || lower_lit[i] == '\0')
            return 0;
    }
    return lower_lit[n] == '\0';
}
// Case-insensitive search for lowercase `needle` within [b,e). Returns the
// match start or NULL.
static const char *hm_find_ci(const char *b, const char *e, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0 || (size_t) (e - b) < nl)
        return NULL;
    for (const char *q = b; q + nl <= e; q++) {
        size_t i = 0;
        while (i < nl && hm_lower(q[i]) == needle[i])
            i++;
        if (i == nl)
            return q;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Dynamic byte buffer (self-contained, like list_ops's own helpers)
// ---------------------------------------------------------------------------
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom;
} Buf;

static int buf_reserve(Buf *b, size_t extra) {
    if (b->oom)
        return 0;
    size_t need = b->len + extra + 1;
    if (need < b->len) { // size_t overflow
        b->oom = 1;
        return 0;
    }
    if (b->cap >= need)
        return 1;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2)) {
            b->oom = 1;
            return 0;
        }
        ncap *= 2;
    }
    char *nd = realloc(b->data, ncap);
    if (!nd) {
        b->oom = 1;
        return 0;
    }
    b->data = nd;
    b->cap = ncap;
    return 1;
}
static void buf_putc_cap(Buf *b, char c, unsigned cap) {
    if (cap && b->len >= cap)
        return; // hard output cap: silently truncate
    if (!buf_reserve(b, 1))
        return;
    b->data[b->len++] = c;
}
static void buf_put_cap(Buf *b, const char *s, size_t n, unsigned cap) {
    for (size_t i = 0; i < n; i++)
        buf_putc_cap(b, s[i], cap);
}
static void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
    b->oom = 0;
}

// ---------------------------------------------------------------------------
// UTF-8 encode + HTML entity decoding
// ---------------------------------------------------------------------------
static size_t hm_utf8_encode(uint32_t cp, char out[8]) {
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
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

// Named entities npad decodes; anything else is left literal. nbsp maps to a
// normal space (friendlier for a plain-text editor than U+00A0).
static uint32_t hm_named_entity(const char *name) {
    static const struct {
        const char *n;
        uint32_t cp;
    } tbl[] = { { "amp", '&' },      { "lt", '<' },       { "gt", '>' },
                { "quot", '"' },     { "apos", '\'' },    { "nbsp", ' ' },
                { "copy", 0x00A9 },  { "reg", 0x00AE },   { "trade", 0x2122 },
                { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 },
                { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C },
                { "rdquo", 0x201D }, { "bull", 0x2022 },  { "middot", 0x00B7 },
                { "deg", 0x00B0 },   { "times", 0x00D7 } };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (strcmp(name, tbl[i].n) == 0)
            return tbl[i].cp;
    return 0;
}

// Decode an entity at p (points at '&'), bounded by n bytes. On success fills
// out[] with NUL-terminated UTF-8 and returns bytes consumed; else returns 0.
static size_t hm_decode_entity(const char *p, size_t n, char out[8]) {
    if (n < 3 || p[0] != '&')
        return 0;
    if (p[1] == '#') {
        size_t i = 2; // n >= 3 here, so index 2 is in bounds
        int base = 10;
        if (p[i] == 'x' || p[i] == 'X') {
            base = 16;
            i++;
        }
        size_t start = i;
        uint32_t cp = 0;
        while (i < n) {
            char c = p[i];
            int d;
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (base == 16 && c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                break;
            if (cp < 0x200000)
                cp = cp * (uint32_t) base + (uint32_t) d;
            i++;
        }
        if (i == start || i >= n || p[i] != ';')
            return 0;
        i++; // consume ';'
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = 0xFFFD;
        size_t k = hm_utf8_encode(cp, out);
        out[k] = '\0';
        return i;
    }
    // named
    char name[12];
    size_t ni = 0, i = 1;
    while (i < n && ni < sizeof(name) - 1 && hm_is_alnum(p[i]))
        name[ni++] = p[i++];
    name[ni] = '\0';
    if (ni == 0 || i >= n || p[i] != ';')
        return 0;
    i++; // consume ';'
    uint32_t cp = hm_named_entity(name);
    if (!cp)
        return 0;
    size_t k = hm_utf8_encode(cp, out);
    out[k] = '\0';
    return i;
}

// ---------------------------------------------------------------------------
// Converter state
// ---------------------------------------------------------------------------
typedef struct {
    Buf out;
    HtmlMdMode mode;
    const char *marker; // bullet for unordered / lists-mode items
    int at_bol;         // logical: the current line has no content yet
    int pending_space;  // collapsed whitespace waiting for the next content
    int trim_lead;      // one-shot: drop the next leading space (after a marker/heading prefix)
    int pending_break;  // 0 none, 1 newline, 2 blank line (top level only)
    int depth;          // list nesting depth
    int ordered[HM_MAX_DEPTH];
    int counter[HM_MAX_DEPTH];
    int bq;  // blockquote depth (FULL only)
    int pre; // inside <pre>
    int in_bold, in_italic, in_code, in_link;
    Buf href; // current <a> target
} State;

static unsigned hm_cap(const State *s) {
    (void) s;
    return HM_MAX_OUTPUT;
}
static void hm_raw(State *s, const char *str) {
    buf_put_cap(&s->out, str, strlen(str), hm_cap(s));
}

// End the current line if it carries content; keep at most a single trailing
// newline (blank lines are added explicitly via pending_break).
static void hm_newline(State *s) {
    if (s->out.len > 0 && s->out.data[s->out.len - 1] != '\n')
        buf_putc_cap(&s->out, '\n', hm_cap(s));
    s->at_bol = 1;
    s->pending_space = 0;
}

// Flush any pending block break, then emit the standing line prefix (blockquote
// markers). Called just before the first content of a line.
static void hm_open_line(State *s) {
    if (s->pending_break) {
        if (s->out.len > 0 && s->out.data[s->out.len - 1] != '\n')
            buf_putc_cap(&s->out, '\n', hm_cap(s));
        if (s->pending_break >= 2 && s->depth == 0 && s->out.len > 0) {
            // A blank separator line between paragraphs (top level only). Inside
            // a blockquote emit a quoted blank line ("> "-less ">") so adjacent
            // quoted paragraphs don't lazily merge under CommonMark.
            for (int i = 0; i < s->bq; i++)
                hm_raw(s, ">");
            buf_putc_cap(&s->out, '\n', hm_cap(s));
        }
        s->pending_break = 0;
        s->pending_space = 0;
        s->at_bol = 1;
    }
    if (s->at_bol) {
        for (int i = 0; i < s->bq; i++)
            hm_raw(s, "> ");
        s->at_bol = 0;
    }
}

// Flush a pending space before content, unless trim_lead says to drop it (the
// first space right after a list marker / heading prefix). Clears both.
static void hm_flush_space(State *s) {
    if (s->pending_space) {
        if (!s->trim_lead)
            buf_putc_cap(&s->out, ' ', hm_cap(s));
        s->pending_space = 0;
    }
    s->trim_lead = 0;
}

// Emit one content byte with leading-space trimming and pending-space flush.
static void hm_char(State *s, char c) {
    if (s->at_bol) {
        s->pending_space = 0; // trim leading whitespace
        s->trim_lead = 0;
        hm_open_line(s);
    } else {
        hm_flush_space(s);
    }
    buf_putc_cap(&s->out, c, hm_cap(s));
}

// Emit a whole string as inline content (opens the line, flushes one space).
static void hm_str(State *s, const char *str) {
    if (!str || !*str)
        return;
    if (s->at_bol) {
        s->pending_space = 0;
        s->trim_lead = 0;
        hm_open_line(s);
    } else {
        hm_flush_space(s);
    }
    hm_raw(s, str);
}

// Emit n raw content bytes (a possibly non-NUL-terminated Buf) like hm_str -
// used for <img> alt text, which lives in a length-counted Buf.
static void hm_bytes(State *s, const char *p, size_t n) {
    if (!p || n == 0)
        return;
    if (s->at_bol) {
        s->pending_space = 0;
        s->trim_lead = 0;
        hm_open_line(s);
    } else {
        hm_flush_space(s);
    }
    buf_put_cap(&s->out, p, n, hm_cap(s));
}

// Emit a CLOSING emphasis delimiter WITHOUT consuming a pending space, so
// whitespace that sat just inside the emphasis lands outside the marker
// (CommonMark won't close emphasis on a space-adjacent delimiter).
static void hm_emit_delim(State *s, const char *delim) {
    if (s->at_bol)
        hm_open_line(s);
    hm_raw(s, delim);
}

// Emit a byte verbatim (preserve leading spaces) - used inside <pre>.
static void hm_char_raw(State *s, char c) {
    if (s->at_bol)
        hm_open_line(s);
    buf_putc_cap(&s->out, c, hm_cap(s));
}

// A verbatim newline for <pre> content: unlike hm_newline it never collapses a
// second consecutive '\n', so blank lines inside a fenced code block survive.
static void hm_pre_newline(State *s) {
    buf_putc_cap(&s->out, '\n', hm_cap(s));
    s->at_bol = 1;
    s->pending_space = 0;
}

// ---------------------------------------------------------------------------
// Text runs (between tags): entity decode + whitespace collapse (or preserve
// inside <pre>).
// ---------------------------------------------------------------------------
static void hm_emit_decoded(State *s, const char *bytes) {
    // A decoded entity that is a single space collapses like whitespace.
    if (bytes[0] == ' ' && bytes[1] == '\0') {
        s->pending_space = 1;
        return;
    }
    for (const char *q = bytes; *q; q++)
        hm_char(s, *q);
}

static void hm_text(State *s, const char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        char c = p[i];
        if (c == '&') {
            char eb[8];
            size_t used = hm_decode_entity(p + i, n - i, eb);
            if (used) {
                if (s->pre) {
                    for (const char *q = eb; *q; q++)
                        hm_char_raw(s, *q);
                } else {
                    hm_emit_decoded(s, eb);
                }
                i += used;
                continue;
            }
            if (s->pre)
                hm_char_raw(s, '&');
            else
                hm_char(s, '&');
            i++;
            continue;
        }
        if (s->pre) {
            if (c == '\r') {
                hm_pre_newline(s);
                if (i + 1 < n && p[i + 1] == '\n')
                    i++;
            } else if (c == '\n') {
                hm_pre_newline(s);
            } else {
                hm_char_raw(s, c);
            }
            i++;
            continue;
        }
        if (hm_is_space(c)) {
            s->pending_space = 1;
            i++;
            continue;
        }
        hm_char(s, c);
        i++;
    }
}

// ---------------------------------------------------------------------------
// Attribute extraction (href/src/alt), with entity decoding of the value.
// ---------------------------------------------------------------------------
static void hm_append_decoded_range(Buf *b, const char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (p[i] == '&') {
            char eb[8];
            size_t used = hm_decode_entity(p + i, n - i, eb);
            if (used) {
                buf_put_cap(b, eb, strlen(eb), 0);
                i += used;
                continue;
            }
        }
        buf_putc_cap(b, p[i], 0);
        i++;
    }
}

// Find attribute `name` in [b,e) and write its decoded value into `val`.
static int hm_get_attr(const char *b, const char *e, const char *name, Buf *val) {
    size_t nl = strlen(name);
    for (const char *q = b; q + nl <= e; q++) {
        if (q != b && !hm_is_space(q[-1]))
            continue;
        size_t i = 0;
        while (i < nl && hm_lower(q[i]) == name[i])
            i++;
        if (i != nl)
            continue;
        const char *r = q + nl;
        while (r < e && hm_is_space(*r))
            r++;
        if (r >= e || *r != '=')
            continue;
        r++;
        while (r < e && hm_is_space(*r))
            r++;
        char quote = 0;
        if (r < e && (*r == '"' || *r == '\'')) {
            quote = *r;
            r++;
        }
        const char *vs = r;
        while (r < e && (quote ? *r != quote : (!hm_is_space(*r) && *r != '>')))
            r++;
        hm_append_decoded_range(val, vs, (size_t) (r - vs));
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// List item marker
// ---------------------------------------------------------------------------
static void hm_open_li(State *s) {
    hm_newline(s);
    s->pending_break = 0; // no blank lines inside a list
    hm_open_line(s);      // blockquote prefix, if any
    int lvl = s->depth < 1 ? 1 : s->depth;
    for (int i = 1; i < lvl; i++)
        hm_raw(s, HM_NEST);
    int top = s->depth - 1;
    if (top >= 0 && top < HM_MAX_DEPTH && s->ordered[top] && s->mode == HTML_MD_FULL) {
        char num[24];
        int v = ++s->counter[top];
        snprintf(num, sizeof(num), "%d. ", v);
        hm_raw(s, num);
    } else {
        if (top >= 0 && top < HM_MAX_DEPTH)
            s->counter[top]++;
        hm_raw(s, s->marker ? s->marker : "- ");
    }
    s->at_bol = 0;
    s->pending_space = 0;
    s->trim_lead = 1; // drop a leading space in the item text (no doubled space)
}

// ---------------------------------------------------------------------------
// Tag dispatch
// ---------------------------------------------------------------------------
static void hm_close_link(State *s) {
    if (!s->in_link)
        return;
    s->in_link = 0;
    s->pending_space = 0;
    if (s->at_bol)
        hm_open_line(s);
    hm_raw(s, "](");
    if (s->href.data)
        buf_put_cap(&s->out, s->href.data, s->href.len, hm_cap(s));
    buf_putc_cap(&s->out, ')', hm_cap(s));
}

static void hm_tag(State *s, const char *name, size_t nlen, int is_close, const char *attr_b,
                   const char *attr_e) {
    int full = (s->mode == HTML_MD_FULL);

#define EQ(lit) hm_ci_eq(name, nlen, lit)

    if (EQ("p") || EQ("div")) {
        if (is_close) {
            hm_newline(s);
            int want = (full && EQ("p")) ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
        } else {
            if (!s->at_bol)
                hm_newline(s);
        }
        return;
    }
    if (EQ("br")) {
        // A <br> at line start (right after another break) escalates to a blank
        // line so the <br><br> vertical-spacing idiom survives; otherwise it is
        // a plain line break.
        if (s->at_bol) {
            if (s->out.len > 0 && s->pending_break < 2)
                s->pending_break = 2;
        } else {
            hm_newline(s);
        }
        return;
    }
    if (nlen == 2 && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6') {
        int level = name[1] - '0';
        if (is_close) {
            hm_newline(s);
            int want = full ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
        } else {
            hm_newline(s);
            if (full) {
                hm_open_line(s);
                for (int i = 0; i < level; i++)
                    buf_putc_cap(&s->out, '#', hm_cap(s));
                buf_putc_cap(&s->out, ' ', hm_cap(s));
                s->at_bol = 0;
                s->trim_lead = 1; // drop a leading space after "# "
            }
        }
        return;
    }
    if (EQ("ul") || EQ("ol")) {
        if (is_close) {
            if (s->depth > 0)
                s->depth--;
            hm_newline(s);
            if (s->depth == 0 && s->pending_break < 1)
                s->pending_break = 1;
        } else if (s->depth < HM_MAX_DEPTH) {
            s->ordered[s->depth] = EQ("ol") ? 1 : 0;
            s->counter[s->depth] = 0;
            s->depth++;
        }
        return;
    }
    if (EQ("li")) {
        if (!is_close) {
            if (s->mode == HTML_MD_PLAIN)
                hm_newline(s); // plain: list items on their own lines, no marker
            else
                hm_open_li(s); // hm_open_li clamps depth 0 (bare <li>) safely
        }
        return;
    }
    if (EQ("blockquote")) {
        if (is_close) {
            if (full && s->bq > 0)
                s->bq--;
            hm_newline(s);
            int want = full ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
        } else {
            hm_newline(s);
            if (full && s->bq < HM_MAX_BQ)
                s->bq++;
        }
        return;
    }
    if (EQ("pre")) {
        if (is_close) {
            if (s->pre > 0)
                s->pre--;
            if (full) {
                hm_newline(s);
                hm_open_line(s);
                hm_raw(s, "```");
                hm_newline(s);
            }
            if (s->pending_break < 1)
                s->pending_break = 1;
        } else {
            hm_newline(s);
            if (full) {
                hm_open_line(s);
                hm_raw(s, "```");
                hm_newline(s);
            }
            s->pre++;
        }
        return;
    }
    if (EQ("hr")) {
        hm_newline(s);
        if (full) {
            hm_open_line(s);
            hm_raw(s, "---");
        }
        hm_newline(s);
        if (s->pending_break < 1)
            s->pending_break = 1;
        return;
    }
    if (!full)
        return; // remaining tags are inline formatting: FULL only

    if (EQ("b") || EQ("strong")) {
        if (is_close) {
            if (s->in_bold) {
                hm_emit_delim(s, "**"); // keep a trailing inside-space outside
                s->in_bold = 0;
            }
        } else if (!s->in_bold && !s->pre) {
            hm_str(s, "**");
            s->in_bold = 1;
        }
        return;
    }
    if (EQ("i") || EQ("em")) {
        if (is_close) {
            if (s->in_italic) {
                hm_emit_delim(s, "*");
                s->in_italic = 0;
            }
        } else if (!s->in_italic && !s->pre) {
            hm_str(s, "*");
            s->in_italic = 1;
        }
        return;
    }
    if (EQ("code")) {
        if (s->pre)
            return; // handled by the fenced block
        if (is_close) {
            if (s->in_code) {
                hm_emit_delim(s, "`");
                s->in_code = 0;
            }
        } else if (!s->in_code) {
            hm_str(s, "`");
            s->in_code = 1;
        }
        return;
    }
    if (EQ("a")) {
        if (is_close) {
            hm_close_link(s);
        } else if (!s->in_link) {
            // A nested <a> (invalid HTML) is ignored so the outer link stays
            // balanced rather than clobbering its href / re-opening a bracket
            s->href.len = 0;
            if (s->href.data)
                s->href.data[0] = '\0';
            if (hm_get_attr(attr_b, attr_e, "href", &s->href) && s->href.len > 0) {
                hm_str(s, "[");
                s->in_link = 1;
            }
        }
        return;
    }
    if (EQ("img")) {
        if (is_close)
            return;
        Buf src = { 0 }, alt = { 0 };
        hm_get_attr(attr_b, attr_e, "src", &src);
        hm_get_attr(attr_b, attr_e, "alt", &alt);
        if (src.len > 0) {
            hm_str(s, "![");
            if (alt.len > 0) {
                buf_put_cap(&s->out, alt.data, alt.len, hm_cap(s));
            }
            hm_raw(s, "](");
            buf_put_cap(&s->out, src.data, src.len, hm_cap(s));
            buf_putc_cap(&s->out, ')', hm_cap(s));
        } else if (alt.len > 0) {
            hm_bytes(s, alt.data, alt.len); // alt Buf is length-counted, not NUL-terminated
        }
        buf_free(&src);
        buf_free(&alt);
        return;
    }
#undef EQ
}

// Is this an element whose entire content we discard?
static int hm_is_skip(const char *name, size_t nlen) {
    return hm_ci_eq(name, nlen, "script") || hm_ci_eq(name, nlen, "style") ||
           hm_ci_eq(name, nlen, "head") || hm_ci_eq(name, nlen, "title");
}

// ---------------------------------------------------------------------------
// Public: HTML -> markdown/text
// ---------------------------------------------------------------------------
char *html_to_markdown(const char *html_utf8, HtmlMdMode mode, const char *list_marker) {
    if (!html_utf8)
        return NULL;
    if (mode != HTML_MD_PLAIN && mode != HTML_MD_LISTS && mode != HTML_MD_FULL)
        mode = HTML_MD_PLAIN;

    State s;
    memset(&s, 0, sizeof(s));
    s.mode = mode;
    s.marker = (list_marker && list_marker[0]) ? list_marker : "- ";
    s.at_bol = 1;

    const char *p = html_utf8;
    const char *end = html_utf8 + strlen(html_utf8);

    while (p < end) {
        if (*p != '<') {
            const char *q = memchr(p, '<', (size_t) (end - p));
            if (!q)
                q = end;
            hm_text(&s, p, (size_t) (q - p));
            p = q;
            continue;
        }
        // p points at '<'
        if (p + 1 < end && p[1] == '!') {
            if (end - p >= 4 && p[2] == '-' && p[3] == '-') {
                const char *c = hm_find_ci(p + 4, end, "-->");
                p = c ? c + 3 : end;
            } else {
                const char *gt = memchr(p, '>', (size_t) (end - p));
                p = gt ? gt + 1 : end;
            }
            continue;
        }
        if (p + 1 < end && p[1] == '?') {
            const char *gt = memchr(p, '>', (size_t) (end - p));
            p = gt ? gt + 1 : end;
            continue;
        }
        const char *gt = memchr(p, '>', (size_t) (end - p));
        if (!gt) {
            // No tag terminator: treat the stray '<' and the rest as text.
            hm_text(&s, p, (size_t) (end - p));
            p = end;
            continue;
        }
        const char *t = p + 1;
        int is_close = 0;
        if (t < gt && *t == '/') {
            is_close = 1;
            t++;
        }
        const char *nb = t;
        while (t < gt && hm_is_alnum(*t))
            t++;
        size_t nlen = (size_t) (t - nb);
        const char *attr_b = t;
        const char *attr_e = gt;
        if (nlen == 0) {
            // e.g. "< " - not a tag; emit literally.
            hm_text(&s, p, (size_t) (gt + 1 - p));
            p = gt + 1;
            continue;
        }
        if (!is_close && hm_is_skip(nb, nlen)) {
            // Skip the element's whole content.
            char closer[16];
            size_t k = 0;
            closer[k++] = '<';
            closer[k++] = '/';
            for (size_t j = 0; j < nlen && k < sizeof(closer) - 1; j++)
                closer[k++] = hm_lower(nb[j]);
            closer[k] = '\0';
            const char *c = hm_find_ci(gt + 1, end, closer);
            if (!c) {
                p = end;
            } else {
                const char *cg = memchr(c, '>', (size_t) (end - c));
                p = cg ? cg + 1 : end;
            }
            continue;
        }
        hm_tag(&s, nb, nlen, is_close, attr_b, attr_e);
        p = gt + 1;
    }

    // Close any inline formatting still open on a live line.
    if (!s.at_bol) {
        if (s.in_code)
            hm_str(&s, "`");
        if (s.in_italic)
            hm_str(&s, "*");
        if (s.in_bold)
            hm_str(&s, "**");
    }
    if (s.in_link)
        hm_close_link(&s);
    buf_free(&s.href);

    if (s.out.oom) {
        buf_free(&s.out);
        return NULL;
    }

    // Trim leading newlines and trailing whitespace/newlines.
    char *r = s.out.data;
    size_t len = s.out.len;
    if (!r) {
        r = malloc(1);
        if (r)
            r[0] = '\0';
        return r;
    }
    size_t start = 0;
    while (start < len && r[start] == '\n')
        start++;
    while (len > start &&
           (r[len - 1] == '\n' || r[len - 1] == ' ' || r[len - 1] == '\t' || r[len - 1] == '\r'))
        len--;
    memmove(r, r + start, len - start);
    r[len - start] = '\0';
    return r;
}

// ---------------------------------------------------------------------------
// Public: extract the copied fragment from a CF_HTML clipboard blob
// ---------------------------------------------------------------------------
static char *hm_copy_range(const char *p, size_t n) {
    char *r = malloc(n + 1);
    if (!r)
        return NULL;
    memcpy(r, p, n);
    r[n] = '\0';
    return r;
}

// Read the unsigned decimal value that follows `key` in the CF_HTML header;
// returns -1 if the key is absent or has no digits.
static long hm_header_num(const char *cf, const char *key) {
    const char *k = strstr(cf, key);
    if (!k)
        return -1;
    const char *q = k + strlen(key);
    while (*q == ' ' || *q == '\t')
        q++;
    if (*q < '0' || *q > '9')
        return -1;
    long v = 0;
    while (*q >= '0' && *q <= '9') {
        if (v < 100000000L)
            v = v * 10 + (*q - '0');
        q++;
    }
    return v;
}

char *html_cf_extract_fragment(const char *cf_html) {
    if (!cf_html)
        return NULL;
    size_t total = strlen(cf_html);

    long sf = hm_header_num(cf_html, "StartFragment:");
    long ef = hm_header_num(cf_html, "EndFragment:");
    if (sf >= 0 && ef > sf && (size_t) ef <= total)
        return hm_copy_range(cf_html + sf, (size_t) (ef - sf));

    const char *s = strstr(cf_html, "<!--StartFragment-->");
    const char *e = strstr(cf_html, "<!--EndFragment-->");
    if (s) {
        s += strlen("<!--StartFragment-->");
        if (e && e > s)
            return hm_copy_range(s, (size_t) (e - s));
    }

    // Last resort. If this is a CF_HTML blob (has the mandatory header) but the
    // offsets and comment markers were both unusable, skip the header's
    // key:value lines so they aren't pasted as literal text; return from the
    // first tag. Header-less input (raw HTML, or plain text) is returned whole.
    if (sf >= 0 || ef >= 0 || strncmp(cf_html, "Version:", 8) == 0) {
        const char *lt = memchr(cf_html, '<', total);
        if (lt)
            return hm_copy_range(lt, total - (size_t) (lt - cf_html));
    }
    return hm_copy_range(cf_html, total);
}
