/*
 * npad - Find seeding, Reset's find_ preference keys, Replace All's text
 *
 * Author: Platima
 * https://npad.dev
 */

#include "find_seed.h"
#include "settings.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool find_seed_enabled(void) {
    return settings_get_bool(FIND_SEED_KEY, true);
}

// Length in bytes of the UTF-8 sequence starting at s, or 0 when it is not a
// well-formed, shortest-form, non-surrogate sequence. cp receives the code
// point.
static size_t utf8_decode(const unsigned char *s, unsigned *cp) {
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    if (s[0] >= 0xC2 && s[0] <= 0xDF && (s[1] & 0xC0) == 0x80) {
        *cp = ((unsigned) (s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        unsigned c =
            ((unsigned) (s[0] & 0x0F) << 12) | ((unsigned) (s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (c >= 0x800 && (c < 0xD800 || c > 0xDFFF)) {
            *cp = c;
            return 3;
        }
        return 0;
    }
    if (s[0] >= 0xF0 && s[0] <= 0xF4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        unsigned c = ((unsigned) (s[0] & 0x07) << 18) | ((unsigned) (s[1] & 0x3F) << 12) |
                     ((unsigned) (s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        if (c >= 0x10000 && c <= 0x10FFFF) {
            *cp = c;
            return 4;
        }
    }
    return 0;
}

static bool is_line_break(unsigned cp) {
    return cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C || cp == 0x85 || cp == 0x2028 ||
           cp == 0x2029;
}

char *find_seed_text(const char *selection, bool escape_backslashes) {
    if (!selection || !selection[0])
        return NULL;

    size_t units = 0;
    size_t bytes = 0;
    const unsigned char *p = (const unsigned char *) selection;
    while (*p) {
        unsigned cp = 0;
        size_t n = utf8_decode(p, &cp);
        if (n == 0 || is_line_break(cp))
            return NULL;
        units += cp >= 0x10000 ? 2 : 1;
        bytes += n;
        if (escape_backslashes && cp == '\\') {
            units++;
            bytes++;
        }
        if (units > FIND_SEED_MAX_UNITS)
            return NULL;
        p += n;
    }

    char *out = malloc(bytes + 1);
    if (!out)
        return NULL;
    char *w = out;
    for (const char *r = selection; *r; r++) {
        if (escape_backslashes && *r == '\\')
            *w++ = '\\';
        *w++ = *r;
    }
    *w = '\0';
    return out;
}

void find_reset_preference_keys(void) {
    // Both are checkboxes on Preferences > Editor. Everything else under find_
    // (match case, whole word, direction, highlight, escapes, history) is set
    // only in the Find/Replace dialog and stays across a reset.
    settings_remove_key(FIND_SEED_KEY);
    settings_remove_key(FIND_WRAP_KEY);
}

wchar_t *find_replace_ranges(const wchar_t *text, size_t len, const FindRange *ranges, size_t count,
                             const wchar_t *replacement, size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if ((!text && len) || (!ranges && count) || !replacement)
        return NULL;
    size_t repl_len = wcslen(replacement);

    // Validate, and total the units the matches remove
    size_t removed = 0;
    long prev_end = 0;
    for (size_t i = 0; i < count; i++) {
        long start = ranges[i].start;
        long end = ranges[i].end;
        if (start < prev_end || end < start || (unsigned long) end > len)
            return NULL;
        removed += (size_t) (end - start);
        prev_end = end;
    }

    size_t kept = len - removed;
    if (repl_len && count > (SIZE_MAX / sizeof(wchar_t) - 1 - kept) / repl_len)
        return NULL; // The result could not be addressed
    size_t total = kept + count * repl_len;

    wchar_t *out = malloc((total + 1) * sizeof(wchar_t));
    if (!out)
        return NULL;
    wchar_t *w = out;
    size_t from = 0;
    for (size_t i = 0; i <= count; i++) {
        // The kept text before this match (or, last time round, the tail)
        size_t upto = i < count ? (size_t) ranges[i].start : len;
        if (upto > from) {
            memcpy(w, text + from, (upto - from) * sizeof(wchar_t));
            w += upto - from;
        }
        if (i == count)
            break;
        if (repl_len) {
            memcpy(w, replacement, repl_len * sizeof(wchar_t));
            w += repl_len;
        }
        from = (size_t) ranges[i].end;
    }
    *w = L'\0';
    if (out_len)
        *out_len = total;
    return out;
}
