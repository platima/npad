/*
 * npad - Session Recovery Implementation
 * Crash-recovery snapshots of unsaved documents
 *
 * Each instance owns a slot: <slot>.txt (UTF-8 content) and <slot>.meta.
 * Slots present at startup are leftovers from instances that did not exit
 * cleanly.
 *
 * The meta file is three lines:
 *
 *     <encoding>
 *     <line ending>[ truncated=<0|1>][ altered=<0|1>][ stamp=<stamp>]
 *     <original path, possibly empty, running to the end of the file>
 *
 * <stamp> is <size>:<write time>:<device>:<inode>, the whole FileStamp; one
 * without the last two (device and inode 0) still parses.
 *
 * The attributes ride on the second line, after the number, because npad
 * before 0.36 reads that line with atoi() and ignores whatever follows: a
 * meta written by this version still restores in an older one, and an older
 * three-line meta reads here as "no attributes". The path stays last because
 * a path may itself contain a newline.
 *
 * On Linux the recovery directory is 0700 and every file in it 0600: the
 * snapshots hold unsaved text, which is exactly what must not be readable by
 * other users of the machine.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "session.h"
#include "file_ops.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define session_getpid _getpid
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define session_getpid getpid
#endif

#define SESSION_MAX_SLOTS 64
// Upper bound on directory entries examined in one pass; the recovery
// directory normally holds two or three files per open window
#define SESSION_MAX_ENTRIES 4096

#define CLAIM_INFIX ".meta.claim-"

// --- Slot ids ----------------------------------------------------------------

bool session_is_valid_slot_id(const char *id) {
    if (!id)
        return false;
    int parts = 0;
    size_t run = 0;
    size_t i = 0;
    for (;; i++) {
        if (i >= SESSION_SLOT_ID_MAX)
            return false; // Too long to be one of ours
        char c = id[i];
        if (c == '-' || c == '\0') {
            if (run == 0 || run > 16)
                return false;
            parts++;
            run = 0;
            if (c == '\0')
                break;
        } else if (isxdigit((unsigned char) c)) {
            run++;
        } else {
            return false; // Anything else could reach outside the directory
        }
    }
    return parts == 2 || parts == 3;
}

bool session_parse_slot_id(const char *id, unsigned long long *start_time, long *pid) {
    if (!session_is_valid_slot_id(id))
        return false;
    char *end = NULL;
    unsigned long long t = strtoull(id, &end, 16);
    if (!end || *end != '-')
        return false;
    // The pid is always the last part, in "<time>-<pid>" and in
    // "<time>-<nonce>-<pid>" alike, which is where npad before 0.36 looks
    const char *last = strrchr(id, '-');
    unsigned long long p = strtoull(last + 1, NULL, 16);
    if (p > 0x7fffffffULL)
        return false; // Not a pid any supported platform hands out
    if (start_time)
        *start_time = t;
    if (pid)
        *pid = (long) p;
    return true;
}

// Only has to tell apart two processes that share a pid and a start second,
// which needs variety rather than cryptographic strength
static unsigned random_nonce(void) {
    unsigned n = 0;
#ifdef _WIN32
    LARGE_INTEGER counter;
    if (QueryPerformanceCounter(&counter))
        n ^= (unsigned) counter.LowPart ^ (unsigned) counter.HighPart;
    n ^= (unsigned) GetTickCount();
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        unsigned char bytes[4];
        if (fread(bytes, 1, sizeof(bytes), f) == sizeof(bytes))
            n = (unsigned) bytes[0] | ((unsigned) bytes[1] << 8) | ((unsigned) bytes[2] << 16) |
                ((unsigned) bytes[3] << 24);
        fclose(f);
    }
#endif
    n ^= (unsigned) clock();
    n ^= (unsigned) ((uintptr_t) &n >> 4); // Address-space layout differs per process
    return n;
}

void session_make_slot_id(char *out, size_t size) {
    if (!out || size == 0)
        return;
    // Nonce in the middle, pid last: npad up to 0.36.0-rc1 takes the pid from
    // after the last '-', so it still sees this window as alive
    snprintf(out, size, "%lx-%x-%x", (unsigned long) time(NULL), random_nonce() & 0xffffu,
             (unsigned) session_getpid());
}

// --- Files -------------------------------------------------------------------

// Snapshots are unsaved text: keep them to the user on Linux. Windows gets
// the same from the per-user ACL that %APPDATA% already carries.
static void make_private(const char *path) {
#ifndef _WIN32
    if (path)
        chmod(path, 0600);
#else
    (void) path;
#endif
}

// A recovery directory made 0755 by npad before 0.36 is narrowed to 0700.
// Done on every look at the directory, not only on a write, so leftovers an
// older npad wrote 0644 stop being readable before anything is offered.
static void tighten_dir(const char *dir) {
#ifndef _WIN32
    struct stat st;
    if (dir && stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 077) != 0)
        chmod(dir, 0700);
#else
    (void) dir;
#endif
}

static void ensure_dir(const char *dir) {
    if (!dir)
        return;
#ifdef _WIN32
    wchar_t wdir[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, 1024) > 0) {
        CreateDirectoryW(wdir, NULL);
    } else {
        CreateDirectoryA(dir, NULL);
    }
#else
    if (mkdir(dir, 0700) != 0)
        tighten_dir(dir); // Already there - possibly made 0755 by npad before 0.36
#endif
}

// Build "<dir>/<slot><suffix>" (suffix e.g. ".txt"). NULL for an id that is
// not one of ours, so a crafted id can never become a path.
static char *slot_path(const char *dir, const char *slot_id, const char *suffix) {
    if (!dir || !session_is_valid_slot_id(slot_id))
        return NULL;
    size_t need = strlen(slot_id) + strlen(suffix) + 1;
    char *name = malloc(need);
    if (!name)
        return NULL;
    snprintf(name, need, "%s%s", slot_id, suffix);
    char *full = file_join_paths(dir, name);
    free(name);
    return full;
}

// "<dir>/<slot>.meta.claim-<claimer>"
static char *claim_path(const char *dir, const char *slot_id, const char *claimer) {
    if (!session_is_valid_slot_id(claimer))
        return NULL;
    char suffix[sizeof(CLAIM_INFIX) + SESSION_SLOT_ID_MAX];
    snprintf(suffix, sizeof(suffix), "%s%s", CLAIM_INFIX, claimer);
    return slot_path(dir, slot_id, suffix);
}

// Rename honoring UTF-8 paths on Windows. Returns true on success.
static bool rename_utf8(const char *from, const char *to) {
    if (!from || !to)
        return false;
#ifdef _WIN32
    wchar_t wfrom[1024], wto[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, from, -1, wfrom, 1024) > 0 &&
        MultiByteToWideChar(CP_UTF8, 0, to, -1, wto, 1024) > 0) {
        return MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING) != 0;
    }
    return rename(from, to) == 0;
#else
    return rename(from, to) == 0;
#endif
}

static bool path_exists(const char *path) {
    return path && file_exists(path);
}

static void delete_path(const char *path) {
    if (path)
        file_delete(path);
}

// --- Meta --------------------------------------------------------------------

static void default_meta(SessionMeta *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->encoding = NPAD_ENC_UTF8;
    meta->line_ending = NPAD_DEFAULT_LINE_ENDING;
    meta->stamp.size = -1;
}

static char *format_meta(const char *path, const SessionMeta *meta) {
    SessionMeta defaults;
    if (!meta) {
        default_meta(&defaults);
        meta = &defaults;
    }
    size_t path_len = path ? strlen(path) : 0;
    size_t size = path_len + 192;
    char *text = malloc(size);
    if (!text)
        return NULL;
    // The whole FileStamp: on POSIX file_stamp_equal also compares the device
    // and inode, so a stamp restored without them would read as a change
    char stamp[128] = "";
    if (meta->stamp.size >= 0) {
        snprintf(stamp, sizeof(stamp), " stamp=%lld:%lld:%llu:%llu", meta->stamp.size,
                 meta->stamp.write_time, meta->stamp.device, meta->stamp.inode);
    }
    snprintf(text, size, "%d\n%d truncated=%d altered=%d%s\n%s", (int) meta->encoding,
             (int) meta->line_ending, meta->truncated ? 1 : 0, meta->altered ? 1 : 0, stamp,
             path ? path : "");
    return text;
}

static bool write_meta_file(const char *meta_path, const char *path, const SessionMeta *meta) {
    char *text = format_meta(path, meta);
    if (!text)
        return false;
    bool ok = file_write_text_atomic(meta_path, text);
    free(text);
    if (ok)
        make_private(meta_path);
    return ok;
}

// Find " <key>=" within [line, line_end) and return what follows it. The
// leading space is part of the match, so a key must follow a separator rather
// than end some other word.
static const char *find_attribute(const char *line, const char *line_end, const char *key) {
    size_t key_len = strlen(key);
    for (const char *p = line; p + key_len + 2 <= line_end; p++) {
        if (*p == ' ' && strncmp(p + 1, key, key_len) == 0 && p[key_len + 1] == '=')
            return p + key_len + 2;
    }
    return NULL;
}

// Parse the meta format described at the top of this file
static void parse_meta(const char *text, char **out_path, SessionMeta *meta) {
    default_meta(meta);
    int enc = 0, eol = (int) NPAD_DEFAULT_LINE_ENDING;
    const char *first_nl = strchr(text, '\n');
    const char *second_nl = first_nl ? strchr(first_nl + 1, '\n') : NULL;
    if (first_nl && second_nl) {
        enc = atoi(text);
        eol = atoi(first_nl + 1);

        const char *value = find_attribute(first_nl + 1, second_nl, "truncated");
        if (value) {
            meta->has_truncated = true;
            meta->truncated = atoi(value) != 0;
        }
        value = find_attribute(first_nl + 1, second_nl, "altered");
        if (value)
            meta->altered = *value == '1';
        value = find_attribute(first_nl + 1, second_nl, "stamp");
        if (value) {
            char *end = NULL;
            long long size = strtoll(value, &end, 10);
            if (end && *end == ':' && size >= 0) {
                meta->stamp.size = size;
                meta->stamp.write_time = strtoll(end + 1, &end, 10);
                // Device and inode follow when the platform has them
                if (end && *end == ':') {
                    meta->stamp.device = strtoull(end + 1, &end, 10);
                    if (end && *end == ':')
                        meta->stamp.inode = strtoull(end + 1, NULL, 10);
                }
            }
        }

        const char *path_start = second_nl + 1;
        if (*path_start && out_path) {
            *out_path = malloc(strlen(path_start) + 1);
            if (*out_path) {
                strcpy(*out_path, path_start);
            }
        }
    }
    meta->encoding = (enc >= 0 && enc <= (int) NPAD_ENC_ANSI) ? (TextEncoding) enc : NPAD_ENC_UTF8;
    meta->line_ending =
        (eol >= 0 && eol <= (int) NPAD_EOL_CR) ? (LineEnding) eol : NPAD_DEFAULT_LINE_ENDING;
}

// --- Writing -------------------------------------------------------------------

bool session_write_ex(const char *dir, const char *slot_id, const char *utf8_content,
                      const char *path, const SessionMeta *meta) {
    if (!dir || !utf8_content || !session_is_valid_slot_id(slot_id))
        return false;

    ensure_dir(dir);

    char *content_path = slot_path(dir, slot_id, ".txt");
    char *meta_path = slot_path(dir, slot_id, ".meta");
    bool ok = false;
    if (content_path && meta_path) {
        // Write content first, then meta: recovery keys off the .meta file,
        // so the content is always present by the time meta appears.
        ok = file_write_text_atomic(content_path, utf8_content);
        if (ok) {
            make_private(content_path);
            ok = write_meta_file(meta_path, path, meta);
        }
    }

    free(content_path);
    free(meta_path);
    return ok;
}

bool session_write(const char *dir, const char *slot_id, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending) {
    SessionMeta meta;
    default_meta(&meta);
    meta.encoding = encoding;
    meta.line_ending = line_ending;
    return session_write_ex(dir, slot_id, utf8_content, path, &meta);
}

// --- Claims --------------------------------------------------------------------

bool session_claim(const char *dir, const char *slot_id, const char *from_claimer,
                   const char *claimer) {
    if (!dir || !session_is_valid_slot_id(slot_id) || !session_is_valid_slot_id(claimer))
        return false;
    if (from_claimer && strcmp(from_claimer, claimer) == 0) {
        char *mine = claim_path(dir, slot_id, claimer);
        bool held = path_exists(mine);
        free(mine);
        return held;
    }

    char *source =
        from_claimer ? claim_path(dir, slot_id, from_claimer) : slot_path(dir, slot_id, ".meta");
    char *target = claim_path(dir, slot_id, claimer);
    // Whoever wins the rename owns the slot; the loser finds the source gone
    bool claimed = source && target && rename_utf8(source, target);
    free(source);
    free(target);
    return claimed;
}

bool session_release(const char *dir, const char *slot_id, const char *claimer) {
    char *claim = claim_path(dir, slot_id, claimer);
    char *meta = slot_path(dir, slot_id, ".meta");
    bool ok = false;
    if (claim && meta) {
        if (path_exists(meta)) {
            // The owner wrote the slot again after all (it was not dead), so
            // its meta is the newer one; the claimed copy is stale
            file_delete(claim);
            ok = true;
        } else {
            ok = rename_utf8(claim, meta);
        }
    }
    free(claim);
    free(meta);
    return ok;
}

void session_discard_claimed(const char *dir, const char *slot_id, const char *claimer) {
    char *claim = claim_path(dir, slot_id, claimer);
    char *content = slot_path(dir, slot_id, ".txt");
    char *handoff = slot_path(dir, slot_id, ".handoff");
    if (claim) {
        delete_path(content);
        delete_path(claim);
        delete_path(handoff);
    }
    free(claim);
    free(content);
    free(handoff);
}

SessionReadResult session_read_claimed(const char *dir, const char *slot_id, const char *claimer,
                                       char **content, char **path, SessionMeta *meta) {
    if (content)
        *content = NULL;
    if (path)
        *path = NULL;
    SessionMeta parsed;
    default_meta(&parsed);

    char *claim = claim_path(dir, slot_id, claimer);
    char *content_path = slot_path(dir, slot_id, ".txt");
    SessionReadResult result = SESSION_READ_FAILED;
    char *meta_text = NULL;
    char *text = NULL;
    char *parsed_path = NULL;

    if (!claim || !content_path)
        goto done;
    if (!path_exists(claim)) {
        result = SESSION_READ_GONE;
        goto done;
    }
    meta_text = file_read_text(claim);
    if (!meta_text)
        goto done; // There, but unreadable: keep it for another try
    if (!path_exists(content_path)) {
        result = SESSION_READ_EMPTY;
        goto done;
    }
    text = file_read_text(content_path);
    if (!text)
        goto done;

    parse_meta(meta_text, &parsed_path, &parsed);
    result = SESSION_READ_OK;
    if (content) {
        *content = text;
        text = NULL;
    }
    if (path) {
        *path = parsed_path;
        parsed_path = NULL;
    }

done:
    if (meta)
        *meta = parsed;
    free(parsed_path);
    free(text);
    free(meta_text);
    free(claim);
    free(content_path);
    return result;
}

bool session_adopt(const char *dir, const char *slot_id, const char *claimer,
                   const char *new_slot_id, const char *path, const SessionMeta *meta) {
    if (!session_is_valid_slot_id(new_slot_id) || !slot_id || strcmp(slot_id, new_slot_id) == 0)
        return false;

    char *claim = claim_path(dir, slot_id, claimer);
    char *content = slot_path(dir, slot_id, ".txt");
    char *handoff = slot_path(dir, slot_id, ".handoff");
    char *new_content = slot_path(dir, new_slot_id, ".txt");
    char *new_meta = slot_path(dir, new_slot_id, ".meta");
    bool ok = false;

    if (claim && content && handoff && new_content && new_meta && path_exists(claim) &&
        rename_utf8(content, new_content)) {
        make_private(new_content);
        if (write_meta_file(new_meta, path, meta)) {
            file_delete(claim);
            ok = true;
        } else if (rename_utf8(claim, new_meta)) {
            // No room for a fresh meta: the claimed one describes the same
            // document, it only lacks what this version adds
            make_private(new_meta);
            ok = true;
        } else {
            rename_utf8(new_content, content); // Put it back as it was
        }
        if (ok)
            file_delete(handoff);
    }

    free(claim);
    free(content);
    free(handoff);
    free(new_content);
    free(new_meta);
    return ok;
}

char *session_take_ex(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                      LineEnding *line_ending, bool *altered) {
    if (out_path)
        *out_path = NULL;
    if (altered)
        *altered = false;
    char me[SESSION_SLOT_ID_MAX];
    session_make_slot_id(me, sizeof(me));
    if (!session_claim(dir, slot_id, NULL, me))
        return NULL; // Missing or already claimed by another instance

    char *content = NULL;
    char *path = NULL;
    SessionMeta meta;
    switch (session_read_claimed(dir, slot_id, me, &content, &path, &meta)) {
        case SESSION_READ_OK:
            session_discard_claimed(dir, slot_id, me);
            if (out_path)
                *out_path = path;
            else
                free(path);
            if (encoding)
                *encoding = meta.encoding;
            if (line_ending)
                *line_ending = meta.line_ending;
            if (altered)
                *altered = meta.altered;
            return content;
        case SESSION_READ_EMPTY:
            session_discard_claimed(dir, slot_id, me); // Nothing in it to keep
            return NULL;
        case SESSION_READ_FAILED:
        case SESSION_READ_GONE:
        default:
            // Never delete what could not be read: give it back for next time
            session_release(dir, slot_id, me);
            return NULL;
    }
}

char *session_take(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending) {
    return session_take_ex(dir, slot_id, out_path, encoding, line_ending, NULL);
}

// --- Listing ---------------------------------------------------------------------

// Every entry name in dir, as malloc'd UTF-8 strings (NULL when empty)
static char **list_entries(const char *dir, int *count) {
    *count = 0;
    char **names = NULL;
    int n = 0, cap = 0;

#ifdef _WIN32
    char *pattern = file_join_paths(dir, "*");
    if (!pattern)
        return NULL;
    wchar_t wpattern[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wpattern, 1024) > 0) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(wpattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                int u8len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, NULL, 0, NULL, NULL);
                if (u8len <= 0 || n >= SESSION_MAX_ENTRIES)
                    continue;
                if (n >= cap) {
                    int new_cap = cap ? cap * 2 : 16;
                    char **grown = realloc(names, (size_t) new_cap * sizeof(char *));
                    if (!grown)
                        break;
                    names = grown;
                    cap = new_cap;
                }
                char *name = malloc((size_t) u8len);
                if (name) {
                    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, u8len, NULL, NULL);
                    names[n++] = name;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    free(pattern);
#else
    DIR *d = opendir(dir);
    if (d) {
        const struct dirent *entry;
        while ((entry = readdir(d)) != NULL && n < SESSION_MAX_ENTRIES) {
            if (entry->d_name[0] == '.')
                continue; // "." and ".." (npad never writes dot files here)
            if (n >= cap) {
                int new_cap = cap ? cap * 2 : 16;
                char **grown = realloc(names, (size_t) new_cap * sizeof(char *));
                if (!grown)
                    break;
                names = grown;
                cap = new_cap;
            }
            size_t len = strlen(entry->d_name);
            char *name = malloc(len + 1);
            if (name) {
                memcpy(name, entry->d_name, len + 1);
                names[n++] = name;
            }
        }
        closedir(d);
    }
#endif

    *count = n;
    if (n == 0) {
        free(names);
        return NULL;
    }
    return names;
}

// If name is "<valid slot id><suffix>", copy the id into out and return true
static bool slot_from_name(const char *name, const char *suffix, char *out, size_t size) {
    size_t len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (len <= suffix_len || strcmp(name + len - suffix_len, suffix) != 0)
        return false;
    size_t id_len = len - suffix_len;
    if (id_len >= size)
        return false;
    memcpy(out, name, id_len);
    out[id_len] = '\0';
    return session_is_valid_slot_id(out);
}

// If name is "<slot>.meta.claim-<claimer>", split it
static bool claim_from_name(const char *name, char *slot, char *claimer, size_t size) {
    const char *infix = strstr(name, CLAIM_INFIX);
    if (!infix)
        return false;
    size_t slot_len = (size_t) (infix - name);
    const char *rest = infix + strlen(CLAIM_INFIX);
    if (slot_len >= size || strlen(rest) >= size)
        return false;
    memcpy(slot, name, slot_len);
    slot[slot_len] = '\0';
    strcpy(claimer, rest);
    return session_is_valid_slot_id(slot) && session_is_valid_slot_id(claimer);
}

char **session_list_slots(const char *dir, int *count) {
    if (count)
        *count = 0;
    if (!dir || !count)
        return NULL;
    tighten_dir(dir);

    int entries = 0;
    char **names = list_entries(dir, &entries);
    char **slots = NULL;
    int n = 0;
    if (entries > 0) {
        slots = malloc((size_t) entries * sizeof(char *));
        for (int i = 0; slots && i < entries && n < SESSION_MAX_SLOTS; i++) {
            char id[SESSION_SLOT_ID_MAX];
            if (!slot_from_name(names[i], ".meta", id, sizeof(id)))
                continue; // Not a slot (or not an id npad could have made)
            char *copy = malloc(strlen(id) + 1);
            if (copy) {
                strcpy(copy, id);
                slots[n++] = copy;
            }
        }
    }
    session_free_slots(names, entries);

    *count = n;
    if (n == 0) {
        free(slots);
        return NULL;
    }
    return slots;
}

void session_free_slots(char **slots, int count) {
    if (!slots)
        return;
    for (int i = 0; i < count; i++) {
        free(slots[i]);
    }
    free(slots);
}

void session_clear_slot(const char *dir, const char *slot_id) {
    if (!dir || !slot_id)
        return;

    char *content_path = slot_path(dir, slot_id, ".txt");
    char *meta_path = slot_path(dir, slot_id, ".meta");
    delete_path(content_path);
    delete_path(meta_path);
    free(content_path);
    free(meta_path);
    session_clear_handoff(dir, slot_id); // Never outlive its slot
}

// --- Orphans ---------------------------------------------------------------------

// Whether any claim on slot_id exists right now (an old-style one included)
static bool slot_has_claim(const char *dir, const char *slot_id) {
    char *taking = slot_path(dir, slot_id, ".meta.taking");
    bool claimed = path_exists(taking);
    free(taking);
    if (claimed)
        return true;

    int entries = 0;
    char **names = list_entries(dir, &entries);
    for (int i = 0; i < entries && !claimed; i++) {
        char slot[SESSION_SLOT_ID_MAX], claimer[SESSION_SLOT_ID_MAX];
        if (claim_from_name(names[i], slot, claimer, sizeof(slot)) && strcmp(slot, slot_id) == 0)
            claimed = true;
    }
    session_free_slots(names, entries);
    return claimed;
}

static bool owner_is_gone(const char *slot_id, const char *own_slot_id, SessionLiveFn is_live,
                          void *ctx) {
    if (own_slot_id && strcmp(slot_id, own_slot_id) == 0)
        return false;
    return !is_live || !is_live(slot_id, ctx);
}

int session_recover_orphans(const char *dir, const char *own_slot_id, SessionLiveFn is_live,
                            void *ctx) {
    if (!dir)
        return 0;
    tighten_dir(dir);
    int restored = 0;
    char slot[SESSION_SLOT_ID_MAX], claimer[SESSION_SLOT_ID_MAX];

    // 1. Claims made by npad before 0.36, killed between its rename and its
    // delete: <slot>.meta.taking / <slot>.txt.taking. Those claims do not say
    // who made them, but the slot's owner was already gone when it was taken.
    int entries = 0;
    char **names = list_entries(dir, &entries);
    for (int i = 0; i < entries; i++) {
        bool is_meta = slot_from_name(names[i], ".meta.taking", slot, sizeof(slot));
        if (!is_meta && !slot_from_name(names[i], ".txt.taking", slot, sizeof(slot)))
            continue;
        if (!owner_is_gone(slot, own_slot_id, is_live, ctx))
            continue;
        char *meta = slot_path(dir, slot, ".meta");
        char *meta_taking = slot_path(dir, slot, ".meta.taking");
        char *content = slot_path(dir, slot, ".txt");
        char *content_taking = slot_path(dir, slot, ".txt.taking");
        if (content && content_taking && path_exists(content_taking) && !path_exists(content))
            rename_utf8(content_taking, content);
        if (is_meta && meta && meta_taking && !path_exists(meta) && path_exists(meta_taking)) {
            if (path_exists(content)) {
                if (rename_utf8(meta_taking, meta))
                    restored++;
            } else {
                file_delete(meta_taking); // Its text is gone: nothing to offer
            }
        }
        free(meta);
        free(meta_taking);
        free(content);
        free(content_taking);
    }
    session_free_slots(names, entries);

    // 2. Claims whose claimer died before it restored or gave the slot back
    names = list_entries(dir, &entries);
    for (int i = 0; i < entries; i++) {
        if (!claim_from_name(names[i], slot, claimer, sizeof(slot)))
            continue;
        if (!owner_is_gone(claimer, own_slot_id, is_live, ctx))
            continue; // Being handled by a running instance right now
        if (session_release(dir, slot, claimer))
            restored++;
    }
    session_free_slots(names, entries);

    // 3. Text with no meta: the very first snapshot was cut off between its
    // two files. The text is intact, only where it came from is unknown, so
    // it is offered as an untitled document.
    names = list_entries(dir, &entries);
    for (int i = 0; i < entries; i++) {
        if (!slot_from_name(names[i], ".txt", slot, sizeof(slot)))
            continue;
        char *meta = slot_path(dir, slot, ".meta");
        if (meta && !path_exists(meta) && owner_is_gone(slot, own_slot_id, is_live, ctx) &&
            !slot_has_claim(dir, slot) && write_meta_file(meta, NULL, NULL)) {
            restored++;
        }
        free(meta);
    }
    session_free_slots(names, entries);

    // 4. A meta whose text is missing, and a handoff marker left on its own,
    // hold nothing to restore; offering them would only show an empty window
    names = list_entries(dir, &entries);
    for (int i = 0; i < entries; i++) {
        bool is_meta = slot_from_name(names[i], ".meta", slot, sizeof(slot));
        if (!is_meta && !slot_from_name(names[i], ".handoff", slot, sizeof(slot)))
            continue;
        char *meta = slot_path(dir, slot, ".meta");
        char *content = slot_path(dir, slot, ".txt");
        bool empty = meta && content && !path_exists(content) && (is_meta || !path_exists(meta));
        if (empty && owner_is_gone(slot, own_slot_id, is_live, ctx) &&
            (is_meta || !slot_has_claim(dir, slot))) {
            if (is_meta)
                file_delete(meta);
            session_clear_handoff(dir, slot);
        }
        free(meta);
        free(content);
    }
    session_free_slots(names, entries);

    return restored;
}

// --- Handoff marker --------------------------------------------------------
// A zero-length "<slot>.handoff" sibling marks a slot that npad wrote because
// something else was closing it (an update, or Windows restarting), rather than
// because it crashed. The distinction is what lets the next launch restore it
// silently instead of asking "npad may have closed unexpectedly".

bool session_mark_handoff(const char *dir, const char *slot_id) {
    if (!dir || !slot_id)
        return false;

    ensure_dir(dir);

    char *path = slot_path(dir, slot_id, ".handoff");
    if (!path)
        return false;
    // Content is irrelevant - existence is the signal
    bool ok = file_write_text(path, "");
    if (ok)
        make_private(path);
    free(path);
    return ok;
}

bool session_is_handoff(const char *dir, const char *slot_id) {
    if (!dir || !slot_id)
        return false;

    char *path = slot_path(dir, slot_id, ".handoff");
    if (!path)
        return false;
    bool marked = file_exists(path);
    free(path);
    return marked;
}

void session_clear_handoff(const char *dir, const char *slot_id) {
    if (!dir || !slot_id)
        return;

    char *path = slot_path(dir, slot_id, ".handoff");
    if (path) {
        file_delete(path);
        free(path);
    }
}
