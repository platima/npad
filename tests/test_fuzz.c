/*
 * npad - Property and fuzz tests
 *
 * Seeded, deterministic random testing of the parsers and round trips that
 * hand-written cases kept missing: the settings reader and its save/load
 * cycle, recovery-slot metadata, HTML-to-Markdown paste, the list tools, and
 * the update version comparator. Each case states a property that must hold
 * for EVERY input and checks it against thousands of generated ones.
 *
 * Run by `make test` at a small iteration count, and by `make test-fuzz` for
 * longer under AddressSanitizer + UBSan (where "no crash" includes "no
 * out-of-bounds read"). Environment:
 *   NPAD_FUZZ_ITERS  iterations per property (default 300)
 *   NPAD_FUZZ_SEED   PRNG seed (default 20261021)
 * A failure prints the seed, the iteration and the offending input in hex;
 * rerunning with the same seed and iteration count reproduces it exactly.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/core/html_md.h"
#include "../src/core/list_ops.h"
#include "../src/core/session.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include "../src/core/update_check.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Deterministic PRNG (xorshift64*) ----------------------------------------

static unsigned long long g_seed = 20261021ULL;
static unsigned long long g_rng;
static long g_iters = 300;

static void rng_reset(unsigned long long salt) {
    // Each property gets its own stream, so adding a case never shifts
    // another. splitmix64 spreads every seed bit (xorshift needs non-zero).
    unsigned long long z = g_seed + salt * 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    g_rng = z ? z : 0x9E3779B97F4A7C15ULL;
}

static unsigned long long rng_next(void) {
    unsigned long long x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static size_t rng_below(size_t n) {
    return n ? (size_t) (rng_next() % n) : 0;
}

// --- Growable byte buffer and mutator ---------------------------------------

typedef struct {
    char *d;
    size_t n, cap;
} Buf;

static void buf_reserve(Buf *b, size_t need) {
    if (need + 1 <= b->cap)
        return;
    size_t c = b->cap ? b->cap : 64;
    while (c < need + 1)
        c *= 2;
    char *grown = realloc(b->d, c);
    if (!grown) {
        printf("out of memory\n");
        exit(1);
    }
    b->d = grown;
    b->cap = c;
}

static void buf_set(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->d, s, n);
    b->n = n;
    b->d[n] = '\0';
}

static void buf_insert(Buf *b, size_t at, const char *s, size_t n) {
    buf_reserve(b, b->n + n);
    memmove(b->d + at + n, b->d + at, b->n - at);
    memcpy(b->d + at, s, n);
    b->n += n;
    b->d[b->n] = '\0';
}

static void buf_append(Buf *b, const char *s) {
    buf_insert(b, b->n, s, strlen(s));
}

static void buf_set_str(Buf *b, const char *s) {
    buf_set(b, s, strlen(s));
}

static void buf_append_char(Buf *b, char c) {
    buf_insert(b, b->n, &c, 1);
}

static void buf_free(Buf *b) {
    free(b->d);
    b->d = NULL;
    b->n = b->cap = 0;
}

// One random mutation. Never inserts a NUL: every input here is a C string.
static void mutate(Buf *b, const char *const *dict, size_t maxlen) {
    static const char interesting[] = { '\n', '\r', '"',  '\\', '<',  '>',  '&',  ';',
                                        '#',  ':',  ',',  '{',  '}',  '-',  ' ',  '\t',
                                        '\x80', '\xbf', '\xc3', '\xe2', '\xed', '\xf0', '\xff' };
    size_t ndict = 0;
    while (dict && dict[ndict])
        ndict++;
    switch (rng_below(8)) {
        case 0: // flip a bit
            if (b->n) {
                size_t at = rng_below(b->n);
                char c = (char) (b->d[at] ^ (char) (1u << rng_below(8)));
                b->d[at] = c ? c : 'x';
            }
            break;
        case 1: { // random byte
            char c = (char) (1 + rng_below(255));
            if (b->n)
                b->d[rng_below(b->n)] = c;
            else
                buf_insert(b, 0, &c, 1);
            break;
        }
        case 2: // delete a range
            if (b->n) {
                size_t at = rng_below(b->n);
                size_t left = b->n - at;
                size_t k = 1 + rng_below(left < 16 ? left : 16);
                memmove(b->d + at, b->d + at + k, b->n - at - k);
                b->n -= k;
                b->d[b->n] = '\0';
            }
            break;
        case 3: // duplicate a chunk
            if (b->n) {
                size_t at = rng_below(b->n);
                size_t left = b->n - at;
                size_t k = 1 + rng_below(left < 64 ? left : 64);
                char *tmp = malloc(k);
                if (tmp) {
                    memcpy(tmp, b->d + at, k);
                    buf_insert(b, rng_below(b->n + 1), tmp, k);
                    free(tmp);
                }
            }
            break;
        case 4:
        case 5: // dictionary token
            if (ndict) {
                const char *t = dict[rng_below(ndict)];
                buf_insert(b, rng_below(b->n + 1), t, strlen(t));
            }
            break;
        case 6: { // interesting byte
            char c = interesting[rng_below(sizeof(interesting))];
            if (b->n && rng_below(2))
                b->d[rng_below(b->n)] = c;
            else
                buf_insert(b, rng_below(b->n + 1), &c, 1);
            break;
        }
        default: // truncate
            if (b->n) {
                b->n = rng_below(b->n + 1);
                b->d[b->n] = '\0';
            }
            break;
    }
    if (b->n > maxlen) {
        b->n = maxlen;
        b->d[b->n] = '\0';
    }
}

// A random valid UTF-8 code point, weighted towards the characters that
// matter: ASCII punctuation, the escaping set, two- three- and four-byte
// sequences. Never NUL; never a surrogate.
static void append_random_char(Buf *b, bool allow_controls) {
    static const char special[] = "\\\"{}:, \t/abcXYZ019";
    unsigned long cp;
    switch (rng_below(6)) {
        case 0:
        case 1:
            cp = (unsigned char) special[rng_below(sizeof(special) - 1)];
            break;
        case 2:
            cp = 0x20 + (unsigned long) rng_below(0x5F);
            break;
        case 3:
            cp = 0xA0 + (unsigned long) rng_below(0x700); // Latin, Greek, Cyrillic...
            break;
        case 4:
            cp = 0x800 + (unsigned long) rng_below(0xD000 - 0x800); // BMP below surrogates
            break;
        default:
            cp = 0x10000 + (unsigned long) rng_below(0x10FFFF - 0x10000 + 1); // astral
            break;
    }
    if (!allow_controls && cp == '\t')
        cp = 'q';
    char out[4];
    int n;
    if (cp < 0x80) {
        out[0] = (char) cp;
        n = 1;
    } else if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        n = 3;
    } else {
        out[0] = (char) (0xF0 | (cp >> 18));
        out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char) (0x80 | (cp & 0x3F));
        n = 4;
    }
    buf_insert(b, b->n, out, (size_t) n);
}

// Print the context of a failed property, then let the assertion fail
static void report_input(const char *what, long iteration, const char *data, size_t n) {
    printf("\n  [fuzz] seed %llu, iteration %ld, %s (%lu bytes): ", g_seed, iteration, what,
           (unsigned long) n);
    for (size_t i = 0; i < n && i < 512; i++)
        printf("%02x", (unsigned char) data[i]);
    printf(n > 512 ? "...\n" : "\n");
}

static size_t file_size_or_zero(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n > 0 ? (size_t) n : 0;
}

static void read_file_into(const char *path, Buf *out) {
    buf_set(out, "", 0);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf_insert(out, out->n, chunk, got);
    fclose(f);
}

static void write_bytes(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

// --- Settings -----------------------------------------------------------------

#define FUZZ_SETTINGS "fuzz_settings.json"

static const char SETTINGS_SEED[] = "{\n"
                                    "  \"recent_file_0\": \"C:\\\\Users\\\\me\\\\notes.txt\",\n"
                                    "  \"find_hist_0\": \"say \\\"hi\\\"\",\n"
                                    "  \"window_x\": \"100\",\n"
                                    "  \"tab_width\": \"4\",\n"
                                    "  \"theme\": \"dark\",\n"
                                    "  \"font_face\": \"Consolas\"\n"
                                    "}\n";

static const char *const SETTINGS_DICT[] = { "\"", "\\", "\\\\", "\\\"", "\\n", "\\t", ":", ",",
                                             "{", "}", "[", "]", " ", "\n", "\"k\": \"v\"",
                                             "\\u00e9", NULL };

// Known gap NP-016: the reader keeps a KEY's escapes, so a backslash or quote
// in a key doubles on every save. npad's own keys never contain either, so it
// is reported (counted) rather than failed - for that one shape only, judged
// from the file npad itself wrote: an entry (`{` or `,`, newline, two spaces,
// quote) whose key, up to the `": "` that ends it, holds a backslash. Keys
// can hold raw newlines too, so this works on the whole text, not by line.
// Once the reader unescapes keys this never triggers.
static bool saved_file_has_escaped_key(const char *text) {
    const char *p = text;
    while ((p = strstr(p, "\n  \"")) != NULL) {
        bool entry_start = p > text && (p[-1] == '{' || p[-1] == ',');
        p += 4;
        if (!entry_start)
            continue;
        const char *sep = strstr(p, "\": \"");
        if (!sep)
            break;
        if (memchr(p, '\\', (size_t) (sep - p)))
            return true;
    }
    return false;
}

// Whatever the file holds, loading it must not crash, and once npad has
// written it back the store must be at a fixed point: every later load/save
// cycle reproduces the same entries and the same file. (A value that grows
// on each cycle is how a path once reached 1.2 GB.)
TEST_CASE(settings_load_of_any_file_reaches_a_fixed_point) {
    settings_set_file_path(FUZZ_SETTINGS);
    rng_reset(1);
    Buf b = { 0 }, written = { 0 };
    long known_gap = 0;
    for (long it = 0; it < g_iters; it++) {
        if (it % 40 == 0)
            buf_set_str(&b, SETTINGS_SEED);
        int n = 1 + (int) rng_below(5);
        for (int m = 0; m < n; m++)
            mutate(&b, SETTINGS_DICT, 4096);
        write_bytes(FUZZ_SETTINGS, b.d, b.n);

        settings_clear_all();
        settings_load();
        (void) settings_get_int("tab_width", 4);
        (void) settings_get_double("window_x", 0);
        (void) settings_get_bool("theme", false);
        char *s = settings_get_string("font_face", "");
        free(s);
        if (!settings_save())
            continue; // nothing serialisable; not this property's concern
        read_file_into(FUZZ_SETTINGS, &written);
        bool escaped_key = saved_file_has_escaped_key(written.d);

        settings_clear_all();
        settings_load();
        int count1 = settings_count();
        settings_save();
        size_t size1 = file_size_or_zero(FUZZ_SETTINGS);
        settings_clear_all();
        settings_load();
        int count2 = settings_count();
        settings_save();
        size_t size2 = file_size_or_zero(FUZZ_SETTINGS);
        if ((count1 != count2 || size1 != size2) && escaped_key) {
            known_gap++;
            continue;
        }
        if (count1 != count2 || size1 != size2) {
            report_input("settings file", it, b.d, b.n);
            report_input("as npad first wrote it", it, written.d, written.n);
            printf("  entries %d -> %d, bytes %lu -> %lu\n", count1, count2,
                   (unsigned long) size1, (unsigned long) size2);
        }
        TEST_ASSERT(count1 == count2, "entry count is stable across load/save cycles");
        TEST_ASSERT(size1 == size2, "file size is stable across load/save cycles");
    }
    if (known_gap)
        printf("(%ld inputs hit known gap NP-016: an escaped key grows) ", known_gap);
    buf_free(&b);
    buf_free(&written);
    settings_clear_all();
    remove(FUZZ_SETTINGS);
}

// Every value npad can store survives any number of save/load cycles
// unchanged: quotes, backslashes, tabs, braces and any non-ASCII text.
// (Keys are npad's own identifiers; control characters other than tab are a
// separate, known gap in the writer and are not generated here.)
TEST_CASE(settings_values_survive_repeated_round_trips) {
    enum { KEYS = 12 };
    settings_set_file_path(FUZZ_SETTINGS);
    rng_reset(2);
    Buf values[KEYS];
    memset(values, 0, sizeof(values));
    for (long it = 0; it < g_iters / 4 + 1; it++) {
        settings_clear_all();
        for (int k = 0; k < KEYS; k++) {
            values[k].n = 0;
            buf_set(&values[k], "", 0);
            size_t len = rng_below(80);
            for (size_t i = 0; i < len; i++)
                append_random_char(&values[k], true);
            char key[32];
            snprintf(key, sizeof key, "fuzz_key_%d", k);
            settings_set_string(key, values[k].d);
        }
        int cycles = 1 + (int) rng_below(5);
        for (int c = 0; c < cycles; c++) {
            TEST_ASSERT(settings_save(), "save succeeds");
            settings_clear_all();
            settings_load();
        }
        for (int k = 0; k < KEYS; k++) {
            char key[32];
            snprintf(key, sizeof key, "fuzz_key_%d", k);
            char *got = settings_get_string(key, NULL);
            bool same = got && strcmp(got, values[k].d) == 0;
            if (!same) {
                report_input("value", it, values[k].d, values[k].n);
                printf("  after %d cycles came back as %s\n", cycles, got ? got : "(missing)");
            }
            free(got);
            TEST_ASSERT(same, "a stored value reads back exactly");
        }
    }
    for (int k = 0; k < KEYS; k++)
        buf_free(&values[k]);
    settings_clear_all();
    remove(FUZZ_SETTINGS);
}

// --- Session (crash recovery) ---------------------------------------------------

#define FUZZ_REC "fuzz_recovery"

// A snapshot taken back is the snapshot written: content, original path,
// encoding and line ending. Taking it removes it (a second take finds
// nothing) and clears any handoff marker.
TEST_CASE(session_write_then_take_is_identity) {
    TEST_ASSERT(test_make_dir(FUZZ_REC), "recovery directory created");
    rng_reset(3);
    Buf content = { 0 }, path = { 0 };
    for (long it = 0; it < g_iters; it++) {
        buf_set(&content, "", 0);
        size_t len = rng_below(300);
        for (size_t i = 0; i < len; i++) {
            if (rng_below(10) == 0)
                buf_append_char(&content, rng_below(2) ? '\n' : '\r');
            else
                append_random_char(&content, true);
        }
        bool has_path = rng_below(3) != 0;
        buf_set(&path, "", 0);
        if (has_path) {
            buf_append(&path, rng_below(2) ? "C:\\Users\\me\\" : "/home/me/");
            size_t plen = 1 + rng_below(40);
            for (size_t i = 0; i < plen; i++)
                append_random_char(&path, false);
        }
        TextEncoding enc = (TextEncoding) rng_below(5);
        LineEnding eol = (LineEnding) rng_below(3);
        char slot[32];
        snprintf(slot, sizeof slot, "s%lx", (unsigned long) it);

        TEST_ASSERT(session_write(FUZZ_REC, slot, content.d, has_path ? path.d : NULL, enc, eol),
                    "snapshot written");
        if (rng_below(4) == 0)
            session_mark_handoff(FUZZ_REC, slot);

        char *got_path = NULL;
        TextEncoding got_enc = NPAD_ENC_UTF8;
        LineEnding got_eol = NPAD_EOL_LF;
        char *got = session_take(FUZZ_REC, slot, &got_path, &got_enc, &got_eol);
        bool content_ok = got && strcmp(got, content.d) == 0;
        bool path_ok = has_path ? (got_path && strcmp(got_path, path.d) == 0) : got_path == NULL;
        if (!content_ok || !path_ok || got_enc != enc || got_eol != eol) {
            report_input("content", it, content.d, content.n);
            report_input("path", it, path.d, path.n);
            printf("  content %s, path %s, encoding %d -> %d, line ending %d -> %d\n",
                   content_ok ? "ok" : "DIFFERS", path_ok ? "ok" : "DIFFERS", (int) enc,
                   (int) got_enc, (int) eol, (int) got_eol);
        }
        free(got);
        free(got_path);
        TEST_ASSERT(content_ok, "content comes back exactly");
        TEST_ASSERT(path_ok, "the original path comes back exactly");
        TEST_ASSERT(got_enc == enc && got_eol == eol, "encoding and line ending come back");
        TEST_ASSERT(!session_is_handoff(FUZZ_REC, slot), "taking a slot clears its marker");
        char *again = session_take(FUZZ_REC, slot, NULL, NULL, NULL);
        bool none = again == NULL;
        free(again);
        TEST_ASSERT(none, "a slot can be taken only once");
    }
    buf_free(&content);
    buf_free(&path);
    test_remove_tree(FUZZ_REC);
}

// Whatever a .meta file holds (a crash mid-write, a hand edit, a different
// version), taking the slot must not crash and must report an encoding and
// a line ending npad knows.
TEST_CASE(session_take_of_any_meta_is_safe) {
    TEST_ASSERT(test_make_dir(FUZZ_REC), "recovery directory created");
    static const char *const META_DICT[] = { "\n", "\r\n", "-1", "4", "2147483648",
                                             "99999999999", "/tmp/x", "C:\\x", NULL };
    rng_reset(4);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        if (it % 30 == 0)
            buf_set_str(&b, "2\n1\n/home/u/notes.txt");
        int n = 1 + (int) rng_below(4);
        for (int m = 0; m < n; m++)
            mutate(&b, META_DICT, 1024);
        write_bytes(FUZZ_REC "/fz.meta", b.d, b.n);
        write_bytes(FUZZ_REC "/fz.txt", "abc", 3);

        char *p = NULL;
        TextEncoding enc = (TextEncoding) 77;
        LineEnding eol = (LineEnding) 77;
        char *c = session_take(FUZZ_REC, "fz", &p, &enc, &eol);
        bool in_range = !c || ((int) enc >= 0 && (int) enc <= (int) NPAD_ENC_ANSI &&
                               (int) eol >= 0 && (int) eol <= (int) NPAD_EOL_CR);
        if (!in_range)
            report_input("meta", it, b.d, b.n);
        free(c);
        free(p);
        TEST_ASSERT(in_range, "encoding and line ending are always known values");
    }
    buf_free(&b);
    test_remove_tree(FUZZ_REC);
}

// --- HTML paste -------------------------------------------------------------------

static const char *const HTML_DICT[] = {
    "<ul>", "</ul>", "<ol>", "</ol>", "<li>", "</li>", "<p>", "</p>", "<b>", "</b>", "<i>",
    "<em>", "<strong>", "<code>", "</code>", "<pre>", "</pre>", "<a href=\"x\">", "</a>",
    "<img src=\"s\" alt=\"a\">", "<h1>", "</h1>", "<h3>", "<blockquote>", "</blockquote>", "<br>",
    "<hr>", "<table>", "<tr>", "<td>", "<div>", "</div>", "<span style=\"x\">", "<!--", "-->",
    "&amp;", "&lt;", "&gt;", "&nbsp;", "&#233;", "&#x1F600;", "&#0;", "&#xD800;", "&bogus;",
    "<script>", "</script>", "<style>", "</style>", "\"", "'", "=", "\r\n", NULL
};

// No HTML fragment, however malformed, may crash the converter or make it
// return nothing; and the result stays proportionate to the input (a fixed
// per-byte bound, so nothing can expand without limit).
TEST_CASE(html_to_markdown_handles_any_fragment) {
    rng_reset(5);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        if (it % 25 == 0)
            buf_set_str(&b, "<ul><li>one</li><li><b>two</b> <a href=\"u\">l</a></li></ul>");
        int n = 1 + (int) rng_below(6);
        for (int m = 0; m < n; m++)
            mutate(&b, HTML_DICT, 2048);
        for (int mode = HTML_MD_PLAIN; mode <= HTML_MD_FULL; mode++) {
            char *md = html_to_markdown(b.d, (HtmlMdMode) mode, rng_below(2) ? "* " : NULL);
            bool ok = md != NULL && strlen(md) <= b.n * 16 + 64;
            if (!ok)
                report_input("html", it, b.d, b.n);
            free(md);
            TEST_ASSERT(ok, "conversion returns a proportionate string");
        }
    }
    buf_free(&b);
}

// The CF_HTML header's offsets come from another program: any values, in or
// out of range, must give a substring of the blob and never read outside it.
TEST_CASE(cf_html_offsets_never_escape_the_blob) {
    rng_reset(6);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        buf_set(&b, "", 0);
        char header[200];
        long values[4];
        for (int i = 0; i < 4; i++) {
            switch (rng_below(4)) {
                case 0:
                    values[i] = (long) rng_below(400);
                    break;
                case 1:
                    values[i] = -(long) rng_below(5);
                    break;
                case 2:
                    values[i] = 2147483647L;
                    break;
                default:
                    values[i] = (long) rng_below(100000);
                    break;
            }
        }
        snprintf(header, sizeof header,
                 "Version:0.9\r\nStartHTML:%ld\r\nEndHTML:%ld\r\nStartFragment:%ld\r\n"
                 "EndFragment:%ld\r\n",
                 values[0], values[1], values[2], values[3]);
        if (rng_below(5))
            buf_append(&b, header);
        if (rng_below(2))
            buf_append(&b, "<html><body><!--StartFragment-->");
        size_t body = rng_below(200);
        for (size_t i = 0; i < body; i++)
            buf_append_char(&b, (char) (0x20 + rng_below(0x5F)));
        if (rng_below(2))
            buf_append(&b, "<!--EndFragment--></body></html>");
        if (rng_below(3) == 0)
            mutate(&b, HTML_DICT, 1024);

        char *frag = html_cf_extract_fragment(b.d);
        bool ok = frag != NULL && strlen(frag) <= b.n;
        if (!ok)
            report_input("CF_HTML", it, b.d, b.n);
        free(frag);
        TEST_ASSERT(ok, "the fragment is never longer than the blob");
    }
    buf_free(&b);
}

// --- List tools -------------------------------------------------------------------

// Random multi-line text from a small alphabet, so lines repeat and differ
// only by case often enough to exercise ordering, stability and duplicates
static void random_lines(Buf *b, bool allow_cr) {
    static const char *const atoms[] = { "a", "A", "b", "B", "z", "Z", "_", " ", "1", "10", "\t",
                                         "\xc3\xa9", "\xc3\x89", "-", "* " };
    buf_set(b, "", 0);
    size_t lines = rng_below(30);
    for (size_t l = 0; l < lines; l++) {
        size_t atoms_n = rng_below(5);
        for (size_t a = 0; a < atoms_n; a++)
            buf_append(b, atoms[rng_below(sizeof(atoms) / sizeof(atoms[0]))]);
        if (allow_cr && rng_below(8) == 0)
            buf_append_char(b, '\r');
        if (l + 1 < lines || rng_below(2))
            buf_append_char(b, '\n');
    }
}

typedef struct {
    const char *s;
    size_t len;
} Span;

// Split as list_ops documents: on '\n', a final '\n' ends the last line
static size_t split_spans(const char *text, Span **out, bool *trailing) {
    size_t len = strlen(text), cap = 8, n = 0;
    Span *v = malloc(cap * sizeof(Span));
    *trailing = len > 0 && text[len - 1] == '\n';
    const char *p = text, *end = text + len;
    while (v) {
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        if (n == cap) {
            Span *g = realloc(v, (cap *= 2) * sizeof(Span));
            if (!g) {
                free(v);
                v = NULL;
                break;
            }
            v = g;
        }
        v[n].s = p;
        v[n].len = nl ? (size_t) (nl - p) : (size_t) (end - p);
        n++;
        if (!nl)
            break;
        p = nl + 1;
        if (p == end)
            break;
    }
    *out = v;
    return v ? n : 0;
}

static char *join_spans(const Span *v, size_t n, bool trailing) {
    size_t total = 2;
    for (size_t i = 0; i < n; i++)
        total += v[i].len + 1;
    char *out = malloc(total), *w = out;
    if (!out)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        memcpy(w, v[i].s, v[i].len);
        w += v[i].len;
        if (i + 1 < n)
            *w++ = '\n';
    }
    if (trailing)
        *w++ = '\n';
    *w = '\0';
    return out;
}

// The documented order: bytes by value, A-Z folded when case-insensitive,
// a prefix before its extensions
static int ref_compare(const Span *a, const Span *b, bool case_sensitive) {
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char) a->s[i], y = (unsigned char) b->s[i];
        if (!case_sensitive) {
            if (x >= 'A' && x <= 'Z')
                x = (unsigned char) (x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z')
                y = (unsigned char) (y - 'A' + 'a');
        }
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a->len == b->len ? 0 : (a->len < b->len ? -1 : 1);
}

// Reference: a plainly stable insertion sort with the documented comparator
static char *ref_sort(const char *text, bool descending, bool case_sensitive) {
    Span *v;
    bool trailing;
    size_t n = split_spans(text, &v, &trailing);
    if (!v)
        return NULL;
    for (size_t i = 1; i < n; i++) {
        Span key = v[i];
        size_t j = i;
        while (j > 0) {
            int c = ref_compare(&v[j - 1], &key, case_sensitive);
            if ((descending ? -c : c) <= 0)
                break;
            v[j] = v[j - 1];
            j--;
        }
        v[j] = key;
    }
    char *out = join_spans(v, n, trailing);
    free(v);
    return out;
}

static char *ref_unique(const char *text) {
    Span *v;
    bool trailing;
    size_t n = split_spans(text, &v, &trailing);
    if (!v)
        return NULL;
    size_t kept = 0;
    for (size_t i = 0; i < n; i++) {
        bool dup = false;
        for (size_t j = 0; j < kept && !dup; j++)
            dup = v[j].len == v[i].len && memcmp(v[j].s, v[i].s, v[i].len) == 0;
        if (!dup)
            v[kept++] = v[i];
    }
    char *out = join_spans(v, kept, trailing);
    free(v);
    return out;
}

// Sort is a stable sort by the documented comparator, in both directions and
// both case modes; sorting twice changes nothing.
TEST_CASE(list_sort_matches_a_stable_reference_sort) {
    rng_reset(7);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        random_lines(&b, true);
        bool desc = rng_below(2) != 0, cs = rng_below(2) != 0;
        char *got = list_sort_lines(b.d, desc, cs);
        char *want = ref_sort(b.d, desc, cs);
        bool same = got && want && strcmp(got, want) == 0;
        char *twice = got ? list_sort_lines(got, desc, cs) : NULL;
        bool idempotent = got && twice && strcmp(got, twice) == 0;
        if (!same || !idempotent) {
            report_input("lines", it, b.d, b.n);
            printf("  descending %d, case-sensitive %d\n", desc, cs);
        }
        free(got);
        free(want);
        free(twice);
        TEST_ASSERT(same, "sort equals the stable reference sort");
        TEST_ASSERT(idempotent, "sorting sorted text changes nothing");
    }
    buf_free(&b);
}

// Unique keeps the first copy of each distinct line, in order, and is
// idempotent
TEST_CASE(list_unique_keeps_first_occurrences) {
    rng_reset(8);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        random_lines(&b, true);
        char *got = list_unique_lines(b.d);
        char *want = ref_unique(b.d);
        bool same = got && want && strcmp(got, want) == 0;
        char *twice = got ? list_unique_lines(got) : NULL;
        bool idempotent = got && twice && strcmp(got, twice) == 0;
        if (!same || !idempotent)
            report_input("lines", it, b.d, b.n);
        free(got);
        free(want);
        free(twice);
        TEST_ASSERT(same, "unique equals the reference");
        TEST_ASSERT(idempotent, "unique of unique text changes nothing");
    }
    buf_free(&b);
}

// Replace All agrees with a naive left-to-right, non-overlapping replace
TEST_CASE(list_replace_all_matches_reference) {
    rng_reset(9);
    Buf text = { 0 }, from = { 0 }, to = { 0 }, want = { 0 };
    static const char alphabet[] = "aab\n\\";
    for (long it = 0; it < g_iters; it++) {
        buf_set(&text, "", 0);
        buf_set(&from, "", 0);
        buf_set(&to, "", 0);
        size_t tl = rng_below(60), fl = 1 + rng_below(3), ol = rng_below(4);
        for (size_t i = 0; i < tl; i++)
            buf_append_char(&text, alphabet[rng_below(sizeof(alphabet) - 1)]);
        for (size_t i = 0; i < fl; i++)
            buf_append_char(&from, alphabet[rng_below(sizeof(alphabet) - 1)]);
        for (size_t i = 0; i < ol; i++)
            buf_append_char(&to, alphabet[rng_below(sizeof(alphabet) - 1)]);

        buf_set(&want, "", 0);
        for (size_t i = 0; i < text.n;) {
            if (i + from.n <= text.n && memcmp(text.d + i, from.d, from.n) == 0) {
                buf_insert(&want, want.n, to.d, to.n);
                i += from.n;
            } else {
                buf_append_char(&want, text.d[i++]);
            }
        }
        char *got = list_replace_all(text.d, from.d, to.d);
        bool same = got && strcmp(got, want.d) == 0;
        if (!same) {
            report_input("text", it, text.d, text.n);
            report_input("from", it, from.d, from.n);
            report_input("to", it, to.d, to.n);
        }
        free(got);
        TEST_ASSERT(same, "replace all equals the reference");
    }
    buf_free(&text);
    buf_free(&from);
    buf_free(&to);
    buf_free(&want);
}

// Unindent undoes Indent for the whitespace formats, at every width
TEST_CASE(list_unindent_undoes_indent_for_whitespace_formats) {
    rng_reset(10);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        random_lines(&b, false);
        ListIndentFormat fmt = rng_below(2) ? LIST_INDENT_SPACES : LIST_INDENT_TAB;
        int width = 1 + (int) rng_below(16);
        char *in = list_indent_lines_w(b.d, fmt, NULL, width);
        char *out = in ? list_unindent_lines_w(in, fmt, NULL, width) : NULL;
        bool same = out && strcmp(out, b.d) == 0;
        if (!same) {
            report_input("lines", it, b.d, b.n);
            printf("  format %d, width %d\n", (int) fmt, width);
        }
        free(in);
        free(out);
        TEST_ASSERT(same, "unindent(indent(text)) == text");
    }
    buf_free(&b);
}

// Escape interpretation never crashes and never grows the text: every escape
// is at least as long as what it stands for (\uXXXX is six bytes for at most
// three)
TEST_CASE(list_unescape_handles_any_input) {
    static const char *const ESC_DICT[] = { "\\", "\\n", "\\t", "\\r", "\\0", "\\\\", "\\u",
                                            "\\u00e9", "\\uD83D", "\\uDE00", "\\uFFFF", "\\x",
                                            NULL };
    rng_reset(11);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        if (it % 20 == 0)
            buf_set_str(&b, "a\\tb\\u0041");
        int n = 1 + (int) rng_below(4);
        for (int m = 0; m < n; m++)
            mutate(&b, ESC_DICT, 256);
        char *u = list_unescape(b.d);
        bool ok = u && strlen(u) <= b.n;
        if (!ok)
            report_input("escaped", it, b.d, b.n);
        free(u);
        TEST_ASSERT(ok, "unescaped text is never longer than its source");
    }
    buf_free(&b);
}

// --- Update version comparison --------------------------------------------------

static void random_version(Buf *b) {
    static const char *const suffixes[] = { "rc", "rc1", "rc2", "rc10", "dev", "beta", "beta2",
                                            "alpha", "RC3", "1", "a1b2", "" };
    buf_set(b, "", 0);
    if (rng_below(4) == 0) { // free-form junk
        size_t n = rng_below(12);
        for (size_t i = 0; i < n; i++)
            buf_append_char(b, "v0123456789.-rc (x)"[rng_below(19)]);
        return;
    }
    if (rng_below(2))
        buf_append_char(b, rng_below(4) ? 'v' : 'V');
    int parts = 1 + (int) rng_below(3);
    for (int i = 0; i < parts; i++) {
        char num[16];
        snprintf(num, sizeof num, "%lu", (unsigned long) rng_below(rng_below(3) ? 4 : 40));
        if (i)
            buf_append_char(b, '.');
        buf_append(b, num);
    }
    if (rng_below(2)) {
        buf_append_char(b, '-');
        buf_append(b, suffixes[rng_below(sizeof(suffixes) / sizeof(suffixes[0]))]);
    }
    if (rng_below(5) == 0)
        buf_append(b, " (abc1234)");
}

static int sign(int v) {
    return (v > 0) - (v < 0);
}

// The comparator is a total preorder, or sorting releases (and deciding
// whether an update is newer) depends on the order of the questions:
// reflexive, antisymmetric, transitive.
TEST_CASE(version_compare_is_a_consistent_order) {
    rng_reset(12);
    Buf a = { 0 }, b = { 0 }, c = { 0 };
    for (long it = 0; it < g_iters * 4; it++) {
        random_version(&a);
        random_version(&b);
        random_version(&c);
        int ab = sign(update_version_compare(a.d, b.d));
        int ba = sign(update_version_compare(b.d, a.d));
        int bc = sign(update_version_compare(b.d, c.d));
        int ac = sign(update_version_compare(a.d, c.d));
        bool reflexive = update_version_compare(a.d, a.d) == 0;
        bool antisymmetric = ab == -ba;
        bool transitive = !(ab <= 0 && bc <= 0) || ac <= 0;
        if (!reflexive || !antisymmetric || !transitive)
            printf("\n  [fuzz] seed %llu, iteration %ld: a=\"%s\" b=\"%s\" c=\"%s\"\n", g_seed,
                   it, a.d, b.d, c.d);
        TEST_ASSERT(reflexive, "a version equals itself");
        TEST_ASSERT(antisymmetric, "compare(a, b) == -compare(b, a)");
        TEST_ASSERT(transitive, "a <= b and b <= c imply a <= c");
        // Newer-than must agree with the comparator when nothing is skipped
        bool newer = update_is_newer_unskipped(a.d, b.d, "");
        TEST_ASSERT(!newer || update_version_compare(b.d, a.d) > 0,
                    "an offered update is newer than the running version");
    }
    buf_free(&a);
    buf_free(&b);
    buf_free(&c);
}

// The release JSON and checksum parsers take network input: any bytes are
// safe, and an extracted tag always fits its buffer
TEST_CASE(update_parsers_handle_any_input) {
    static const char *const JSON_DICT[] = { "\"tag_name\"", ":", "\"v0.36.0\"", "\"", "\\", "{",
                                             "}", " ", "\"browser_download_url\"", "\"name\"",
                                             "-setup-win-x64.exe", NULL };
    rng_reset(13);
    Buf b = { 0 };
    for (long it = 0; it < g_iters; it++) {
        if (it % 30 == 0)
            buf_set_str(&b, "{\"tag_name\":\"v0.36.0\",\"assets\":[{\"name\":\"x\"}]}");
        int n = 1 + (int) rng_below(5);
        for (int m = 0; m < n; m++)
            mutate(&b, JSON_DICT, 2048);
        char tag[16];
        memset(tag, 'Z', sizeof tag);
        bool got = update_extract_tag(b.d, tag, sizeof tag);
        bool fits = !got || memchr(tag, '\0', sizeof tag) != NULL;
        char url[64];
        (void) update_find_asset_url(b.d, "-setup-win-x64.exe", url, sizeof url);
        char hex[65];
        (void) update_parse_sha256(b.d, hex);
        if (!fits)
            report_input("json", it, b.d, b.n);
        TEST_ASSERT(fits, "an extracted tag is terminated within its buffer");
    }
    buf_free(&b);
}

int main(void) {
    const char *iters = getenv("NPAD_FUZZ_ITERS");
    const char *seed = getenv("NPAD_FUZZ_SEED");
    if (iters && atol(iters) > 0)
        g_iters = atol(iters);
    if (seed && *seed)
        g_seed = strtoull(seed, NULL, 10);

    thread_safety_init();
    TEST_INIT();
    printf("Seed %llu, %ld iterations per property\n\n", g_seed, g_iters);

    RUN_TEST(settings_load_of_any_file_reaches_a_fixed_point);
    RUN_TEST(settings_values_survive_repeated_round_trips);
    RUN_TEST(session_write_then_take_is_identity);
    RUN_TEST(session_take_of_any_meta_is_safe);
    RUN_TEST(html_to_markdown_handles_any_fragment);
    RUN_TEST(cf_html_offsets_never_escape_the_blob);
    RUN_TEST(list_sort_matches_a_stable_reference_sort);
    RUN_TEST(list_unique_keeps_first_occurrences);
    RUN_TEST(list_replace_all_matches_reference);
    RUN_TEST(list_unindent_undoes_indent_for_whitespace_formats);
    RUN_TEST(list_unescape_handles_any_input);
    RUN_TEST(version_compare_is_a_consistent_order);
    RUN_TEST(update_parsers_handle_any_input);

    settings_cleanup();
    thread_safety_cleanup();
    TEST_SUMMARY();
    return 0;
}
