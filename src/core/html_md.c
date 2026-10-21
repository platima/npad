/*
 * npad - HTML to Markdown/text conversion (rich-text paste)
 *
 * A single forward pass over the HTML: text runs are decoded and collapsed,
 * tags drive a small state machine (block breaks, lists, tables, preformatted
 * blocks, inline emphasis). Inline delimiters are written lazily - only once
 * real content follows - and closed again at every block break, so the output
 * never carries an empty "****", a delimiter on the wrong side of a space, or
 * an emphasis span that crosses a paragraph. In full Markdown mode the text
 * itself is escaped wherever it would otherwise be read as Markdown syntax.
 *
 * Author: Platima
 * https://npad.dev
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
#define HM_NEST 2                           // Lists mode: two spaces per level (matches list_ops)
#define HM_MAX_ISTACK 256                   // open inline elements tracked one by one
#define HM_MAX_OL_START 999999999L          // CommonMark allows at most nine digits

// ---------------------------------------------------------------------------
// ASCII-only char helpers (locale-independent)
// ---------------------------------------------------------------------------
static int hm_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static char hm_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}
static int hm_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int hm_is_digit(char c) {
    return c >= '0' && c <= '9';
}
static int hm_is_alnum(char c) {
    return hm_is_alpha(c) || hm_is_digit(c);
}
static int hm_is_punct(char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') ||
           (c >= '{' && c <= '~');
}
// Case-insensitive equality of a lowercase literal against s[0..n).
static int hm_ci_eq(const char *s, size_t n, const char *lower_lit) {
    for (size_t i = 0; i < n; i++) {
        if (hm_lower(s[i]) != lower_lit[i] || lower_lit[i] == '\0')
            return 0;
    }
    return lower_lit[n] == '\0';
}
// Does [b,e) start with lowercase `lit`, ignoring case?
static int hm_ci_prefix(const char *b, const char *e, const char *lit) {
    size_t n = strlen(lit);
    return (size_t) (e - b) >= n && hm_ci_eq(b, n, lit);
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
// Insert n copies of c at pos (used to widen a code delimiter or fence once
// its content is known). Skipped when it would pass the output cap.
static void buf_insert_fill(Buf *b, size_t pos, char c, size_t n, unsigned cap) {
    if (n == 0 || pos > b->len)
        return;
    if (cap && (b->len + n > cap || b->len + n < b->len))
        return;
    if (!buf_reserve(b, n))
        return;
    memmove(b->data + pos + n, b->data + pos, b->len - pos);
    memset(b->data + pos, c, n);
    b->len += n;
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
// Tags and attributes - quote-aware, so a '>' or "href=" inside a quoted
// attribute value is data, not markup.
// ---------------------------------------------------------------------------

// The '>' that ends the tag starting at p (which points at '<'), skipping
// quoted attribute values; NULL when the input ends first. An unterminated
// quote falls back to the first '>' after p, so one stray quote cannot
// swallow the rest of the paste.
static const char *hm_tag_end(const char *p, const char *end) {
    const char *q = p + 1;
    while (q < end) {
        char c = *q;
        if (c == '>')
            return q;
        if (c == '=') {
            q++;
            while (q < end && hm_is_space(*q))
                q++;
            if (q < end && (*q == '"' || *q == '\'')) {
                const char *close = memchr(q + 1, *q, (size_t) (end - q - 1));
                if (!close)
                    return memchr(p, '>', (size_t) (end - p));
                q = close + 1;
            }
            continue;
        }
        q++;
    }
    return NULL;
}

// Walk one name[=value] pair from *pp within [*pp, e). Returns 0 when no
// attributes remain. The value excludes its quotes (empty when there is none).
static int hm_next_attr(const char **pp, const char *e, const char **nb, size_t *nl,
                        const char **vb, size_t *vl) {
    const char *q = *pp;
    while (q < e && (hm_is_space(*q) || *q == '/'))
        q++;
    if (q >= e)
        return 0;
    *nb = q;
    while (q < e && !hm_is_space(*q) && *q != '=' && *q != '/')
        q++;
    if (q == *nb)
        q++; // a stray byte (e.g. '"'): step over it, never loop
    *nl = (size_t) (q - *nb);
    *vb = q;
    *vl = 0;
    const char *r = q;
    while (r < e && hm_is_space(*r))
        r++;
    if (r < e && *r == '=') {
        r++;
        while (r < e && hm_is_space(*r))
            r++;
        if (r < e && (*r == '"' || *r == '\'')) {
            char quote = *r++;
            const char *close = memchr(r, quote, (size_t) (e - r));
            const char *ve = close ? close : e;
            *vb = r;
            *vl = (size_t) (ve - r);
            q = close ? close + 1 : e;
        } else {
            *vb = r;
            while (r < e && !hm_is_space(*r))
                r++;
            *vl = (size_t) (r - *vb);
            q = r;
        }
    }
    *pp = q;
    return 1;
}

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

// Find attribute `name` (lowercase) in [b,e) and put its entity-decoded value
// in `val` (replacing what was there). The first occurrence wins, as in HTML.
static int hm_get_attr(const char *b, const char *e, const char *name, Buf *val) {
    val->len = 0;
    const char *q = b, *nb, *vb;
    size_t nl, vl;
    while (hm_next_attr(&q, e, &nb, &nl, &vb, &vl)) {
        if (hm_ci_eq(nb, nl, name)) {
            hm_append_decoded_range(val, vb, vl);
            return 1;
        }
    }
    return 0;
}

// The value of CSS property `prop` (lowercase) in a style attribute, the last
// declaration winning (Google Docs writes "white-space:pre;white-space:pre-wrap"),
// lowercased and trimmed, "!important" dropped. 0 when absent.
static int hm_style_prop(const char *st, size_t n, const char *prop, char *out, size_t outsz) {
    int found = 0;
    size_t i = 0;
    while (i < n) {
        size_t ds = i;
        while (i < n && st[i] != ';')
            i++;
        size_t de = i; // declaration [ds, de)
        if (i < n)
            i++;
        const char *colon = memchr(st + ds, ':', de - ds);
        if (!colon)
            continue;
        size_t a = ds, b = (size_t) (colon - st);
        while (a < b && hm_is_space(st[a]))
            a++;
        while (b > a && hm_is_space(st[b - 1]))
            b--;
        if (!hm_ci_eq(st + a, b - a, prop))
            continue;
        size_t va = (size_t) (colon - st) + 1, ve = de;
        while (va < ve && hm_is_space(st[va]))
            va++;
        size_t k = 0;
        while (va < ve && k + 1 < outsz)
            out[k++] = hm_lower(st[va++]);
        out[k] = '\0';
        char *bang = strchr(out, '!');
        if (bang)
            *bang = '\0';
        k = strlen(out);
        while (k > 0 && hm_is_space(out[k - 1]))
            out[--k] = '\0';
        found = 1;
    }
    return found;
}

// A style attribute's property, or 0 when the tag has no style / no such
// property.
static int hm_tag_style(const char *ab, const char *ae, const char *prop, char *out, size_t outsz) {
    Buf st = { 0 };
    int r = hm_get_attr(ab, ae, "style", &st) && st.len > 0 &&
            hm_style_prop(st.data, st.len, prop, out, outsz);
    buf_free(&st);
    return r;
}

// font-weight: 1 bold, 0 not bold, -1 unrecognized.
static int hm_weight_is_bold(const char *w) {
    if (strcmp(w, "bold") == 0 || strcmp(w, "bolder") == 0)
        return 1;
    if (strcmp(w, "normal") == 0 || strcmp(w, "lighter") == 0)
        return 0;
    if (hm_is_digit(w[0]))
        return atoi(w) >= 600;
    return -1;
}

// ---------------------------------------------------------------------------
// Converter state
// ---------------------------------------------------------------------------

// Inline kinds, each with its own Markdown delimiter.
enum { K_BOLD = 0, K_ITALIC = 1, K_CODE = 2, K_LINK = 3, K_COUNT = 4 };
static const char *const K_OPEN[K_COUNT] = { "**", "*", "`", "[" };

// Elements that may switch emphasis on, tracked one by one so each close tag
// undoes exactly what its open tag did: Google Docs wraps the whole paste in
// <b style="font-weight:normal">, which adds nothing, and carries real bold
// on <span style="font-weight:700">.
enum { T_B = 1, T_I = 2, T_SPAN = 3 };
#define F_BOLD 1
#define F_ITALIC 2

typedef struct {
    Buf out;
    HtmlMdMode mode;
    int full;           // mode == HTML_MD_FULL
    const char *marker; // bullet for unordered / lists-mode items
    int marker_len;
    int at_bol;        // logical: the current line has no content yet
    int pending_space; // collapsed whitespace waiting for the next content
    int trim_lead;     // one-shot: drop the next leading space (after a marker/heading prefix)
    int pending_break; // 0 none, 1 newline, 2 blank line
    int lead;          // what the line holds so far: 0 nothing, 1 only digits, 2 other

    int depth;      // list nesting depth
    int depth_lost; // list opens past HM_MAX_DEPTH, so their closes balance
    int ordered[HM_MAX_DEPTH];
    int counter[HM_MAX_DEPTH];
    int content_col[HM_MAX_DEPTH]; // column where an item's text starts, per level
    int opening_li;                // writing a marker line: no continuation indent
    int li_fresh;                  // a marker was just written; a block opener stays on its line
    int saw_li;                    // at least one list item was converted
    int top_items;                 // items so far in the current top-level list
    int word_depth;                // list levels opened by Word mso-list paragraphs
    int word_li;                   // inside a Word list paragraph

    int bq;          // blockquote depth (FULL only)
    int pre;         // <pre> nesting
    int wsp;         // nesting of the white-space:pre block's own tag name
    int wsp_p;       // ... which is <p> (else <div>)
    int pre_skip_lf; // the LF right after <pre> is not content (HTML parsing rule)
    int fence_open;  // a ``` fence was written for the current preformatted block
    size_t fence_pos;

    int kcount[K_COUNT];   // nesting count per inline kind
    int korder[K_COUNT];   // active kinds in the order they opened
    int nk;                // entries in korder
    int kwritten[K_COUNT]; // delimiter written on the current line
    size_t code_pos;       // offset of the current code span's opening backtick
    unsigned char istack[HM_MAX_ISTACK];
    int nistack, istack_lost;
    Buf href; // current <a> target

    int cell;         // inside a table cell
    int row_open;     // a table row has been started on the current line
    int row_cells;    // cells written in the current row
    int header_done;  // the current table's header separator row is written
    int inner_tables; // tables nested inside a cell (flattened)

    const char *src_next; // input right after the tag being handled
    const char *src_end;
} State;

static unsigned hm_cap(const State *s) {
    (void) s;
    return HM_MAX_OUTPUT;
}
static void hm_raw(State *s, const char *str) {
    buf_put_cap(&s->out, str, strlen(str), hm_cap(s));
}
static void hm_fill(State *s, char c, size_t n) {
    for (size_t i = 0; i < n; i++)
        buf_putc_cap(&s->out, c, hm_cap(s));
}
static int hm_in_pre(const State *s) {
    return s->pre > 0 || s->wsp > 0;
}
static int hm_lvl(int depth) {
    return depth > HM_MAX_DEPTH ? HM_MAX_DEPTH : depth;
}

// Longest run of c in out[from..len).
static size_t hm_longest_run(const State *s, size_t from, char c) {
    size_t run = 0, best = 0;
    for (size_t i = from; i < s->out.len; i++) {
        if (s->out.data[i] == c) {
            if (++run > best)
                best = run;
        } else {
            run = 0;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Inline delimiters
// ---------------------------------------------------------------------------

// The URL of a link or image: wrapped in <...> when it holds a space, a
// parenthesis or an angle bracket (or is empty), backslashes escaped.
static void hm_put_url(State *s, const char *u, size_t n) {
    int angle = (n == 0);
    for (size_t i = 0; i < n; i++)
        if (u[i] == ' ' || u[i] == '(' || u[i] == ')' || u[i] == '<' || u[i] == '>')
            angle = 1;
    if (angle)
        buf_putc_cap(&s->out, '<', hm_cap(s));
    for (size_t i = 0; i < n; i++) {
        if (u[i] == '\\' || (angle && (u[i] == '<' || u[i] == '>')))
            buf_putc_cap(&s->out, '\\', hm_cap(s));
        buf_putc_cap(&s->out, u[i], hm_cap(s));
    }
    if (angle)
        buf_putc_cap(&s->out, '>', hm_cap(s));
}

// Close a code span: a backtick inside the code needs a longer delimiter run
// than any run in the content, and a space pad when the content starts or
// ends with a backtick.
static void hm_close_code(State *s) {
    size_t from = s->code_pos + 1 <= s->out.len ? s->code_pos + 1 : s->out.len;
    size_t best = hm_longest_run(s, from, '`');
    if (best == 0) {
        hm_raw(s, "`");
        return;
    }
    int pad = s->out.len > from && (s->out.data[from] == '`' || s->out.data[s->out.len - 1] == '`');
    if (pad)
        buf_insert_fill(&s->out, from, ' ', 1, hm_cap(s));
    buf_insert_fill(&s->out, from, '`', best, hm_cap(s));
    if (pad)
        buf_putc_cap(&s->out, ' ', hm_cap(s));
    hm_fill(s, '`', best + 1);
}

// Write kind k's closing delimiter. Never consumes a pending space, so
// whitespace that sat just inside the element lands outside the marker
// (CommonMark won't close emphasis on a space-adjacent delimiter).
static void hm_close_delim(State *s, int k) {
    if (k == K_CODE) {
        hm_close_code(s);
    } else if (k == K_LINK) {
        hm_raw(s, "](");
        hm_put_url(s, s->href.data, s->href.len);
        buf_putc_cap(&s->out, ')', hm_cap(s));
    } else {
        hm_raw(s, K_OPEN[k]);
    }
    s->kwritten[k] = 0;
}

// Close every delimiter written on this line, innermost first. The elements
// stay open, so their delimiters reopen before the next content - emphasis
// never spans a line or block break.
static void hm_inline_close_written(State *s) {
    for (int i = s->nk - 1; i >= 0; i--)
        if (s->kwritten[s->korder[i]])
            hm_close_delim(s, s->korder[i]);
}

// Write the opening delimiters of the open elements that have none on this
// line yet. Nothing opens inside a code span or a preformatted block.
static void hm_inline_open(State *s) {
    if (hm_in_pre(s))
        return;
    int code_on = 0;
    for (int i = 0; i < s->nk; i++) {
        int k = s->korder[i];
        if (s->kwritten[k]) {
            code_on |= (k == K_CODE);
            continue;
        }
        if (code_on)
            continue;
        if (k == K_CODE) {
            s->code_pos = s->out.len;
            code_on = 1;
        }
        hm_raw(s, K_OPEN[k]);
        s->kwritten[k] = 1;
        s->lead = 2;
    }
}

static void hm_kind_push(State *s, int k) {
    if (s->kcount[k]++ == 0)
        s->korder[s->nk++] = k;
}

static void hm_kind_pop(State *s, int k) {
    if (s->kcount[k] == 0 || --s->kcount[k] > 0)
        return;
    int idx = 0;
    while (idx < s->nk && s->korder[idx] != k)
        idx++;
    if (idx == s->nk)
        return;
    if (s->kwritten[k]) {
        // Misnested HTML (<b><i></b></i>): close what opened inside k first;
        // those elements stay open and reopen at the next content
        for (int i = s->nk - 1; i > idx; i--)
            if (s->kwritten[s->korder[i]])
                hm_close_delim(s, s->korder[i]);
        hm_close_delim(s, k);
    }
    for (int i = idx; i + 1 < s->nk; i++)
        s->korder[i] = s->korder[i + 1];
    s->nk--;
}

static void hm_istack_push(State *s, int tag, int flags) {
    if (s->nistack >= HM_MAX_ISTACK) {
        s->istack_lost++;
        return;
    }
    s->istack[s->nistack++] = (unsigned char) ((tag << 4) | flags);
    if (flags & F_BOLD)
        hm_kind_push(s, K_BOLD);
    if (flags & F_ITALIC)
        hm_kind_push(s, K_ITALIC);
}

static void hm_istack_pop(State *s, int tag) {
    int i = s->nistack - 1;
    while (i >= 0 && (s->istack[i] >> 4) != tag)
        i--;
    if (i < 0) {
        if (s->istack_lost > 0)
            s->istack_lost--;
        return; // a stray close tag
    }
    int flags = s->istack[i] & 0x0F;
    for (; i + 1 < s->nistack; i++)
        s->istack[i] = s->istack[i + 1];
    s->nistack--;
    if (flags & F_ITALIC)
        hm_kind_pop(s, K_ITALIC);
    if (flags & F_BOLD)
        hm_kind_pop(s, K_BOLD);
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

// End the current line if it carries content; keep at most a single trailing
// newline (blank lines are added explicitly via pending_break). Inside a table
// cell a block break is only a space: a Markdown table row is one line.
static void hm_newline(State *s) {
    if (s->cell) {
        s->pending_space = 1;
        return;
    }
    if (!s->at_bol)
        hm_inline_close_written(s);
    if (s->out.len > 0 && s->out.data[s->out.len - 1] != '\n')
        buf_putc_cap(&s->out, '\n', hm_cap(s));
    s->at_bol = 1;
    s->pending_space = 0;
    s->li_fresh = 0;
    s->lead = 0;
}

// Flush any pending block break, then emit the standing line prefix
// (blockquote markers, and inside a list item the indent that keeps a
// continuation line in that item). Called just before the first content of a
// line.
static void hm_open_line(State *s) {
    if (s->pending_break) {
        if (s->out.len > 0 && s->out.data[s->out.len - 1] != '\n')
            buf_putc_cap(&s->out, '\n', hm_cap(s));
        if (s->pending_break >= 2 && (s->depth == 0 || s->full || s->opening_li) &&
            s->out.len > 0) {
            // A blank separator line. Inside a blockquote emit a quoted blank
            // line ("> "-less ">") so adjacent quoted paragraphs don't lazily
            // merge under CommonMark.
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
        if (s->depth > 0 && !s->opening_li)
            hm_fill(s, ' ', (size_t) s->content_col[hm_lvl(s->depth) - 1]);
        s->at_bol = 0;
        s->lead = 0;
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

// Ready the line for content: open it (or flush the space before it), then
// write the delimiters of any inline element still waiting for content.
static void hm_begin_content(State *s) {
    if (s->at_bol || s->pending_break) {
        s->pending_space = 0; // trim leading whitespace
        s->trim_lead = 0;
        hm_open_line(s);
    } else {
        hm_flush_space(s);
    }
    s->li_fresh = 0;
    hm_inline_open(s);
}

// A verbatim byte inside a preformatted block (leading spaces kept).
static void hm_char_raw(State *s, char c) {
    if (s->at_bol)
        hm_open_line(s);
    s->li_fresh = 0;
    s->lead = 2;
    buf_putc_cap(&s->out, c, hm_cap(s));
}

// A verbatim newline for preformatted content: unlike hm_newline it never
// collapses a second consecutive '\n', so blank lines inside a code block
// survive.
static void hm_pre_newline(State *s) {
    buf_putc_cap(&s->out, '\n', hm_cap(s));
    s->at_bol = 1;
    s->pending_space = 0;
    s->lead = 0;
}

// Would text byte c (followed in the source by look[0..lookn)) be read as
// Markdown syntax if written as is?
static int hm_needs_escape(const State *s, char c, const char *look, size_t lookn) {
    char prev = s->out.len ? s->out.data[s->out.len - 1] : '\n';
    char nx = lookn ? look[0] : '\0';
    int nx_blank = (nx == '\0' || nx == ' ' || nx == '\t');
    switch (c) {
        case '`':
        case '[':
        case ']':
        case '~': // GFM strikethrough, or a ~~~ fence
            return 1;
        case '*': // a lone '*' between spaces is neither emphasis nor (mid-line) a bullet
            return s->lead == 0 || !(prev == ' ' && nx == ' ');
        case '\\':
            return nx == '\0' || hm_is_punct(nx);
        case '_': // intraword underscores never form emphasis
            return !(hm_is_alnum(prev) && hm_is_alnum(nx));
        case '<': // raw HTML or an autolink
            return nx == '\0' || hm_is_alpha(nx) || nx == '/' || nx == '!' || nx == '?';
        case '&': { // an entity reference the renderer would decode
            if (nx == '#')
                return 1;
            size_t i = 0;
            while (i < lookn && hm_is_alnum(look[i]))
                i++;
            return i > 0 && i < lookn && look[i] == ';';
        }
        case '|':
            return s->cell;
        default:
            break;
    }
    if (s->lead == 0) { // block syntax at the start of a line
        switch (c) {
            case '>': // a block quote
                return 1;
            case '#': // a heading
                return nx_blank || nx == '#';
            case '-': // a bullet, a rule, or a setext underline
            case '=':
                return nx_blank || nx == c;
            case '+':
                return nx_blank;
            default:
                break;
        }
    }
    // "1." / "1)" at the start of a line is an ordered list item
    return s->lead == 1 && (c == '.' || c == ')') && nx_blank;
}

// Emit one byte of text content (whitespace already collapsed). In full
// Markdown mode it is escaped wherever it would read as syntax; inside code
// only a table cell's '|' needs that.
static void hm_text_char(State *s, char c, const char *look, size_t lookn) {
    hm_begin_content(s);
    if (s->full && !hm_in_pre(s)) {
        int esc = s->kcount[K_CODE] ? (c == '|' && s->cell) : hm_needs_escape(s, c, look, lookn);
        if (esc)
            buf_putc_cap(&s->out, '\\', hm_cap(s));
    }
    buf_putc_cap(&s->out, c, hm_cap(s));
    s->lead = (s->lead <= 1 && hm_is_digit(c)) ? 1 : 2;
}

// ---------------------------------------------------------------------------
// Text runs (between tags): entity decode + whitespace collapse (or preserve
// inside a preformatted block).
// ---------------------------------------------------------------------------
static void hm_text(State *s, const char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        char c = p[i];
        if (s->pre_skip_lf) {
            s->pre_skip_lf = 0;
            if (c == '\n' || c == '\r') {
                i += (c == '\r' && i + 1 < n && p[i + 1] == '\n') ? 2 : 1;
                continue;
            }
        }
        if (c == '&') {
            char eb[8];
            size_t used = hm_decode_entity(p + i, n - i, eb);
            if (used) {
                if (hm_in_pre(s)) {
                    for (const char *q = eb; *q; q++) {
                        if (*q == '\n' || *q == '\r')
                            hm_pre_newline(s);
                        else
                            hm_char_raw(s, *q);
                    }
                } else if (eb[1] == '\0' && hm_is_space(eb[0])) {
                    s->pending_space = 1; // &nbsp; &#10; &#13; collapse like whitespace
                } else {
                    for (const char *q = eb; *q; q++)
                        hm_text_char(s, *q, p + i + used, n - i - used);
                }
                i += used;
                continue;
            }
        }
        if (hm_in_pre(s)) {
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
        hm_text_char(s, c, p + i + 1, n - i - 1);
        i++;
    }
}

// Already-decoded text (an <img> alt with no src) through the same
// collapse-and-escape path as ordinary text.
static void hm_text_decoded(State *s, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (hm_is_space(p[i]))
            s->pending_space = 1;
        else
            hm_text_char(s, p[i], p + i + 1, n - i - 1);
    }
}

// A URL from href/src: CR, LF, TAB and other control bytes dropped (HTML
// strips them from URLs; in Markdown a line break would end the link), and
// surrounding spaces trimmed.
static void hm_clean_url(Buf *b) {
    size_t o = 0;
    for (size_t i = 0; i < b->len; i++)
        if ((unsigned char) b->data[i] >= 0x20)
            b->data[o++] = b->data[i];
    size_t a = 0;
    while (a < o && b->data[a] == ' ')
        a++;
    while (o > a && b->data[o - 1] == ' ')
        o--;
    if (a)
        memmove(b->data, b->data + a, o - a);
    b->len = o - a;
}

// Alt text: every whitespace run (CR and LF included) becomes one space,
// trimmed at both ends.
static void hm_clean_alt(Buf *b) {
    size_t o = 0;
    int sp = 0;
    for (size_t i = 0; i < b->len; i++) {
        char c = b->data[i];
        if (hm_is_space(c) || (unsigned char) c < 0x20) {
            sp = (o > 0);
            continue;
        }
        if (sp)
            b->data[o++] = ' ';
        sp = 0;
        b->data[o++] = c;
    }
    b->len = o;
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

// Start a list item: its marker on a new line, indented under its parent -
// two spaces per level in Lists mode (what list_ops' Indent produces), the
// parent's content column in Markdown mode (CommonMark only nests a child
// that starts there, so "1. " children need three spaces).
static void hm_open_li(State *s) {
    int top = hm_lvl(s->depth) - 1;
    hm_newline(s);
    // No blank lines between list items; the break before a top-level list's
    // first item (after a paragraph) stays
    if (!(top == 0 && s->top_items == 0))
        s->pending_break = 0;
    if (top == 0)
        s->top_items++;
    s->opening_li = 1;
    hm_open_line(s); // blockquote prefix, if any
    s->opening_li = 0;
    int indent = 0;
    if (top > 0)
        indent = s->full ? s->content_col[top - 1] : top * HM_NEST;
    hm_fill(s, ' ', (size_t) indent);
    int mlen;
    if (top >= 0 && s->ordered[top] && s->full) {
        char num[24];
        if (s->counter[top] < HM_MAX_OL_START)
            s->counter[top]++;
        mlen = snprintf(num, sizeof(num), "%d. ", s->counter[top]);
        hm_raw(s, num);
    } else {
        if (top >= 0 && s->counter[top] < HM_MAX_OL_START)
            s->counter[top]++;
        hm_raw(s, s->marker);
        mlen = s->marker_len;
    }
    if (top >= 0)
        s->content_col[top] = indent + mlen;
    s->at_bol = 0;
    s->pending_space = 0;
    s->trim_lead = 1; // drop a leading space in the item text (no doubled space)
    s->li_fresh = 1;
    s->lead = 0; // "- # x" would be a heading inside the item
    s->saw_li = 1;
}

static long hm_attr_long(const char *ab, const char *ae, const char *name, long dflt) {
    Buf v = { 0 };
    long r = dflt;
    if (hm_get_attr(ab, ae, name, &v) && v.len > 0) {
        size_t i = 0;
        while (i < v.len && hm_is_space(v.data[i]))
            i++;
        if (i < v.len && hm_is_digit(v.data[i])) {
            r = 0;
            while (i < v.len && hm_is_digit(v.data[i])) {
                // Saturate before the multiply: long is 32 bits on Windows
                if (r > (HM_MAX_OL_START - 9) / 10)
                    r = HM_MAX_OL_START;
                else
                    r = r * 10 + (v.data[i] - '0');
                i++;
            }
        }
    }
    buf_free(&v);
    return r;
}

// A Word list paragraph carries "mso-list:l0 level2 lfo1" in its style; its
// level (1-based), or 0 for any other paragraph.
static int hm_word_list_level(const char *ab, const char *ae) {
    char v[64];
    if (!hm_tag_style(ab, ae, "mso-list", v, sizeof(v)))
        return 0;
    const char *lv = strstr(v, "level");
    if (!lv)
        return 0; // mso-list:Ignore / none
    int lvl = 0;
    for (lv += 5; hm_is_digit(*lv) && lvl < 100; lv++)
        lvl = lvl * 10 + (*lv - '0');
    return lvl;
}

// Is the Word list paragraph that starts at s->src_next numbered? Word writes
// the item's number or bullet glyph inside <![if !supportLists]>: "1." "a)"
// "iv." are numbers, "·" "o" "§" bullets.
static int hm_word_list_ordered(const State *s) {
    const char *p = s->src_next, *e = s->src_end;
    if (!p || p >= e)
        return 0;
    const char *lim = (e - p > 4096) ? p + 4096 : e;
    const char *a = hm_find_ci(p, lim, "supportlists]");
    if (!a)
        return 0;
    a = memchr(a, '>', (size_t) (lim - a));
    if (!a)
        return 0;
    a++;
    char tok[16];
    size_t tn = 0;
    while (a < lim && tn < sizeof(tok) - 1) {
        if (*a == '<') {
            if (hm_ci_prefix(a, lim, "<![endif]") || hm_ci_prefix(a, lim, "<!--[endif]"))
                break;
            const char *g = memchr(a, '>', (size_t) (lim - a));
            if (!g)
                break;
            a = g + 1;
            continue;
        }
        if (*a == '&' || hm_is_space(*a)) { // &nbsp; padding ends the token
            if (tn)
                break;
            const char *semi = (*a == '&') ? memchr(a, ';', (size_t) (lim - a)) : NULL;
            a = semi ? semi + 1 : a + 1;
            continue;
        }
        tok[tn++] = *a++;
    }
    if (tn < 2 || (tok[tn - 1] != '.' && tok[tn - 1] != ')'))
        return 0;
    for (size_t i = 0; i + 1 < tn; i++)
        if (!hm_is_alnum(tok[i]))
            return 0;
    return 1;
}

// A Word list paragraph at `level`: Word writes lists as a run of <p>
// elements, not <ul>/<li>, so the levels are opened and closed here.
static void hm_word_li(State *s, int level, int ordered) {
    int base = s->depth - s->word_depth; // list depth outside the Word list
    if (level > HM_MAX_DEPTH - base)
        level = HM_MAX_DEPTH - base;
    if (level < 1) {
        hm_newline(s);
        return;
    }
    while (s->word_depth < level) {
        int d = s->depth;
        s->ordered[d] = ordered;
        s->counter[d] = 0;
        s->content_col[d] = d > 0 ? s->content_col[d - 1] : 0;
        if (d == 0)
            s->top_items = 0;
        s->depth++;
        s->word_depth++;
    }
    s->depth -= s->word_depth - level;
    s->word_depth = level;
    int top = s->depth - 1;
    if (s->ordered[top] != ordered) {
        s->ordered[top] = ordered;
        s->counter[top] = 0;
    }
    hm_open_li(s);
    s->word_li = 1;
}

static void hm_word_list_end(State *s) {
    if (!s->word_depth)
        return;
    s->depth -= s->word_depth;
    s->word_depth = 0;
    s->word_li = 0;
    hm_newline(s);
    int want = s->full ? 2 : 1;
    if (s->depth == 0 && s->pending_break < want)
        s->pending_break = want;
}

// ---------------------------------------------------------------------------
// Preformatted blocks (<pre>, or a <div>/<p> styled white-space:pre as VS Code
// and Windows Terminal copy)
// ---------------------------------------------------------------------------
static void hm_pre_start(State *s) {
    hm_newline(s);
    if (s->full) {
        hm_open_line(s);
        s->fence_pos = s->out.len;
        hm_raw(s, "```");
        s->fence_open = 1;
        hm_newline(s);
    }
}

// The fence is longer than any backtick run in the block, so content holding
// ``` cannot end it early.
static void hm_pre_finish(State *s) {
    if (s->fence_open) {
        size_t best = hm_longest_run(s, s->fence_pos + 3, '`');
        size_t n = best >= 3 ? best + 1 : 3;
        hm_newline(s);
        buf_insert_fill(&s->out, s->fence_pos, '`', n - 3, hm_cap(s));
        hm_open_line(s);
        hm_fill(s, '`', n);
        hm_newline(s);
        s->fence_open = 0;
    }
    int want = s->full ? 2 : 1;
    if (s->pending_break < want)
        s->pending_break = want;
}

// Only "pre" marks code: "pre-wrap" is common on ordinary prose (Google Docs
// spans, chat apps), which must not turn into a code block.
static int hm_style_is_pre(const char *ab, const char *ae) {
    char v[32];
    return hm_tag_style(ab, ae, "white-space", v, sizeof(v)) && strcmp(v, "pre") == 0;
}

// ---------------------------------------------------------------------------
// Tables: a GitHub-style pipe table in Markdown mode (header separator after
// the first row), tab-separated cells otherwise - what Excel's own plain text
// looks like. Excel's fragment carries bare <tr>/<td> with no <table>.
// ---------------------------------------------------------------------------
static void hm_cell_close(State *s) {
    hm_inline_close_written(s);
    s->cell = 0;
    s->pending_space = 0;
}

static void hm_cell_open(State *s) {
    if (!s->row_open) {
        hm_newline(s);
        if (s->full && !s->header_done && s->out.len > 0 && s->pending_break < 2)
            s->pending_break = 2;
        hm_open_line(s);
        s->row_open = 1;
        s->row_cells = 0;
    } else {
        hm_inline_close_written(s);
    }
    s->li_fresh = 0;
    if (s->full)
        hm_raw(s, s->row_cells ? " | " : "| ");
    else if (s->row_cells)
        buf_putc_cap(&s->out, '\t', hm_cap(s));
    s->row_cells++;
    s->cell = 1;
    s->at_bol = 0;
    s->pending_space = 0;
    s->trim_lead = 1; // the separator already carries the space
    s->lead = 2;
}

static void hm_end_row(State *s) {
    if (!s->row_open)
        return;
    if (s->cell)
        hm_cell_close(s);
    if (s->full) {
        hm_raw(s, " |");
        if (!s->header_done) {
            hm_newline(s);
            hm_open_line(s);
            hm_raw(s, "|");
            for (int i = 0; i < s->row_cells; i++)
                hm_raw(s, " --- |");
            s->header_done = 1;
        }
    }
    s->row_open = 0;
    hm_newline(s);
}

// ---------------------------------------------------------------------------
// Tag dispatch
// ---------------------------------------------------------------------------

// Block-level elements other than the ones with their own handling below:
// each starts and ends a line, like <div>.
static int hm_is_plain_block(const char *n, size_t l) {
    static const char *const tbl[] = { "div",     "section",    "article", "aside",  "header",
                                       "footer",  "nav",        "main",    "figure", "figcaption",
                                       "address", "details",    "summary", "dl",     "dt",
                                       "dd",      "fieldset",   "legend",  "form",   "center",
                                       "caption", "hgroup",     "menu",    "dir",    "noscript",
                                       "option",  "blockquote", "hr",      "ul",     "ol",
                                       "li",      "pre",        "p" };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (hm_ci_eq(n, l, tbl[i]))
            return 1;
    return l == 2 && (n[0] == 'h' || n[0] == 'H') && n[1] >= '1' && n[1] <= '6';
}

static int hm_is_table_tag(const char *n, size_t l) {
    return hm_ci_eq(n, l, "table") || hm_ci_eq(n, l, "tr") || hm_ci_eq(n, l, "td") ||
           hm_ci_eq(n, l, "th");
}

// Inline tags (Markdown mode only).
static void hm_inline_tag(State *s, const char *name, size_t nlen, int is_close, const char *ab,
                          const char *ae) {
#define EQ(lit) hm_ci_eq(name, nlen, lit)
    if (EQ("b") || EQ("strong")) {
        if (is_close) {
            hm_istack_pop(s, T_B);
            return;
        }
        // Google Docs wraps its whole payload in <b style="font-weight:normal"
        // id="docs-internal-guid-...">, which is not bold at all
        int bold = 1;
        char w[32];
        Buf id = { 0 };
        if (hm_get_attr(ab, ae, "id", &id) && id.len >= 18 &&
            memcmp(id.data, "docs-internal-guid", 18) == 0)
            bold = 0;
        buf_free(&id);
        if (bold && hm_tag_style(ab, ae, "font-weight", w, sizeof(w)) && hm_weight_is_bold(w) == 0)
            bold = 0;
        hm_istack_push(s, T_B, bold ? F_BOLD : 0);
        return;
    }
    if (EQ("i") || EQ("em")) {
        if (is_close) {
            hm_istack_pop(s, T_I);
            return;
        }
        char st[32];
        int italic =
            !(hm_tag_style(ab, ae, "font-style", st, sizeof(st)) && strcmp(st, "normal") == 0);
        hm_istack_push(s, T_I, italic ? F_ITALIC : 0);
        return;
    }
    if (EQ("span")) {
        if (is_close) {
            hm_istack_pop(s, T_SPAN);
            return;
        }
        // Google Docs and LibreOffice carry emphasis on styled spans
        int flags = 0;
        char v[32];
        if (hm_tag_style(ab, ae, "font-weight", v, sizeof(v)) && hm_weight_is_bold(v) == 1)
            flags |= F_BOLD;
        if (hm_tag_style(ab, ae, "font-style", v, sizeof(v)) &&
            (strcmp(v, "italic") == 0 || strcmp(v, "oblique") == 0))
            flags |= F_ITALIC;
        hm_istack_push(s, T_SPAN, flags);
        return;
    }
    if (EQ("code") || EQ("kbd") || EQ("samp") || EQ("tt")) {
        if (hm_in_pre(s))
            return; // handled by the fenced block
        if (is_close)
            hm_kind_pop(s, K_CODE);
        else
            hm_kind_push(s, K_CODE);
        return;
    }
    if (EQ("a")) {
        if (is_close) {
            hm_kind_pop(s, K_LINK);
            return;
        }
        // A nested <a> (invalid HTML) is ignored so the outer link stays
        // balanced; a link inside code stays code
        if (s->kcount[K_LINK] || s->kcount[K_CODE] || hm_in_pre(s))
            return;
        if (hm_get_attr(ab, ae, "href", &s->href)) {
            hm_clean_url(&s->href);
            if (s->href.len > 0)
                hm_kind_push(s, K_LINK);
        }
        return;
    }
    if (EQ("img")) {
        if (is_close)
            return;
        Buf src = { 0 }, alt = { 0 };
        hm_get_attr(ab, ae, "src", &src);
        hm_get_attr(ab, ae, "alt", &alt);
        hm_clean_url(&src);
        hm_clean_alt(&alt);
        if (src.len > 0 && !s->kcount[K_CODE] && !hm_in_pre(s)) {
            hm_begin_content(s);
            hm_raw(s, "![");
            for (size_t i = 0; i < alt.len; i++) {
                char c = alt.data[i];
                if (c == '[' || c == ']' || c == '\\' || (c == '|' && s->cell))
                    buf_putc_cap(&s->out, '\\', hm_cap(s));
                buf_putc_cap(&s->out, c, hm_cap(s));
            }
            hm_raw(s, "](");
            hm_put_url(s, src.data, src.len);
            buf_putc_cap(&s->out, ')', hm_cap(s));
            s->lead = 2;
        } else if (alt.len > 0) {
            hm_text_decoded(s, alt.data, alt.len);
        }
        buf_free(&src);
        buf_free(&alt);
        return;
    }
#undef EQ
}

static void hm_tag(State *s, const char *name, size_t nlen, int is_close, const char *ab,
                   const char *ae) {
    int full = s->full;
    int heading =
        nlen == 2 && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6';

#define EQ(lit) hm_ci_eq(name, nlen, lit)

    s->pre_skip_lf = 0; // only the LF directly after <pre> is dropped

    // Inside a preformatted block, block tags only break lines.
    if (hm_in_pre(s)) {
        if (EQ("pre")) {
            if (is_close) {
                if (s->pre > 0 && --s->pre == 0 && !hm_in_pre(s))
                    hm_pre_finish(s);
            } else {
                s->pre++;
            }
            return;
        }
        if (EQ("br")) {
            hm_pre_newline(s);
            return;
        }
        if (s->wsp && (s->wsp_p ? EQ("p") : EQ("div"))) {
            if (is_close && --s->wsp == 0) {
                if (!hm_in_pre(s))
                    hm_pre_finish(s);
                return;
            }
            if (!is_close)
                s->wsp++;
            if (!s->at_bol)
                hm_pre_newline(s);
            return;
        }
        if (hm_is_plain_block(name, nlen) || hm_is_table_tag(name, nlen)) {
            if (!s->at_bol)
                hm_pre_newline(s);
            return;
        }
        if (full)
            hm_inline_tag(s, name, nlen, is_close, ab, ae);
        return;
    }

    // Inside a table cell, block structure flattens to spaces.
    if (s->cell && !hm_is_table_tag(name, nlen) && (EQ("br") || hm_is_plain_block(name, nlen))) {
        s->pending_space = 1;
        return;
    }

    if (EQ("table") || EQ("tr") || EQ("td") || EQ("th")) {
        if (EQ("table") && s->cell) {
            if (!is_close)
                s->inner_tables++;
            else if (s->inner_tables > 0)
                s->inner_tables--;
            s->pending_space = 1;
            return;
        }
        if (s->inner_tables) {
            s->pending_space = 1; // a nested table's cells run together
            return;
        }
        if (EQ("table")) {
            hm_word_list_end(s);
            hm_end_row(s);
            hm_newline(s);
            s->header_done = 0;
            int want = full ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
        } else if (EQ("tr")) {
            hm_end_row(s);
        } else if (!is_close) {
            hm_word_list_end(s);
            hm_cell_open(s);
        } else if (s->cell) {
            hm_cell_close(s);
        }
        return;
    }

    // A Word list is a run of mso-list paragraphs; any other block ends it.
    int word_level = 0;
    if (EQ("p") && !is_close && s->mode != HTML_MD_PLAIN)
        word_level = hm_word_list_level(ab, ae);
    if (s->word_depth && !is_close && !word_level && hm_is_plain_block(name, nlen))
        hm_word_list_end(s);

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
    if (heading) {
        int level = name[1] - '0';
        if (is_close) {
            hm_newline(s);
            int want = full ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
        } else {
            if (!s->li_fresh)
                hm_newline(s); // "<li><h2>" keeps the heading on the marker line
            if (full) {
                hm_open_line(s);
                hm_fill(s, '#', (size_t) level);
                buf_putc_cap(&s->out, ' ', hm_cap(s));
                s->at_bol = 0;
                s->trim_lead = 1; // drop a leading space after "# "
                s->lead = 2;
                s->li_fresh = 0;
            }
        }
        return;
    }
    if (EQ("ul") || EQ("ol")) {
        if (is_close) {
            if (s->depth_lost > 0)
                s->depth_lost--;
            else if (s->depth > s->word_depth)
                s->depth--;
            hm_newline(s);
            int want = full ? 2 : 1; // a paragraph right after a list must not join its last item
            if (s->depth == 0 && s->pending_break < want)
                s->pending_break = want;
        } else if (s->depth < HM_MAX_DEPTH) {
            int d = s->depth;
            s->ordered[d] = EQ("ol") ? 1 : 0;
            s->counter[d] = 0;
            if (s->ordered[d])
                s->counter[d] = (int) hm_attr_long(ab, ae, "start", 1) - 1;
            s->content_col[d] = d > 0 ? s->content_col[d - 1] : 0;
            if (d == 0)
                s->top_items = 0;
            s->depth++;
        } else {
            s->depth_lost++;
        }
        return;
    }
    if (EQ("li")) {
        if (!is_close) {
            if (s->mode == HTML_MD_PLAIN) {
                hm_newline(s); // plain: list items on their own lines, no marker
            } else {
                int top = hm_lvl(s->depth) - 1;
                if (top >= 0 && s->ordered[top]) {
                    long v = hm_attr_long(ab, ae, "value", -1);
                    if (v >= 0)
                        s->counter[top] = (int) v - 1;
                }
                hm_open_li(s); // hm_open_li clamps depth 0 (bare <li>) safely
            }
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
        if (!is_close) {
            hm_pre_start(s);
            s->pre++;
            s->pre_skip_lf = 1;
        }
        return; // a stray </pre> outside one does nothing
    }
    if (EQ("hr")) {
        hm_newline(s);
        if (full) {
            // After a blank line, so "text\n---" cannot read as a heading
            if (s->out.len > 0 && s->pending_break < 2)
                s->pending_break = 2;
            hm_open_line(s);
            hm_raw(s, "---");
            s->lead = 2;
        }
        hm_newline(s);
        int want = full ? 2 : 1;
        if (s->pending_break < want)
            s->pending_break = want;
        return;
    }
    if (EQ("p") || hm_is_plain_block(name, nlen)) {
        if (is_close) {
            if (EQ("p") && s->word_li) {
                hm_newline(s); // items of a Word list stay tight
                s->word_li = 0;
                return;
            }
            hm_newline(s);
            int want = (full && EQ("p")) ? 2 : 1;
            if (s->pending_break < want)
                s->pending_break = want;
            return;
        }
        if ((EQ("p") || EQ("div")) && hm_style_is_pre(ab, ae)) {
            hm_pre_start(s);
            s->wsp = 1;
            s->wsp_p = EQ("p");
            return;
        }
        if (word_level) {
            hm_word_li(s, word_level, hm_word_list_ordered(s));
            return;
        }
        if (!s->at_bol && !s->li_fresh)
            hm_newline(s); // "<li><p>" keeps the item text on the marker line
        return;
    }
    if (full)
        hm_inline_tag(s, name, nlen, is_close, ab, ae); // inline formatting: FULL only
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
    s.full = (mode == HTML_MD_FULL);
    s.marker = (list_marker && list_marker[0]) ? list_marker : "- ";
    s.marker_len = (int) strlen(s.marker);
    s.at_bol = 1;

    const char *p = html_utf8;
    const char *end = html_utf8 + strlen(html_utf8);
    s.src_end = end;

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
                // Word / Outlook: <!--[if !supportLists]-->glyph<!--[endif]-->
                if (c && hm_find_ci(p + 4, c, "[if !supportlists]")) {
                    const char *ei = hm_find_ci(c, end, "<!--[endif]-->");
                    p = ei ? ei + 14 : c + 3;
                    continue;
                }
                p = c ? c + 3 : end;
            } else if (hm_ci_prefix(p, end, "<![if !supportlists]>")) {
                // Word's list number / bullet glyph: the list itself is
                // rebuilt from the paragraph's mso-list style
                const char *ei = hm_find_ci(p, end, "<![endif]>");
                p = ei ? ei + 10 : p + 21;
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
        const char *t = p + 1;
        int is_close = 0;
        if (t < end && *t == '/') {
            is_close = 1;
            t++;
        }
        const char *nb = t;
        if (t < end && hm_is_alpha(*t)) // a tag name starts with a letter
            while (t < end && hm_is_alnum(*t))
                t++;
        size_t nlen = (size_t) (t - nb);
        if (nlen == 0) {
            // e.g. "a < b" or "a<3" - not a tag; the '<' is text.
            s.pre_skip_lf = 0;
            if (hm_in_pre(&s))
                hm_char_raw(&s, '<');
            else
                hm_text_char(&s, '<', p + 1, (size_t) (end - p - 1));
            p++;
            continue;
        }
        const char *gt = hm_tag_end(p, end);
        if (!gt) {
            // No tag terminator: treat the stray '<' and the rest as text.
            hm_text(&s, p, (size_t) (end - p));
            p = end;
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
        s.src_next = gt + 1;
        hm_tag(&s, nb, nlen, is_close, t, gt);
        p = gt + 1;
    }

    // Close whatever is still open: a table row, inline delimiters on a live
    // line, an unterminated code block.
    hm_end_row(&s);
    if (!s.at_bol)
        hm_inline_close_written(&s);
    if (hm_in_pre(&s)) {
        s.pre = s.wsp = 0;
        hm_pre_finish(&s);
    }
    buf_free(&s.href);

    if (s.out.oom) {
        buf_free(&s.out);
        return NULL;
    }

    // Lists mode exists to turn lists into list lines. Anything else (code
    // from an editor, a terminal, table cells, plain paragraphs) is better
    // served by the plain text the source put on the clipboard, which keeps
    // indentation and columns that tag stripping cannot; an empty result
    // tells the caller to paste that instead.
    if (mode == HTML_MD_LISTS && !s.saw_li)
        s.out.len = 0;

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
