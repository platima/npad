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

// Copy the string value of "key" starting at p (which must point at the
// opening quote of the key) into out. Returns the position just past the
// closing quote of the value, or NULL when the shape is not key:"value".
static const char *read_string_value(const char *p, const char *key, char *out, size_t out_cap) {
    size_t klen = strlen(key);
    if (strncmp(p, key, klen) != 0)
        return NULL;
    p += klen;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return NULL;
    p++;
    size_t n = 0;
    while (p[n] && p[n] != '"')
        n++;
    if (p[n] != '"')
        return NULL;
    if (out) {
        if (n >= out_cap)
            return NULL;
        memcpy(out, p, n);
        out[n] = '\0';
    }
    return p + n + 1;
}

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    if (lx > ls)
        return false;
    const char *tail = s + (ls - lx);
    for (size_t i = 0; i < lx; i++) {
        if (tolower((unsigned char) tail[i]) != tolower((unsigned char) suffix[i]))
            return false;
    }
    return true;
}

bool update_find_asset_url(const char *json, const char *name_suffix, char *out, size_t out_cap) {
    if (!json || !name_suffix || !out || out_cap == 0)
        return false;

    const char *assets = strstr(json, "\"assets\"");
    if (!assets)
        return false;

    // Within each asset object GitHub emits "name" before
    // "browser_download_url", so remember the most recent name and use it when
    // the URL turns up. Any "name" belonging to a nested object would simply be
    // overwritten by the asset's own before its URL is reached.
    char name[256];
    bool have_name = false;

    for (const char *p = assets; *p;) {
        if (*p != '"') {
            p++;
            continue;
        }
        const char *next = read_string_value(p, "\"name\"", name, sizeof(name));
        if (next) {
            have_name = true;
            p = next;
            continue;
        }
        char url[512];
        next = read_string_value(p, "\"browser_download_url\"", url, sizeof(url));
        if (next) {
            if (have_name && ends_with_ci(name, name_suffix)) {
                size_t n = strlen(url);
                if (n >= out_cap)
                    return false;
                memcpy(out, url, n + 1);
                return true;
            }
            have_name = false; // Consumed; do not pair it with a later URL
            p = next;
            continue;
        }
        p++; // Some other key: step past this quote and keep scanning
    }
    return false;
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

bool update_is_newer_unskipped(const char *current, const char *latest, const char *skipped) {
    if (!latest || !latest[0])
        return false;
    if (update_version_compare(latest, current) <= 0)
        return false;
    // A skip only suppresses that exact version; a newer release resurfaces
    if (skipped && skipped[0] && update_version_compare(latest, skipped) == 0)
        return false;
    return true;
}
