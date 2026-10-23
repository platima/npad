/*
 * npad - Update check helpers implementation
 *
 * Author: Platima
 * https://npad.dev
 */

#include "update_check.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// A minimal JSON walker for the releases API responses. Not a validator: it
// only needs to know where each value starts and ends, so that a key is read
// at the level of the object it belongs to. Every step stops at the
// terminating NUL, so a response that is cut short simply yields nothing.
// ---------------------------------------------------------------------------

static const char *json_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

// p is on a string's opening quote: the position just past its closing quote,
// or NULL when the string never ends
static const char *json_skip_string(const char *p) {
    if (*p != '"')
        return NULL;
    for (p++; *p; p++) {
        if (*p == '\\') {
            if (!p[1])
                return NULL;
            p++; // The escaped character, which may be a quote
        } else if (*p == '"') {
            return p + 1;
        }
    }
    return NULL;
}

// Past one value of any type, or NULL when it is malformed or cut short
static const char *json_skip_value(const char *p) {
    p = json_ws(p);
    if (*p == '"')
        return json_skip_string(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = json_skip_string(p);
                if (!p)
                    return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                if (--depth == 0)
                    return p + 1;
            }
            p++;
        }
        return NULL;
    }
    // A number, true, false or null
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' &&
           *p != '\n')
        p++;
    return p == start ? NULL : p;
}

// The value of key in the object starting at obj - that object's own member,
// never one of a nested object - or NULL
static const char *json_member(const char *obj, const char *key) {
    if (!obj)
        return NULL;
    const char *p = json_ws(obj);
    if (*p != '{')
        return NULL;
    size_t klen = strlen(key);
    p = json_ws(p + 1);
    while (*p == '"') {
        const char *key_end = json_skip_string(p);
        if (!key_end)
            return NULL;
        bool match = (size_t) (key_end - p) == klen + 2 && strncmp(p + 1, key, klen) == 0;
        p = json_ws(key_end);
        if (*p != ':')
            return NULL;
        p = json_ws(p + 1);
        if (match)
            return p;
        p = json_skip_value(p);
        if (!p)
            return NULL;
        p = json_ws(p);
        if (*p != ',')
            return NULL; // '}' (not found) or malformed
        p = json_ws(p + 1);
    }
    return NULL;
}

// The first element of the array starting at p, or NULL when it is empty
static const char *json_first(const char *p) {
    if (!p)
        return NULL;
    p = json_ws(p);
    if (*p != '[')
        return NULL;
    p = json_ws(p + 1);
    return (*p == ']' || !*p) ? NULL : p;
}

// The element after elem, or NULL at the end of the array
static const char *json_next(const char *elem) {
    const char *p = json_skip_value(elem);
    if (!p)
        return NULL;
    p = json_ws(p);
    if (*p != ',')
        return NULL;
    p = json_ws(p + 1);
    return *p ? p : NULL;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char) tolower((unsigned char) c);
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

// Copy the string value at p into out, decoding its escapes. \u escapes are
// accepted for ASCII only: nothing npad reads (asset names, URLs, tags) is
// anything else, and refusing beats mangling. False when p is not a string or
// it does not fit.
static bool json_copy_string(const char *p, char *out, size_t cap) {
    if (!p || *p != '"' || !out || cap == 0)
        return false;
    size_t n = 0;
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
                case '"':
                case '\\':
                case '/':
                    c = *p;
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'u': {
                    int v = 0;
                    for (int i = 1; i <= 4; i++) {
                        int h = hex_value(p[i]);
                        if (h < 0)
                            return false;
                        v = v * 16 + h;
                    }
                    if (v == 0 || v >= 0x80)
                        return false;
                    c = (char) v;
                    p += 4;
                    break;
                }
                default:
                    return false;
            }
        }
        if (n + 1 >= cap)
            return false;
        out[n++] = c;
    }
    if (*p != '"')
        return false;
    out[n] = '\0';
    return true;
}

static bool json_is_true(const char *p) {
    return p && strncmp(p, "true", 4) == 0;
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

    // Each asset's own "name" and "browser_download_url", read from the asset
    // object itself: GitHub's field order is not a contract, and the uploader
    // object nested inside every asset may one day carry a "name" of its own
    const char *assets = json_member(json, "assets");
    for (const char *a = json_first(assets); a; a = json_next(a)) {
        char name[256];
        if (!json_copy_string(json_member(a, "name"), name, sizeof(name)) ||
            !ends_with_ci(name, name_suffix))
            continue;
        if (json_copy_string(json_member(a, "browser_download_url"), out, out_cap))
            return true;
    }
    out[0] = '\0';
    return false;
}

bool update_pick_release_tag(const char *json, bool include_prereleases, char *out,
                             size_t out_cap) {
    if (!json || !out || out_cap == 0)
        return false;
    const char *p = json_ws(json);
    bool single = (*p == '{');
    char best[64] = "";
    for (const char *r = single ? p : json_first(p); r; r = single ? NULL : json_next(r)) {
        if (json_is_true(json_member(r, "draft")))
            continue;
        if (!include_prereleases && json_is_true(json_member(r, "prerelease")))
            continue;
        char tag[64];
        if (!json_copy_string(json_member(r, "tag_name"), tag, sizeof(tag)))
            continue;
        const char *digits = (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag;
        if (!isdigit((unsigned char) *digits))
            continue; // Not a version tag
        if (!best[0] || update_version_compare(tag, best) > 0)
            memcpy(best, tag, sizeof(best));
    }
    size_t n = strlen(best);
    if (n == 0 || n >= out_cap)
        return false;
    memcpy(out, best, n + 1);
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

// The pre-release suffix of a version string: what follows "-" right after
// the numeric triple, up to a space or the end ("rc2" in "v0.36.0-rc2
// (abc)"). NULL when there is none.
static const char *prerelease_suffix(const char *s, size_t *len) {
    if (!s)
        return NULL;
    if (*s == 'v' || *s == 'V')
        s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char) *s))
            return NULL;
        while (isdigit((unsigned char) *s))
            s++;
        if (i < 2) {
            if (*s != '.')
                return NULL;
            s++;
        }
    }
    if (*s != '-' || !s[1] || s[1] == ' ')
        return NULL;
    s++;
    size_t n = 0;
    while (s[n] && s[n] != ' ')
        n++;
    *len = n;
    return s;
}

// Compare two suffixes with runs of digits compared as numbers
static int natural_compare(const char *a, size_t la, const char *b, size_t lb) {
    size_t i = 0, j = 0;
    while (i < la && j < lb) {
        if (isdigit((unsigned char) a[i]) && isdigit((unsigned char) b[j])) {
            unsigned long na = 0, nb = 0;
            while (i < la && isdigit((unsigned char) a[i]))
                na = na * 10 + (unsigned long) (a[i++] - '0');
            while (j < lb && isdigit((unsigned char) b[j]))
                nb = nb * 10 + (unsigned long) (b[j++] - '0');
            if (na != nb)
                return na < nb ? -1 : 1;
            continue;
        }
        int ca = tolower((unsigned char) a[i]), cb = tolower((unsigned char) b[j]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        i++;
        j++;
    }
    if (i < la)
        return 1;
    if (j < lb)
        return -1;
    return 0;
}

int update_version_compare(const char *a, const char *b) {
    long pa[3], pb[3];
    parse_triple(a, pa);
    parse_triple(b, pb);
    for (int i = 0; i < 3; i++) {
        if (pa[i] != pb[i])
            return (pa[i] < pb[i]) ? -1 : 1;
    }
    // Same triple: a pre-release comes before its release
    size_t la = 0, lb = 0;
    const char *sa = prerelease_suffix(a, &la);
    const char *sb = prerelease_suffix(b, &lb);
    if (!sa && !sb)
        return 0;
    if (!sa)
        return 1;
    if (!sb)
        return -1;
    return natural_compare(sa, la, sb, lb);
}

bool update_is_prerelease(const char *version) {
    size_t n = 0;
    const char *suffix = prerelease_suffix(version, &n);
    if (!suffix)
        return false;
    return !(n == 3 && tolower((unsigned char) suffix[0]) == 'd' &&
             tolower((unsigned char) suffix[1]) == 'e' &&
             tolower((unsigned char) suffix[2]) == 'v');
}

void update_http_status_message(int status, char *out, size_t cap) {
    if (!out || cap == 0)
        return;
    if (status == 403 || status == 429) {
        // GitHub's unauthenticated API allows 60 requests an hour per address
        snprintf(out, cap,
                 "GitHub is limiting update checks from this network for now (HTTP %d). Try "
                 "again in an hour.",
                 status);
    } else if (status == 404) {
        snprintf(out, cap, "The update server could not find npad's releases (HTTP 404).");
    } else if (status >= 500 && status <= 599) {
        snprintf(out, cap, "GitHub's servers are having trouble (HTTP %d). Try again later.",
                 status);
    } else {
        snprintf(out, cap, "The update server answered with HTTP %d. Try again later.", status);
    }
}

void update_current_version(const char *build_version, int major, int minor, int patch, char *out,
                            size_t cap) {
    if (!out || cap == 0)
        return;
    const char *s = build_version ? build_version : "";
    if (*s == 'v' || *s == 'V')
        s++;
    long parts[3];
    parse_triple(s, parts);
    if (!isdigit((unsigned char) *s) || parts[0] != major || parts[1] != minor ||
        parts[2] != patch) {
        // Not a version string we recognise, or not this build's numbers
        snprintf(out, cap, "v%d.%d.%d", major, minor, patch);
        return;
    }
    size_t n = 0;
    while (s[n] && s[n] != ' ')
        n++;
    snprintf(out, cap, "v%.*s", (int) n, s);
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

const char *update_build_arch(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#else
    return NULL;
#endif
}

// The installers each architecture's updater downloads. Release validation
// (scripts/validate-release-assets.sh) reads every quoted string in this file
// that starts with -setup-win- or -msi-win- and fails the release unless an
// asset ends with each one, so keep them spelled out here rather than
// assembled at run time, and quote no other string of that shape in this file.
static const struct {
    const char *arch;
    const char *installer;
    const char *checksum;
    const char *msi;
    const char *msi_checksum;
} k_installers[] = {
    { "x64", "-setup-win-x64.exe", "-setup-win-x64.exe.sha256", "-msi-win-x64.msi",
      "-msi-win-x64.msi.sha256" },
    { "arm64", "-setup-win-arm64.exe", "-setup-win-arm64.exe.sha256", "-msi-win-arm64.msi",
      "-msi-win-arm64.msi.sha256" },
};

const char *update_package_suffix(const char *arch, UpdatePackage package, bool checksum) {
    if (!arch)
        return NULL;
    for (size_t i = 0; i < sizeof(k_installers) / sizeof(k_installers[0]); i++) {
        if (strcmp(arch, k_installers[i].arch) != 0)
            continue;
        if (package == UPDATE_PACKAGE_MSI)
            return checksum ? k_installers[i].msi_checksum : k_installers[i].msi;
        return checksum ? k_installers[i].checksum : k_installers[i].installer;
    }
    return NULL;
}

const char *update_installer_suffix(const char *arch, bool checksum) {
    return update_package_suffix(arch, UPDATE_PACKAGE_SETUP, checksum);
}

UpdatePackage update_windows_package(bool setup_owns_this_copy, bool msi_owns_this_copy) {
    // Both claiming the folder means two products already share it; the setup
    // is what this updater has always run there, so keep to it
    return (msi_owns_this_copy && !setup_owns_this_copy) ? UPDATE_PACKAGE_MSI
                                                         : UPDATE_PACKAGE_SETUP;
}

// An MSI Feature table identifier: letters, digits, '_' and '.'. Anything else
// would not be a feature npad's MSI has, and must not reach a command line.
static bool msi_feature_name_ok(const char *s) {
    if (!s || !s[0])
        return false;
    for (; *s; s++) {
        if (!isalnum((unsigned char) *s) && *s != '_' && *s != '.')
            return false;
    }
    return true;
}

static bool name_listed(const char *name, const char *const *list, size_t count) {
    for (size_t i = 0; list && i < count; i++) {
        if (list[i] && strcmp(list[i], name) == 0)
            return true;
    }
    return false;
}

// Append ` <property>=a,b,...` for the valid entries of installed that are
// (keep == false) or are not (keep == true) in drop. False when it does not fit
// or names nothing.
static bool append_feature_list(char *out, size_t cap, size_t *used, const char *property,
                                const char *const *installed, size_t installed_count,
                                const char *const *drop, size_t drop_count, bool keep) {
    int w = snprintf(out + *used, cap - *used, " %s=", property);
    if (w < 0 || (size_t) w >= cap - *used)
        return false;
    *used += (size_t) w;
    bool any = false;
    for (size_t i = 0; i < installed_count; i++) {
        const char *name = installed[i];
        if (!msi_feature_name_ok(name) || name_listed(name, drop, drop_count) == keep)
            continue;
        if (name_listed(name, installed, i))
            continue; // Listed twice
        w = snprintf(out + *used, cap - *used, "%s%s", any ? "," : "", name);
        if (w < 0 || (size_t) w >= cap - *used)
            return false;
        *used += (size_t) w;
        any = true;
    }
    return any;
}

bool update_msi_feature_args(const char *const *installed, size_t installed_count,
                             const char *const *drop, size_t drop_count, char *out, size_t cap) {
    if (!out || cap == 0)
        return false;
    out[0] = '\0';
    if (!installed)
        return false;

    bool must_drop = false;
    for (size_t i = 0; i < installed_count && !must_drop; i++)
        must_drop =
            msi_feature_name_ok(installed[i]) && name_listed(installed[i], drop, drop_count);
    if (!must_drop)
        return false; // Leave the selection to MigrateFeatureStates

    size_t used = 0;
    if (!append_feature_list(out, cap, &used, "ADDLOCAL", installed, installed_count, drop,
                             drop_count, true) ||
        !append_feature_list(out, cap, &used, "REMOVE", installed, installed_count, drop,
                             drop_count, false)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static bool is_path_sep(char c) {
    return c == '\\' || c == '/';
}

int update_msi_owner(const UpdateMsiProduct *products, size_t count, const char *exe_path,
                     const char *machine_default_dir, const char *user_default_dir) {
    if (!products || !exe_path)
        return -1;
    for (size_t i = 0; i < count; i++) {
        const char *loc = products[i].install_location;
        if (loc && loc[0] && update_dir_holds_exe(loc, exe_path))
            return (int) i;
    }
    for (size_t i = 0; i < count; i++) {
        const char *loc = products[i].install_location;
        if (!loc || loc[0])
            continue;
        const char *def = products[i].per_machine ? machine_default_dir : user_default_dir;
        if (def && update_dir_holds_exe(def, exe_path))
            return (int) i;
    }
    return -1;
}

bool update_dir_holds_exe(const char *dir, const char *exe_path) {
    if (!dir || !exe_path || !dir[0])
        return false;
    size_t dl = strlen(dir);
    while (dl > 0 && is_path_sep(dir[dl - 1]))
        dl--;
    if (dl == 0)
        return false;
    // The exe's directory: everything before its last separator
    const char *last = NULL;
    for (const char *p = exe_path; *p; p++) {
        if (is_path_sep(*p))
            last = p;
    }
    if (!last || !last[1])
        return false;
    size_t el = (size_t) (last - exe_path);
    if (el != dl)
        return false;
    for (size_t i = 0; i < dl; i++) {
        char a = dir[i], b = exe_path[i];
        if (is_path_sep(a) && is_path_sep(b))
            continue;
        if (tolower((unsigned char) a) != tolower((unsigned char) b))
            return false;
    }
    return true;
}

const char *update_signature_problem(UpdateSignature this_build, UpdateSignature installer,
                                     bool same_publisher) {
    if (installer == UPDATE_SIG_BAD)
        return "The downloaded installer's digital signature is not valid, so it was not run.";
    if (this_build != UPDATE_SIG_VALID)
        return NULL; // An unsigned build: the SHA-256 is the check it has always had
    if (installer == UPDATE_SIG_NONE)
        return "This copy of npad is digitally signed, but the downloaded installer is not, so "
               "it was not run.";
    if (!same_publisher)
        return "The downloaded installer is signed by a different publisher than this copy of "
               "npad, so it was not run.";
    return NULL;
}

// ---------------------------------------------------------------------------
// HTTP response parsing (the Linux check)
// ---------------------------------------------------------------------------

// Whether the header line [line, line + n) is "<name>:" (case-insensitive);
// on success *value points past the colon and any spaces
static bool header_is(const char *line, size_t n, const char *name, const char **value) {
    size_t ln = strlen(name);
    if (n <= ln || line[ln] != ':')
        return false;
    for (size_t i = 0; i < ln; i++) {
        if (tolower((unsigned char) line[i]) != tolower((unsigned char) name[i]))
            return false;
    }
    const char *v = line + ln + 1;
    while (v < line + n && (*v == ' ' || *v == '\t'))
        v++;
    *value = v;
    return true;
}

// Whether [s, end) contains word, case-insensitively
static bool span_contains_ci(const char *s, const char *end, const char *word) {
    size_t lw = strlen(word);
    for (; s + lw <= end; s++) {
        size_t i = 0;
        while (i < lw && tolower((unsigned char) s[i]) == tolower((unsigned char) word[i]))
            i++;
        if (i == lw)
            return true;
    }
    return false;
}

// The first CRLF in [p, end), or NULL
static const char *find_crlf(const char *p, const char *end) {
    for (; p + 1 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n')
            return p;
    }
    return NULL;
}

// De-chunk [p, end) into out (which has room for end - p bytes). Returns the
// decoded length, or -1 when the chunks are malformed or cut short.
static long dechunk(const char *p, const char *end, char *out) {
    long n = 0;
    for (;;) {
        const char *eol = find_crlf(p, end);
        if (!eol || !isxdigit((unsigned char) *p))
            return -1;
        unsigned long size = 0;
        for (; p < eol && isxdigit((unsigned char) *p); p++) {
            if (size > (unsigned long) (end - p))
                return -1; // Larger than everything that is left
            int c = tolower((unsigned char) *p);
            size = size * 16 + (unsigned long) (isdigit(c) ? c - '0' : c - 'a' + 10);
        }
        p = eol + 2; // Past the size line (and any chunk extension)
        if (size == 0)
            return n; // Last chunk; trailers, if any, are of no interest
        if (size > (unsigned long) (end - p) || (unsigned long) (end - p) - size < 2)
            return -1;
        memcpy(out + n, p, size);
        n += (long) size;
        p += size;
        if (p[0] != '\r' || p[1] != '\n')
            return -1;
        p += 2;
    }
}

bool update_http_parse_response(const char *raw, size_t len, int *status, char **body,
                                size_t *body_len) {
    if (!raw || !status || !body)
        return false;
    *body = NULL;
    // "HTTP/1.x NNN"
    if (len < 12 || strncmp(raw, "HTTP/1.", 7) != 0 || !isdigit((unsigned char) raw[7]) ||
        raw[8] != ' ')
        return false;
    for (int i = 9; i < 12; i++) {
        if (!isdigit((unsigned char) raw[i]))
            return false;
    }
    int code = (raw[9] - '0') * 100 + (raw[10] - '0') * 10 + (raw[11] - '0');

    const char *end = raw + len;
    const char *line = find_crlf(raw, end);
    if (!line)
        return false;
    line += 2;

    bool chunked = false;
    long long content_length = -1;
    for (;;) {
        const char *eol = find_crlf(line, end);
        if (!eol)
            return false; // The headers never ended
        if (eol == line)
            break; // The blank line: the body follows
        const char *value = NULL;
        size_t n = (size_t) (eol - line);
        if (header_is(line, n, "Transfer-Encoding", &value)) {
            chunked = span_contains_ci(value, eol, "chunked");
        } else if (header_is(line, n, "Content-Length", &value)) {
            content_length = 0;
            for (; value < eol && isdigit((unsigned char) *value); value++) {
                if (content_length > (long long) len)
                    return false;
                content_length = content_length * 10 + (*value - '0');
            }
        }
        line = eol + 2;
    }

    const char *start = line + 2;
    size_t avail = (size_t) (end - start);
    char *out = malloc(avail + 1);
    if (!out)
        return false;
    size_t out_len;
    if (chunked) {
        long n = dechunk(start, end, out);
        if (n < 0) {
            free(out);
            return false;
        }
        out_len = (size_t) n;
    } else if (content_length >= 0) {
        if ((unsigned long long) content_length > avail) {
            free(out);
            return false; // The connection closed before the body was complete
        }
        out_len = (size_t) content_length;
        memcpy(out, start, out_len);
    } else {
        out_len = avail;
        memcpy(out, start, out_len);
    }
    out[out_len] = '\0';
    *status = code;
    *body = out;
    if (body_len)
        *body_len = out_len;
    return true;
}

// ---------------------------------------------------------------------------
// How this copy was installed (Linux)
// ---------------------------------------------------------------------------

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    return lx <= ls && strcmp(s + (ls - lx), suffix) == 0;
}

UpdateInstallSource update_install_source(const UpdateInstallFacts *facts) {
    if (!facts)
        return UPDATE_SOURCE_PORTABLE;
    switch (facts->owner) {
        case UPDATE_OWNER_DPKG:
            return facts->apt_repo ? UPDATE_SOURCE_APT_REPO : UPDATE_SOURCE_DEB_FILE;
        case UPDATE_OWNER_RPM:
            return facts->dnf_repo ? UPDATE_SOURCE_DNF_REPO : UPDATE_SOURCE_RPM_FILE;
        case UPDATE_OWNER_PACMAN:
            // AUR packages are named for how they build; the repository's is
            // plain "npad", so the name wins even with [npad] configured
            if (facts->package &&
                (ends_with(facts->package, "-bin") || ends_with(facts->package, "-git")))
                return UPDATE_SOURCE_AUR;
            return facts->pacman_repo ? UPDATE_SOURCE_PACMAN_REPO : UPDATE_SOURCE_PACMAN_FILE;
        case UPDATE_OWNER_NONE:
        default:
            return UPDATE_SOURCE_PORTABLE;
    }
}

const char *update_install_advice(UpdateInstallSource source) {
    switch (source) {
        case UPDATE_SOURCE_APT_REPO:
            return "npad came from the npad apt repository, so it updates with the rest of your "
                   "system. Use your system's updater, or run:\n"
                   "sudo apt update && sudo apt upgrade";
        case UPDATE_SOURCE_DNF_REPO:
            return "npad came from the npad dnf repository, so it updates with the rest of your "
                   "system. Use your system's updater, or run:\n"
                   "sudo dnf upgrade npad";
        case UPDATE_SOURCE_PACMAN_REPO:
            return "npad came from the npad pacman repository, so it updates with the rest of "
                   "your system:\n"
                   "sudo pacman -Syu";
        case UPDATE_SOURCE_AUR:
            return "npad came from the AUR. Update it with your AUR helper, for example:\n"
                   "yay -Syu";
        case UPDATE_SOURCE_DEB_FILE:
            return "npad was installed from a downloaded .deb, without the npad repository, so "
                   "it does not update with your system. Download the new .deb from the release "
                   "page, or add the repository once to get updates with the rest of your "
                   "system:\n"
                   "curl -fsSL https://pkg.npad.dev/install.sh | sudo sh";
        case UPDATE_SOURCE_RPM_FILE:
            return "npad was installed from a downloaded rpm, without the npad repository, so it "
                   "does not update with your system. Download the new rpm from the release "
                   "page, or on Fedora add the repository once to get updates with the rest of "
                   "your system:\n"
                   "curl -fsSL https://pkg.npad.dev/install.sh | sudo sh";
        case UPDATE_SOURCE_PACMAN_FILE:
            return "npad was installed with pacman, but not from the npad repository. If it came "
                   "from the AUR, update it with your AUR helper; otherwise download the new "
                   "package from the release page, or add the [npad] repository described at:\n"
                   "https://pkg.npad.dev";
        case UPDATE_SOURCE_PORTABLE:
        default:
            return "This copy of npad was not installed by a package manager (a portable copy, "
                   "or built from source), so nothing updates it. Download the new release "
                   "from:\n"
                   "https://github.com/platima/npad/releases/latest";
    }
}

bool update_exe_may_be_packaged(const char *exe_path) {
    if (!exe_path)
        return false;
    const char *name = NULL;
    if (strncmp(exe_path, "/usr/bin/", 9) == 0)
        name = exe_path + 9;
    else if (strncmp(exe_path, "/bin/", 5) == 0)
        name = exe_path + 5;
    return name && name[0] && !strchr(name, '/');
}

// Calls match(line, end) for each line of text that is not blank or a
// comment, with leading whitespace skipped; stops at the first true
static bool any_live_line(const char *text, bool (*match)(const char *, const char *)) {
    if (!text)
        return false;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        if (!eol)
            eol = p + strlen(p);
        const char *s = p;
        while (s < eol && (*s == ' ' || *s == '\t'))
            s++;
        if (s < eol && *s != '#' && *s != ';' && match(s, eol))
            return true;
        p = *eol ? eol + 1 : eol;
    }
    return false;
}

static bool names_npad_host(const char *line, const char *end) {
    return span_contains_ci(line, end, "pkg.npad.dev");
}

static bool is_npad_section(const char *line, const char *end) {
    static const char section[] = "[npad]";
    size_t n = sizeof(section) - 1;
    if ((size_t) (end - line) < n || strncmp(line, section, n) != 0)
        return false;
    for (const char *p = line + n; p < end && *p != '#'; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\r')
            return false; // "[npad]x" is some other line
    }
    return true;
}

bool update_repo_text_names_npad(const char *text) {
    return any_live_line(text, names_npad_host);
}

bool update_pacman_conf_has_npad(const char *text) {
    return any_live_line(text, is_npad_section);
}

static bool is_package_char(char c) {
    return isalnum((unsigned char) c) || c == '+' || c == '-' || c == '.' || c == '_' || c == '@';
}

bool update_parse_owner_output(UpdatePackageOwner owner, const char *output, char *pkg,
                               size_t pkg_cap) {
    if (!output || !pkg || pkg_cap == 0 || owner == UPDATE_OWNER_NONE)
        return false;
    const char *p = output;
    while (*p) {
        const char *eol = strchr(p, '\n');
        if (!eol)
            eol = p + strlen(p);
        const char *s = p;
        while (s < eol && (*s == ' ' || *s == '\t'))
            s++;
        p = *eol ? eol + 1 : eol;
        if (s == eol || *s == '\r')
            continue;
        // dpkg -S reports diversions on lines of their own before the owner
        if (owner == UPDATE_OWNER_DPKG && strncmp(s, "diversion by ", 13) == 0)
            continue;
        if (owner == UPDATE_OWNER_DPKG && strncmp(s, "dpkg-query:", 11) == 0)
            return false; // "dpkg-query: no path found matching pattern ..."

        // The name runs to the first character a package name cannot hold.
        // For dpkg that must be the "pkg: path" separator (an arch qualifier
        // such as "npad:amd64: path" is skipped); rpm and pacman print the
        // name alone, so anything else after it means a message, not a name.
        size_t n = 0;
        while (s + n < eol && is_package_char(s[n]))
            n++;
        const char *rest = s + n;
        if (owner == UPDATE_OWNER_DPKG) {
            const char *colon = rest;
            if (*colon == ':' && colon[1] != ' ') { // ":amd64"
                colon++;
                while (colon < eol && is_package_char(*colon))
                    colon++;
            }
            if (*colon != ':' && *colon != ',')
                return false;
        } else {
            while (rest < eol && (*rest == ' ' || *rest == '\t' || *rest == '\r'))
                rest++;
            if (rest != eol)
                return false; // "file ... is not owned by any package", "error: ..."
        }
        if (n == 0 || n >= pkg_cap)
            return false;
        memcpy(pkg, s, n);
        pkg[n] = '\0';
        return true;
    }
    return false;
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
