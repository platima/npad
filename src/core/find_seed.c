/*
 * npad - Find seeding
 *
 * Author: Platima
 * https://npad.dev
 */

#include "find_seed.h"
#include "settings.h"

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
