/*
 * npad - Update check helpers implementation
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "update_check.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool update_extract_tag(const char *json, char *out, size_t out_cap) {
    if (!json || !out || out_cap == 0)
        return false;

    const char *key = strstr(json, "\"tag_name\"");
    if (!key)
        return false;
    const char *p = key + strlen("\"tag_name\"");
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != ':')
        return false;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return false;
    p++;

    size_t n = 0;
    while (p[n] && p[n] != '"')
        n++;
    if (p[n] != '"' || n == 0 || n >= out_cap)
        return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// Parse up to three dot-separated numeric components; a leading 'v'/'V' and
// anything after the numbers (suffixes like "-dev") are ignored.
static void parse_triple(const char *s, long parts[3]) {
    parts[0] = parts[1] = parts[2] = 0;
    if (!s)
        return;
    if (*s == 'v' || *s == 'V')
        s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char) *s))
            return;
        parts[i] = strtol(s, (char **) &s, 10);
        if (*s != '.')
            return;
        s++;
    }
}

int update_version_compare(const char *a, const char *b) {
    long pa[3], pb[3];
    parse_triple(a, pa);
    parse_triple(b, pb);
    for (int i = 0; i < 3; i++) {
        if (pa[i] != pb[i])
            return (pa[i] < pb[i]) ? -1 : 1;
    }
    return 0;
}

bool update_parse_sha256(const char *text, char *out) {
    if (!text || !out)
        return false;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;
    for (int i = 0; i < 64; i++) {
        char c = text[i];
        if (!isxdigit((unsigned char) c))
            return false;
        out[i] = (char) tolower((unsigned char) c);
    }
    // Must end exactly at 64 digits (longer hex runs are not SHA-256)
    if (isxdigit((unsigned char) text[64]))
        return false;
    out[64] = '\0';
    return true;
}
