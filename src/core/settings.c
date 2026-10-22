/*
 * npad - Settings Management Implementation
 * Cross-platform settings storage using JSON format
 *
 * The model, since several npad windows (one process each) share one file:
 * - Each instance holds the whole store in memory and remembers which keys IT
 *   set or removed since it last saved ("unsaved changes").
 * - settings_save() is a read-modify-write: it re-reads settings.json, applies
 *   only this instance's unsaved changes on top, and writes the result through
 *   the atomic writer. So an instance never writes back values it merely
 *   loaded - the exit save no longer lets the last window to close overwrite
 *   what the others changed meanwhile.
 * - settings_load() (the reload a settings broadcast triggers) reads the file
 *   into a fresh store and swaps it in only when the read worked, re-applying
 *   this instance's unsaved changes, so a reload neither discards recent files
 *   or find history not yet saved nor empties the store when the file cannot
 *   be read at that moment.
 * - A list stored one key per position (Recent Files, find/replace history)
 *   is one unit: settings_set_list marks every position as changed and every
 *   position past its end as removed, so the save replaces the whole list
 *   with the version of the window that changed it last instead of mixing
 *   two windows' lists entry by entry.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
// fcntl locks, O_CLOEXEC and nanosleep (c99 mode hides them otherwise)
#define _POSIX_C_SOURCE 200809L
#endif

#include "settings.h"
#include "error.h"
#include "file_ops.h"
#include "thread_safety.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#define MAX_RECENT_FILES 10

// One setting. Entries are kept in a list in file (then insertion) order, so
// a save writes them in a stable order, and in a hash table for lookup.
typedef struct SettingEntry {
    char *key;
    char *value;
    bool raw;   // value is JSON kept verbatim from the file: a nested object or
                // array, or a bare number or true/false (see is_raw_scalar)
    bool dirty; // set or changed by this instance since its last save
    struct SettingEntry *prev, *next; // order
    struct SettingEntry *chain;       // hash bucket
} SettingEntry;

typedef struct {
    SettingEntry *head, *tail;
    SettingEntry **buckets;
    size_t bucket_count;
    size_t count;
} SettingStore;

// Global settings state
static SettingStore g_store;   // every setting
static SettingStore g_removed; // keys this instance removed and has not saved yet
static char *g_settings_file_path = NULL;

// Helper functions
static char *get_settings_directory(void);
static bool load_settings_from(const char *path, bool may_move_aside);
static void create_directory_recursive(const char *path);
#ifndef _WIN32
static char *settings_legacy_file_path(void);
#endif

// A settings value long enough to be nonsense rather than data. Real values are
// paths, face names and small numbers; the runaway-escaping bug produced one of
// 1.2 GB. Anything past this is dropped on load so a corrupt file cannot stop
// npad starting, and refused by settings_set_string so it is never written.
#define SETTINGS_MAX_VALUE_LEN 65536

// Keys, likewise: npad's longest is a few dozen bytes
#define SETTINGS_MAX_KEY_LEN 256

// npad uses a couple of hundred keys. A corrupt or crafted file of hundreds of
// thousands costs memory in every window (one process each), so loading stops
// at this many and a save keeps a copy of the original (see settings_save).
#define SETTINGS_MAX_KEYS 10000

// The whole file, likewise. Loading is a full read into memory, so an absurd
// file must be rejected before it is read rather than after.
#define SETTINGS_MAX_FILE_BYTES (8 * 1024 * 1024)

// --- Store primitives ------------------------------------------------------
//
// None of these lock: callers hold g_settings_mutex (or own a private store).

static char *dup_range(const char *s, size_t n) {
    char *copy = malloc(n + 1);
    if (copy) {
        memcpy(copy, s, n);
        copy[n] = '\0';
    }
    return copy;
}

static char *dup_string(const char *s) {
    return dup_range(s, strlen(s));
}

static size_t hash_key(const char *key) {
    size_t h = 2166136261u; // FNV-1a
    for (const unsigned char *p = (const unsigned char *) key; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

static SettingEntry *store_find(const SettingStore *store, const char *key) {
    if (!store->buckets || !key)
        return NULL;
    for (SettingEntry *e = store->buckets[hash_key(key) % store->bucket_count]; e; e = e->chain) {
        if (strcmp(e->key, key) == 0)
            return e;
    }
    return NULL;
}

// Double the bucket array. False only when there is none at all to use.
static bool store_grow(SettingStore *store) {
    size_t count = store->bucket_count ? store->bucket_count * 2 : 64;
    SettingEntry **buckets = calloc(count, sizeof(*buckets));
    if (!buckets)
        return store->buckets != NULL; // Longer chains, but still correct
    for (SettingEntry *e = store->head; e; e = e->next) {
        size_t i = hash_key(e->key) % count;
        e->chain = buckets[i];
        buckets[i] = e;
    }
    free(store->buckets);
    store->buckets = buckets;
    store->bucket_count = count;
    return true;
}

// Set key to value, appending a new key at the end. The entry, or NULL when
// out of memory (the store is then unchanged).
static SettingEntry *store_put(SettingStore *store, const char *key, const char *value, bool raw) {
    SettingEntry *e = store_find(store, key);
    if (e) {
        char *copy = dup_string(value);
        if (!copy)
            return NULL;
        free(e->value);
        e->value = copy;
        e->raw = raw;
        return e;
    }

    if (store->count >= store->bucket_count && !store_grow(store))
        return NULL;
    e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->key = dup_string(key);
    e->value = dup_string(value);
    if (!e->key || !e->value) {
        free(e->key);
        free(e->value);
        free(e);
        return NULL;
    }
    e->raw = raw;
    size_t i = hash_key(key) % store->bucket_count;
    e->chain = store->buckets[i];
    store->buckets[i] = e;
    e->prev = store->tail;
    if (store->tail)
        store->tail->next = e;
    else
        store->head = e;
    store->tail = e;
    store->count++;
    return e;
}

static void store_unlink_free(SettingStore *store, SettingEntry *e) {
    SettingEntry **link = &store->buckets[hash_key(e->key) % store->bucket_count];
    while (*link && *link != e)
        link = &(*link)->chain;
    if (*link)
        *link = e->chain;
    if (e->prev)
        e->prev->next = e->next;
    else
        store->head = e->next;
    if (e->next)
        e->next->prev = e->prev;
    else
        store->tail = e->prev;
    store->count--;
    free(e->key);
    free(e->value);
    free(e);
}

static bool store_remove(SettingStore *store, const char *key) {
    SettingEntry *e = store_find(store, key);
    if (!e)
        return false;
    store_unlink_free(store, e);
    return true;
}

static void store_free(SettingStore *store) {
    SettingEntry *e = store->head;
    while (e) {
        SettingEntry *next = e->next;
        free(e->key);
        free(e->value);
        free(e);
        e = next;
    }
    free(store->buckets);
    memset(store, 0, sizeof(*store));
}

static bool has_unsaved_changes_locked(void) {
    if (g_removed.count > 0)
        return true;
    for (const SettingEntry *e = g_store.head; e; e = e->next) {
        if (e->dirty)
            return true;
    }
    return false;
}

// Put this instance's unsaved changes (values it set, keys it removed) on top
// of `onto`, marking the values dirty there too when keep_dirty. False when out
// of memory, with `onto` part-merged (callers then discard it).
static bool apply_unsaved_changes(SettingStore *onto, bool keep_dirty) {
    for (const SettingEntry *e = g_store.head; e; e = e->next) {
        if (!e->dirty)
            continue;
        SettingEntry *put = store_put(onto, e->key, e->value, e->raw);
        if (!put)
            return false;
        put->dirty = keep_dirty;
    }
    for (const SettingEntry *r = g_removed.head; r; r = r->next)
        store_remove(onto, r->key);
    return true;
}

static bool parse_settings_file(const char *content, SettingStore *out, bool *complete);
static char *serialize_settings(const SettingStore *store);

bool settings_init(void) {
    // Get settings directory
    char *settings_dir = get_settings_directory();
    if (!settings_dir)
        return false;

    // Create settings file path
    g_settings_file_path = file_join_paths(settings_dir, "settings.json");
    free(settings_dir);

    if (!g_settings_file_path)
        return false;

#ifndef _WIN32
    // The config directory must exist before anything else writes under it:
    // crash-recovery snapshots go in <dir>/recovery and only create that last
    // level, and a first run (or a newly set $XDG_CONFIG_HOME) reaches here
    // before any settings save has made the parents
    char *config_dir = file_get_directory(g_settings_file_path);
    if (config_dir) {
        create_directory_recursive(config_dir);
        free(config_dir);
    }

    if (!file_exists(g_settings_file_path)) {
        // Nothing at the new location. An npad that predates $XDG_CONFIG_HOME
        // support kept its settings in ~/.config/npad, so read those instead of
        // starting from defaults. READ only: the old file is neither moved nor
        // deleted (it is the user's only copy until the next save writes the
        // new one), and a runaway oversized one is salvaged in memory without
        // being renamed.
        char *legacy = settings_legacy_file_path();
        if (legacy && strcmp(legacy, g_settings_file_path) != 0 && file_exists(legacy)) {
            load_settings_from(legacy, false);
            free(legacy);
            return true;
        }
        free(legacy);
    }
#endif

    // Load existing settings
    settings_load();

    return true;
}

void settings_cleanup(void) {
    npad_mutex_lock(&g_settings_mutex);
    store_free(&g_store);
    store_free(&g_removed);
    npad_mutex_unlock(&g_settings_mutex);

    if (g_settings_file_path) {
        free(g_settings_file_path);
        g_settings_file_path = NULL;
    }
}

// A raw entry holding a bare number or true/false (kept as the file typed it)
// rather than a nested object or array
static bool is_raw_scalar(const SettingEntry *e) {
    return e->raw && e->value[0] != '{' && e->value[0] != '[';
}

// Set key, recording it as this instance's change. Setting a value the store
// already holds is not a change (so re-asserting every option on each search
// does not later overwrite another window's newer choice), unless force_dirty:
// the window geometry written at exit always counts, as in Notepad, where the
// window closed last decides where the next one opens, and so does every
// position of a list written whole (settings_set_list). Caller holds the lock.
static bool set_string_locked(const char *key, const char *value, bool force_dirty) {
    SettingEntry *entry = store_find(&g_store, key);
    // A bare 5 or true from the file is the same setting as "5" or "true", so
    // re-asserting it is no change either, and it keeps its JSON type
    if (entry && (!entry->raw || is_raw_scalar(entry)) && strcmp(entry->value, value) == 0) {
        if (force_dirty) {
            entry->dirty = true;
            store_remove(&g_removed, key);
        }
        return true;
    }
    if (!entry && g_store.count >= SETTINGS_MAX_KEYS)
        return false;
    entry = store_put(&g_store, key, value, false);
    if (entry) {
        entry->dirty = true;
        store_remove(&g_removed, key);
    } else {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, key, "Failed to allocate memory for setting");
    }
    return entry != NULL;
}

// The reader drops what is longer than this, so writing it would only lose it
// silently on the next start
static bool setting_fits(const char *key, const char *value) {
    if (strlen(key) > SETTINGS_MAX_KEY_LEN || strlen(value) > SETTINGS_MAX_VALUE_LEN) {
        NPAD_ERROR_WARNING(NPAD_ERROR_INVALID_PARAM, 0, "Settings",
                           "Refusing to store an implausibly long setting");
        return false;
    }
    return true;
}

static bool set_string_internal(const char *key, const char *value, bool force_dirty) {
    if (!key || !value || !setting_fits(key, value))
        return false;
    npad_mutex_lock(&g_settings_mutex);
    bool ok = set_string_locked(key, value, force_dirty);
    npad_mutex_unlock(&g_settings_mutex);
    return ok;
}

bool settings_set_string(const char *key, const char *value) {
    return set_string_internal(key, value, false);
}

// Record key as removed by this instance whether or not it holds it, so the
// next save removes it from the file even when only another window had it.
// Caller holds the lock.
static void remove_key_always_locked(const char *key) {
    store_remove(&g_store, key);
    if (!store_find(&g_removed, key))
        store_put(&g_removed, key, "", false);
}

bool settings_set_list(const char *prefix, char *const *values, int count, int capacity) {
    if (!prefix || count < 0 || capacity < 0 || count > capacity || (count > 0 && !values))
        return false;

    char key[SETTINGS_MAX_KEY_LEN + 1];
    for (int i = 0; i < capacity; i++) {
        int n = snprintf(key, sizeof(key), "%s_%d", prefix, i);
        if (n < 0 || (size_t) n >= sizeof(key))
            return false; // Prefix too long to make the keys
        if (i < count && (!values[i] || !setting_fits(key, values[i])))
            return false;
    }

    // One unit, under one lock: every position is this instance's change, the
    // ones it already held included, and every position past the end is a
    // removal, held here or not. Per key, an unchanged position was not saved
    // and a removal of a key only another window had was not recorded, so the
    // save merged two windows' lists entry by entry - duplicates, entries that
    // survived Clear Recent Files, and gaps that hid the rest of the history.
    bool ok = true;
    npad_mutex_lock(&g_settings_mutex);
    for (int i = 0; i < capacity; i++) {
        snprintf(key, sizeof(key), "%s_%d", prefix, i);
        if (i < count) {
            if (!set_string_locked(key, values[i], true))
                ok = false;
        } else {
            remove_key_always_locked(key);
        }
    }
    npad_mutex_unlock(&g_settings_mutex);
    return ok;
}

int settings_count(void) {
    npad_mutex_lock(&g_settings_mutex);
    int count = (int) g_store.count;
    npad_mutex_unlock(&g_settings_mutex);
    return count;
}

char *settings_get_string(const char *key, const char *default_value) {
    if (!key)
        return NULL;

    npad_mutex_lock(&g_settings_mutex);
    const SettingEntry *entry = store_find(&g_store, key);
    char *result = NULL;
    if (entry)
        result = dup_string(entry->value);
    else if (default_value)
        result = dup_string(default_value);
    npad_mutex_unlock(&g_settings_mutex);
    return result;
}

// Whole-string integer, optionally surrounded by spaces. False for anything
// else - "ten", "", "12abc", or a value outside int - so the getter falls back
// to its default rather than reading 0 the way atoi did.
static bool parse_int_text(const char *s, int *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s)
        return false;
    while (isspace((unsigned char) *end))
        end++;
    if (*end || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return false;
    *out = (int) v;
    return true;
}

// The decimal separator in the stored text is always '.', but strtod and
// printf use the locale's (GTK sets the user's locale, and a German one uses
// ','), so translate at the boundary
static char locale_decimal_point(void) {
    const struct lconv *lc = localeconv();
    return (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point[0] : '.';
}

static bool parse_double_text(const char *s, double *out) {
    char buf[64];
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof(buf))
        return false;
    memcpy(buf, s, n + 1);
    char dp = locale_decimal_point();
    if (dp != '.') {
        for (char *p = buf; *p; p++) {
            if (*p == '.')
                *p = dp;
        }
    }
    char *end = NULL;
    errno = 0;
    double v = strtod(buf, &end);
    if (end == buf)
        return false;
    while (isspace((unsigned char) *end))
        end++;
    if (*end || errno == ERANGE || !isfinite(v))
        return false;
    *out = v;
    return true;
}

static bool ascii_equal_ci(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b))
            return false;
    }
    return *a == *b;
}

bool settings_set_int(const char *key, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return settings_set_string(key, buffer);
}

int settings_get_int(const char *key, int default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    int result = default_value;
    (void) parse_int_text(str_value, &result); // Leaves the default on malformed text
    free(str_value);
    return result;
}

bool settings_set_bool(const char *key, bool value) {
    return settings_set_string(key, value ? "true" : "false");
}

bool settings_get_bool(const char *key, bool default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    // Anything that is not recognisably true or false keeps the default,
    // rather than reading as false
    bool result = default_value;
    if (ascii_equal_ci(str_value, "true") || strcmp(str_value, "1") == 0)
        result = true;
    else if (ascii_equal_ci(str_value, "false") || strcmp(str_value, "0") == 0)
        result = false;
    free(str_value);
    return result;
}

bool settings_set_double(const char *key, double value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.6f", value);
    char dp = locale_decimal_point();
    if (dp != '.') {
        for (char *p = buffer; *p; p++) {
            if (*p == dp)
                *p = '.';
        }
    }
    return settings_set_string(key, buffer);
}

double settings_get_double(const char *key, double default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    double result = default_value;
    (void) parse_double_text(str_value, &result); // Leaves the default on malformed text
    free(str_value);
    return result;
}

bool settings_has_key(const char *key) {
    if (!key)
        return false;

    npad_mutex_lock(&g_settings_mutex);
    bool found = store_find(&g_store, key) != NULL;
    npad_mutex_unlock(&g_settings_mutex);
    return found;
}

// Drop an entry, remembering the removal so the next save removes the key from
// the file too (rather than another window's copy of it surviving the merge)
static void remove_entry_locked(SettingEntry *entry) {
    store_put(&g_removed, entry->key, "", false);
    store_unlink_free(&g_store, entry);
}

bool settings_remove_key(const char *key) {
    if (!key)
        return false;

    npad_mutex_lock(&g_settings_mutex);
    SettingEntry *entry = store_find(&g_store, key);
    if (entry)
        remove_entry_locked(entry);
    npad_mutex_unlock(&g_settings_mutex);
    return entry != NULL;
}

bool settings_clear_all(void) {
    npad_mutex_lock(&g_settings_mutex);
    store_free(&g_store);
    store_free(&g_removed);
    npad_mutex_unlock(&g_settings_mutex);
    return true;
}

bool settings_has_unsaved_changes(void) {
    npad_mutex_lock(&g_settings_mutex);
    bool unsaved = has_unsaved_changes_locked();
    npad_mutex_unlock(&g_settings_mutex);
    return unsaved;
}

void settings_discard_unsaved_changes(void) {
    npad_mutex_lock(&g_settings_mutex);
    for (SettingEntry *e = g_store.head; e; e = e->next)
        e->dirty = false;
    store_free(&g_removed);
    npad_mutex_unlock(&g_settings_mutex);
}

void settings_discard_unsaved_change(const char *key) {
    if (!key)
        return;
    npad_mutex_lock(&g_settings_mutex);
    SettingEntry *entry = store_find(&g_store, key);
    if (entry)
        entry->dirty = false;
    store_remove(&g_removed, key);
    npad_mutex_unlock(&g_settings_mutex);
}

int settings_reset_except_prefixes(const char *const *keep_prefixes, int prefix_count) {
    int removed = 0;
    npad_mutex_lock(&g_settings_mutex);

    SettingEntry *current = g_store.head;
    while (current) {
        SettingEntry *next = current->next;
        bool keep = false;
        for (int i = 0; i < prefix_count && !keep; i++) {
            const char *p = keep_prefixes[i];
            if (p && strncmp(current->key, p, strlen(p)) == 0)
                keep = true;
        }
        if (!keep) {
            remove_entry_locked(current);
            removed++;
        }
        current = next;
    }

    npad_mutex_unlock(&g_settings_mutex);
    return removed;
}

// Create a directory and any missing parent directories
static void create_directory_recursive(const char *path) {
    if (!path || !path[0])
        return;

    char *copy = malloc(strlen(path) + 1);
    if (!copy)
        return;
    strcpy(copy, path);

    // Create each intermediate component (skip drive/root prefixes)
    for (char *p = copy + 1; *p; p++) {
        if ((*p == '/' || *p == '\\') && *(p - 1) != ':') {
            char saved = *p;
            *p = '\0';
#ifdef _WIN32
            CreateDirectory(copy, NULL);
#else
            mkdir(copy, 0700); // Private: settings and recovery snapshots live here
#endif
            *p = saved;
        }
    }

#ifdef _WIN32
    CreateDirectory(copy, NULL);
#else
    mkdir(copy, 0700);
#endif
    free(copy);
}

static char *path_with_suffix(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    char *s = malloc(a + b + 1);
    if (s) {
        memcpy(s, path, a);
        memcpy(s + a, suffix, b + 1);
    }
    return s;
}

// --- Reading the file ------------------------------------------------------

typedef enum {
    SOURCE_MISSING,    // No file, or an empty one: nothing to read
    SOURCE_OK,         // Read and parsed in full
    SOURCE_PARTIAL,    // Only a prefix parsed (truncated or malformed)
    SOURCE_OVERSIZED,  // Too large to read whole: what a bounded prefix held
    SOURCE_NOT_JSON,   // Read, but holds no settings object at all
    SOURCE_UNREADABLE, // Exists, but could not be read just now
} SettingsSource;

// A free name beside path for keeping an unreadable original:
// settings.json.corrupt, or .corrupt.1 to .corrupt.99 when that is taken. An
// earlier copy is never overwritten - it can be the only full copy of a
// runaway file the load moved aside. NULL when every name is taken.
static char *free_aside_path(const char *path) {
    for (int i = 0; i < 100; i++) {
        char suffix[24];
        if (i == 0)
            snprintf(suffix, sizeof(suffix), ".corrupt");
        else
            snprintf(suffix, sizeof(suffix), ".corrupt.%d", i);
        char *candidate = path_with_suffix(path, suffix);
        if (!candidate || !file_exists(candidate))
            return candidate;
        free(candidate);
    }
    return NULL;
}

// Move an oversized settings file out of the way, without reading it: the
// rename is the only way to keep it that costs no memory. The name it went
// to (malloc'd), or NULL when it could not be moved.
static char *move_aside(const char *path) {
    char *aside = free_aside_path(path);
    if (aside && !file_rename(path, aside)) {
        free(aside);
        aside = NULL;
    }
    if (aside)
        NPAD_ERROR_WARNING(NPAD_ERROR_FILE_IO, 0, "Settings",
                           "settings.json was implausibly large; recoverable settings were "
                           "salvaged and the original moved aside as settings.json.corrupt");
    return aside;
}

static bool is_blank(const char *s) {
    for (; *s; s++) {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
            return false;
    }
    return true;
}

// Read `path` into `out`, a private store. Nothing is renamed here: the
// callers decide what happens to an oversized file (load_settings_from moves
// it aside when it may, settings_save always does before replacing it).
static SettingsSource read_settings_file(const char *path, SettingStore *out) {
    if (!path || !file_exists(path))
        return SOURCE_MISSING;

    // An implausibly large settings file cannot be loaded whole: that full read,
    // multiplied by one process per window, is what exhausted 64 GB of RAM.
    //
    // But it must not simply be DISCARDED either. v0.28.7 did exactly that, and
    // since main() rewrites settings.json on every exit, the original was then
    // overwritten with defaults - so upgrading silently lost the user's real
    // settings. Instead: salvage what can be parsed from a bounded prefix (the
    // runaway value is a single entry, and everything written before it is
    // intact), then move the original aside so it is recoverable by hand and is
    // not read again.
    size_t size = file_get_size(path);
    if (size > SETTINGS_MAX_FILE_BYTES) {
        char *prefix = file_read_text_limited(path, SETTINGS_MAX_FILE_BYTES);
        if (!prefix)
            return SOURCE_UNREADABLE;
        bool complete = false;
        parse_settings_file(prefix, out, &complete); // Truncated final entry is not stored
        free(prefix);
        return SOURCE_OVERSIZED;
    }

    char *content = file_read_text(path);
    if (!content)
        return SOURCE_UNREADABLE;

    SettingsSource source;
    bool complete = false;
    if (is_blank(content))
        source = SOURCE_MISSING;
    else if (!parse_settings_file(content, out, &complete))
        source = SOURCE_NOT_JSON;
    else
        source = complete ? SOURCE_OK : SOURCE_PARTIAL;
    free(content);
    return source;
}

// --- Cross-process save lock -------------------------------------------------
//
// A save is read-modify-write, so two windows saving at the same instant could
// each read the file before the other wrote it, and the second write would drop
// the first one's change. A lock file beside settings.json serialises saves.
// Best effort: when it cannot be had within about two seconds (or at all, on a
// filesystem without locks) the save goes ahead unlocked.

typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
} SaveLock;

static void save_lock_acquire(SaveLock *lock, const char *settings_path) {
#ifdef _WIN32
    lock->handle = INVALID_HANDLE_VALUE;
#else
    lock->fd = -1;
#endif
    char *lock_path = path_with_suffix(settings_path, ".lock");
    if (!lock_path)
        return;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, lock_path, -1, NULL, 0);
    wchar_t *wpath = wlen > 0 ? malloc((size_t) wlen * sizeof(wchar_t)) : NULL;
    if (wpath && MultiByteToWideChar(CP_UTF8, 0, lock_path, -1, wpath, wlen) > 0) {
        // No sharing, and deleted when closed - also when the process dies
        for (int attempt = 0; attempt < 100; attempt++) {
            HANDLE h = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                lock->handle = h;
                break;
            }
            if (GetLastError() != ERROR_SHARING_VIOLATION)
                break; // Not held by another window (a read-only folder, say)
            Sleep(20);
        }
    }
    free(wpath);
#else
    int fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        for (int attempt = 0; attempt < 100; attempt++) {
            if (fcntl(fd, F_SETLK, &fl) == 0) {
                lock->fd = fd;
                break;
            }
            if (errno != EACCES && errno != EAGAIN)
                break;
            struct timespec pause = { 0, 20L * 1000 * 1000 };
            nanosleep(&pause, NULL);
        }
        if (lock->fd < 0)
            close(fd);
    }
#endif
    free(lock_path);
}

static void save_lock_release(const SaveLock *lock) {
#ifdef _WIN32
    if (lock->handle != INVALID_HANDLE_VALUE)
        CloseHandle(lock->handle);
#else
    if (lock->fd >= 0)
        close(lock->fd); // Releases the lock; the empty file stays for next time
#endif
}

// Keep a settings file that could not be read in full before a save replaces
// it, so what the reader could not make sense of is not lost for good. A file
// within the size cap is copied, so settings.json stays in place should the
// write then fail. A larger one - oversized at this read, or grown past the
// cap since (an older npad still running, say) - is renamed instead: copying
// is a full read into memory, which the cap exists to prevent. *moved_to is
// then the name it went to (malloc'd), for settings_save to put it back if the
// write fails; otherwise NULL.
static bool preserve_original(const char *path, bool oversized, char **moved_to) {
    *moved_to = NULL;
    if (oversized || file_get_size(path) > SETTINGS_MAX_FILE_BYTES) {
        *moved_to = move_aside(path);
        return *moved_to != NULL;
    }
    char *aside = free_aside_path(path);
    bool ok = aside && file_copy(path, aside);
    free(aside);
    if (ok)
        NPAD_ERROR_WARNING(NPAD_ERROR_FILE_IO, 0, "Settings",
                           "settings.json could not be read in full; a copy was kept beside it "
                           "as settings.json.corrupt");
    else
        NPAD_ERROR_WARNING(NPAD_ERROR_FILE_IO, 0, "Settings",
                           "settings.json could not be read in full and no copy of it could be "
                           "kept, so settings were not saved");
    return ok;
}

bool settings_save(void) {
    if (!g_settings_file_path)
        return false;

    // Ensure the settings directory (and its parents) exist
    char *dir = file_get_directory(g_settings_file_path);
    if (dir) {
        create_directory_recursive(dir);
        free(dir);
    }

    SaveLock lock;
    save_lock_acquire(&lock, g_settings_file_path);

    // Read-modify-write: start from what the file holds NOW (another window may
    // have saved since this one loaded) and apply only this instance's own
    // changes, so values it merely loaded are never written back over newer
    // ones. Writing the whole in-memory store let whichever window closed last
    // undo every other window's changes.
    SettingStore merged;
    memset(&merged, 0, sizeof(merged));
    SettingsSource source = read_settings_file(g_settings_file_path, &merged);
    bool ok = false;
    bool do_write = true;
    char *content = NULL;
    char *moved_to = NULL; // Where preserve_original renamed the original, if it did

    npad_mutex_lock(&g_settings_mutex);
    if (source == SOURCE_UNREADABLE) {
        // What is there cannot be seen, so writing now could destroy another
        // window's settings. Keep the changes for a later save.
        NPAD_ERROR_WARNING(NPAD_ERROR_FILE_IO, 0, "Settings",
                           "settings.json could not be read, so settings were not saved");
        do_write = false;
    } else if (source == SOURCE_OK) {
        if (!has_unsaved_changes_locked()) {
            ok = true; // Nothing of ours to add: leave the file alone
            do_write = false;
        } else if (!apply_unsaved_changes(&merged, false)) {
            do_write = false;
        }
    } else if (source == SOURCE_PARTIAL || source == SOURCE_OVERSIZED) {
        do_write = preserve_original(g_settings_file_path, source == SOURCE_OVERSIZED, &moved_to) &&
                   apply_unsaved_changes(&merged, false);
    } else {
        // Nothing to merge with - no file yet, an empty one, one deleted by
        // hand, or settings read from the pre-XDG location - or a file that is
        // not settings at all (kept aside first): write the whole store
        do_write =
            source != SOURCE_NOT_JSON || preserve_original(g_settings_file_path, false, &moved_to);
        store_free(&merged);
        for (const SettingEntry *e = g_store.head; do_write && e; e = e->next) {
            if (!store_put(&merged, e->key, e->value, e->raw))
                do_write = false;
        }
    }

    if (do_write)
        content = serialize_settings(&merged);
    // Atomic write (temp file + verify + rename) so an interrupted save can
    // never truncate or corrupt the existing settings file
    if (content && file_write_text_atomic(g_settings_file_path, content)) {
#ifndef _WIN32
        // Recent paths and Find history are nobody else's business
        chmod(g_settings_file_path, 0600);
#endif
        // The file now holds exactly the merged store: adopt it, so this
        // instance also sees what the others saved, and nothing is unsaved
        store_free(&g_store);
        g_store = merged;
        memset(&merged, 0, sizeof(merged));
        store_free(&g_removed);
        ok = true;
    } else if (moved_to && !file_exists(g_settings_file_path)) {
        // The original was renamed aside but nothing replaced it: put it back,
        // so a failed save leaves the file where it was
        file_rename(moved_to, g_settings_file_path);
    }
    npad_mutex_unlock(&g_settings_mutex);

    free(moved_to);
    free(content);
    store_free(&merged);
    save_lock_release(&lock);
    return ok;
}

bool settings_load(void) {
    return load_settings_from(g_settings_file_path, true);
}

// Load `path` into the store, keeping this instance's unsaved changes on top.
// may_move_aside is false for a fallback file we do not own the right to
// rename (see settings_init).
static bool load_settings_from(const char *path, bool may_move_aside) {
    SettingStore fresh;
    memset(&fresh, 0, sizeof(fresh));
    SettingsSource source = read_settings_file(path, &fresh);
    if (source == SOURCE_OVERSIZED && may_move_aside) {
        // Salvaged; now out of the load path so it is not read again, and kept
        // (never over an earlier copy) to be recovered by hand
        free(move_aside(path));
    }
    if (source == SOURCE_MISSING) {
        store_free(&fresh);
        return true; // No file to load, that's OK: keep what is held
    }
    if (source == SOURCE_UNREADABLE || source == SOURCE_NOT_JSON) {
        // Keep what is held. Clearing first is how a reload at a moment the
        // file could not be read (a sharing violation, an offline redirected
        // AppData) left the window on defaults, which its exit save then wrote.
        store_free(&fresh);
        return false;
    }

    // Parsed into a fresh store and swapped in only now, with what this
    // instance has set or removed and not yet saved put back on top: a reload
    // must not throw away a Recent Files entry or a search not yet saved
    npad_mutex_lock(&g_settings_mutex);
    bool ok = apply_unsaved_changes(&fresh, true);
    if (ok) {
        store_free(&g_store);
        g_store = fresh;
    } else {
        store_free(&fresh);
    }
    npad_mutex_unlock(&g_settings_mutex);
    return ok;
}

const char *settings_get_file_path(void) {
    return g_settings_file_path;
}

bool settings_set_file_path(const char *path) {
    if (!path)
        return false;
    char *copy = malloc(strlen(path) + 1);
    if (!copy)
        return false;
    strcpy(copy, path);
    free(g_settings_file_path);
    g_settings_file_path = copy;
    return true;
}

char *settings_get_config_dir(void) {
    if (!g_settings_file_path)
        return NULL;
    return file_get_directory(g_settings_file_path);
}

static bool set_window_int(const char *key, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return set_string_internal(key, buffer, true);
}

// Always this instance's change, even when unchanged from what it loaded: the
// window closed last decides where the next one opens, as in Notepad
bool settings_save_window_state(int x, int y, int width, int height, bool maximized) {
    return set_window_int("window_x", x) && set_window_int("window_y", y) &&
           set_window_int("window_width", width) && set_window_int("window_height", height) &&
           set_string_internal("window_maximized", maximized ? "true" : "false", true);
}

bool settings_load_window_state(int *x, int *y, int *width, int *height, bool *maximized) {
    if (!x || !y || !width || !height || !maximized)
        return false;

    *x = settings_get_int("window_x", 100);
    *y = settings_get_int("window_y", 100);
    *width = settings_get_int("window_width", 800);
    *height = settings_get_int("window_height", 600);
    *maximized = settings_get_bool("window_maximized", false);

    return true;
}

// User-configurable cap on the recent files list (0 disables it)
static int recent_files_limit(void) {
    int max = settings_get_int("recent_files_max", MAX_RECENT_FILES);
    if (max < 0)
        max = 0;
    if (max > MAX_RECENT_FILES)
        max = MAX_RECENT_FILES;
    return max;
}

// The stored list, in order, up to the limit. only_existing filters it to
// files that exist right now, which is for showing it, never for rewriting
// it: a file on a sleeping NAS or a removed USB stick is only unreachable for
// the moment, and filtering when the list was rewritten dropped it for good.
static char **read_recent_list(int *count, bool only_existing) {
    if (!count)
        return NULL;

    char **files = malloc(MAX_RECENT_FILES * sizeof(char *));
    if (!files) {
        *count = 0;
        return NULL;
    }
    *count = 0;

    int limit = recent_files_limit();
    for (int i = 0; i < MAX_RECENT_FILES; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent_file_%d", i);

        char *filepath = settings_get_string(key, NULL);
        if (filepath && filepath[0] && *count < limit &&
            (!only_existing || file_exists(filepath))) {
            files[*count] = filepath;
            (*count)++;
        } else {
            free(filepath);
        }
    }

    if (*count == 0) {
        free(files);
        return NULL;
    }
    return files;
}

bool settings_add_recent_file(const char *filepath) {
    if (!filepath)
        return false;

    int limit = recent_files_limit();
    if (limit == 0)
        return true; // Recent files disabled

    // Get current recent files - all of them, reachable or not
    int count;
    char **recent_files = read_recent_list(&count, false);

    // Check if file is already in the list
    int existing_index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(recent_files[i], filepath) == 0) {
            existing_index = i;
            break;
        }
    }

    // Create new list with the file at the top
    char **new_list = malloc(MAX_RECENT_FILES * sizeof(char *));
    if (!new_list) {
        if (recent_files) {
            settings_free_recent_files(recent_files, count);
        }
        return false;
    }
    int new_count = 0;

    // Add the new file first
    new_list[new_count] = malloc(strlen(filepath) + 1);
    if (!new_list[new_count]) {
        free(new_list);
        if (recent_files) {
            settings_free_recent_files(recent_files, count);
        }
        return false;
    }
    strcpy(new_list[new_count], filepath);
    new_count++;

    // Add existing files (except the one we're moving to top)
    for (int i = 0; i < count && new_count < limit; i++) {
        if (i != existing_index && recent_files && recent_files[i]) {
            new_list[new_count] = malloc(strlen(recent_files[i]) + 1);
            if (!new_list[new_count]) {
                // Cleanup on failure
                for (int j = 0; j < new_count; j++) {
                    free(new_list[j]);
                }
                free(new_list);
                settings_free_recent_files(recent_files, count);
                return false;
            }
            strcpy(new_list[new_count], recent_files[i]);
            new_count++;
        }
    }

    // Save the new list, as a whole: the window that changed it last decides
    // all of it, rather than its changed positions being mixed with another
    // window's list (which listed a file twice)
    bool ok = settings_set_list("recent_file", new_list, new_count, MAX_RECENT_FILES);

    // Cleanup
    for (int i = 0; i < new_count; i++) {
        free(new_list[i]);
    }
    free(new_list);

    settings_free_recent_files(recent_files, count);

    return ok;
}

char **settings_get_recent_files(int *count) {
    return read_recent_list(count, true);
}

void settings_free_recent_files(char **files, int count) {
    if (!files)
        return;

    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}

bool settings_clear_recent_files(void) {
    // Every position is removed from the file, also those only another window
    // held: removing just the keys held here left that window's entries behind
    return settings_set_list("recent_file", NULL, 0, MAX_RECENT_FILES);
}

// Helper function implementations

char *settings_resolve_config_dir(const char *xdg_config_home, const char *home) {
    // $XDG_CONFIG_HOME counts only when set and absolute: the XDG spec says a
    // relative value is invalid and must be ignored
    if (xdg_config_home && xdg_config_home[0] == '/')
        return file_join_paths(xdg_config_home, "npad");
    return settings_resolve_legacy_dir(home);
}

char *settings_resolve_legacy_dir(const char *home) {
    if (!home || !home[0])
        return NULL;
    char *config_dir = file_join_paths(home, ".config");
    if (!config_dir)
        return NULL;
    char *npad_dir = file_join_paths(config_dir, "npad");
    free(config_dir);
    return npad_dir;
}

#ifndef _WIN32
static const char *settings_home_dir(void) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        const struct passwd *pw = getpwuid(getuid());
        if (pw)
            home = pw->pw_dir;
    }
    return home;
}
#endif

#ifndef _WIN32
// ~/.config/npad/settings.json, where npad kept settings before it honoured
// $XDG_CONFIG_HOME. NULL without a home directory.
static char *settings_legacy_file_path(void) {
    char *dir = settings_resolve_legacy_dir(settings_home_dir());
    if (!dir)
        return NULL;
    char *path = file_join_paths(dir, "settings.json");
    free(dir);
    return path;
}
#endif

static char *get_settings_directory(void) {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
        char *platima_dir = file_join_paths(path, "Platima");
        char *npad_dir = file_join_paths(platima_dir, "npad");
        free(platima_dir);
        return npad_dir;
    }
    return NULL;
#else
    return settings_resolve_config_dir(getenv("XDG_CONFIG_HOME"), settings_home_dir());
#endif
}

// --- Reader ----------------------------------------------------------------
//
// A small JSON reader for the shape npad writes - one object of "key": "value"
// pairs - that is also safe with the rest of JSON a script or a later npad may
// put there: escapes npad does not write (\uXXXX with surrogate pairs, \b, \f,
// \/), numbers, true/false/null, and nested objects or arrays (skipped by
// depth). Numbers, true/false and nested values are kept verbatim, so a save
// writes them back with their JSON types; a key npad itself sets to a new
// value becomes a string. It never recurses, so no nesting depth can overflow
// the stack.

static bool is_json_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_space(const char **p, const char *end) {
    while (*p < end && is_json_space(**p))
        (*p)++;
}

// The closing quote of the string whose opening quote is at s, or NULL when it
// is unterminated
static const char *string_end(const char *s, const char *end) {
    for (const char *q = s + 1; q < end; q++) {
        if (*q == '\\') {
            q++; // Whatever follows is escaped, a quote included
        } else if (*q == '"') {
            return q;
        }
    }
    return NULL;
}

static bool hex4(const char *s, const char *end, unsigned *out) {
    if (end - s < 4)
        return false;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned) (c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned) (c - 'A' + 10);
        else
            return false;
    }
    *out = v;
    return true;
}

static char *put_utf8(char *w, unsigned long cp) {
    if (cp < 0x80) {
        *w++ = (char) cp;
    } else if (cp < 0x800) {
        *w++ = (char) (0xC0 | (cp >> 6));
        *w++ = (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *w++ = (char) (0xE0 | (cp >> 12));
        *w++ = (char) (0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char) (0x80 | (cp & 0x3F));
    } else {
        *w++ = (char) (0xF0 | (cp >> 18));
        *w++ = (char) (0x80 | ((cp >> 12) & 0x3F));
        *w++ = (char) (0x80 | ((cp >> 6) & 0x3F));
        *w++ = (char) (0x80 | (cp & 0x3F));
    }
    return w;
}

// Decode the body of a JSON string (between its quotes) into a new string. A
// decoded escape is never longer than its source, so the source length bounds
// the buffer. Escapes must be undone for keys as well as values: an escape
// written without a matching unescape doubles on every save (the 1.2 GB
// settings file in LESSONS.md, and later the same growth in keys).
static char *json_decode(const char *s, const char *end) {
    char *out = malloc((size_t) (end - s) + 1);
    if (!out)
        return NULL;
    char *w = out;
    while (s < end) {
        char c = *s++;
        if (c != '\\' || s >= end) {
            *w++ = c; // Raw control characters an older npad wrote are kept as they are
            continue;
        }
        char esc = *s++;
        switch (esc) {
            case 'b':
                *w++ = '\b';
                break;
            case 'f':
                *w++ = '\f';
                break;
            case 'n':
                *w++ = '\n';
                break;
            case 'r':
                *w++ = '\r';
                break;
            case 't':
                *w++ = '\t';
                break;
            case 'u': {
                unsigned hi;
                if (!hex4(s, end, &hi)) {
                    *w++ = 'u'; // Malformed: keep the letter, as for any unknown escape
                    break;
                }
                s += 4;
                unsigned long cp = hi;
                if (hi >= 0xD800 && hi <= 0xDBFF) {
                    unsigned lo;
                    if (end - s >= 6 && s[0] == '\\' && s[1] == 'u' && hex4(s + 2, end, &lo) &&
                        lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + (((unsigned long) hi - 0xD800) << 10) + (lo - 0xDC00);
                        s += 6;
                    } else {
                        cp = 0xFFFD; // A lone high surrogate
                    }
                } else if ((hi >= 0xDC00 && hi <= 0xDFFF) || hi == 0) {
                    cp = 0xFFFD; // A lone low surrogate, or a NUL a C string cannot hold
                }
                w = put_utf8(w, cp);
                break;
            }
            default:
                *w++ = esc; // \" \\ \/, and anything unknown, stand for themselves
                break;
        }
    }
    *w = '\0';
    return out;
}

// Step over the object or array starting at *p, honouring strings. False when
// it is unterminated.
static bool skip_nested(const char **p, const char *end) {
    size_t depth = 0;
    while (*p < end) {
        char c = **p;
        if (c == '"') {
            const char *q = string_end(*p, end);
            if (!q)
                return false;
            *p = q + 1;
            continue;
        }
        if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (depth == 0)
                return false;
            if (--depth == 0) {
                (*p)++;
                return true;
            }
        }
        (*p)++;
    }
    return false;
}

// Is s[0..n) exactly a JSON number or true/false - text that may be written
// back verbatim without making the file invalid?
static bool is_json_scalar(const char *s, size_t n) {
    if ((n == 4 && memcmp(s, "true", 4) == 0) || (n == 5 && memcmp(s, "false", 5) == 0))
        return true;
    const char *p = s, *end = s + n;
    if (p < end && *p == '-')
        p++;
    if (p >= end || *p < '0' || *p > '9')
        return false;
    if (*p == '0')
        p++; // No leading zeros
    else
        while (p < end && *p >= '0' && *p <= '9')
            p++;
    if (p < end && *p == '.') {
        p++;
        if (p >= end || *p < '0' || *p > '9')
            return false;
        while (p < end && *p >= '0' && *p <= '9')
            p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-'))
            p++;
        if (p >= end || *p < '0' || *p > '9')
            return false;
        while (p < end && *p >= '0' && *p <= '9')
            p++;
    }
    return p == end;
}

// Parse `content` into `out`, appending in file order (a repeated key keeps its
// first position and its last value). False when it holds no settings object
// at all; otherwise true, with *complete false when parsing stopped early at
// something malformed or truncated - everything before that point is kept.
static bool parse_settings_file(const char *content, SettingStore *out, bool *complete) {
    *complete = false;
    if (!content)
        return false;

    const char *p = content;
    const char *end = content + strlen(content);
    if (end - p >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0)
        p += 3; // A UTF-8 BOM, which an editor may add on a hand edit
    skip_space(&p, end);
    if (p >= end || *p != '{')
        return false;
    p++;

    for (;;) {
        skip_space(&p, end);
        if (p >= end)
            return true; // Truncated
        if (*p == '}') {
            *complete = true; // Anything after the object is ignored
            return true;
        }
        if (*p == ',') {
            p++; // Between members; a stray or trailing one is forgiven
            continue;
        }
        if (*p != '"')
            return true;

        const char *key_end = string_end(p, end);
        if (!key_end)
            return true;
        char *key = json_decode(p + 1, key_end);
        if (!key)
            return true;
        p = key_end + 1;
        skip_space(&p, end);
        if (p >= end || *p != ':') {
            free(key);
            return true;
        }
        p++;
        skip_space(&p, end);
        if (p >= end) {
            free(key);
            return true;
        }

        char *value = NULL;
        bool raw = false;
        bool is_null = false;
        if (*p == '"') {
            const char *value_end = string_end(p, end);
            if (value_end) {
                value = json_decode(p + 1, value_end);
                p = value_end + 1;
            }
        } else if (*p == '{' || *p == '[') {
            const char *start = p;
            if (skip_nested(&p, end)) {
                value = dup_range(start, (size_t) (p - start));
                raw = true;
            }
        } else {
            // A number, true, false or null, kept as its text. A valid number
            // or true/false is also written back bare, so a save does not turn
            // a script's 5 into "5"; anything else (a typo such as tru) is
            // kept as a string, which writes back as valid JSON.
            const char *start = p;
            while (p < end && !is_json_space(*p) && *p != ',' && *p != '}' && *p != ']' &&
                   *p != '{' && *p != '[' && *p != '"' && *p != ':')
                p++;
            if (p > start && p < end) {
                size_t n = (size_t) (p - start);
                value = dup_range(start, n);
                is_null = n == 4 && memcmp(start, "null", 4) == 0;
                raw = is_json_scalar(start, n);
            }
        }
        if (!value) {
            free(key); // Unterminated, empty or out of memory
            return true;
        }

        // Drop implausible keys and values rather than carrying them forward:
        // an already-corrupt file must not keep npad from starting, and must
        // heal on the next save. null means "not set".
        if (!is_null && strlen(key) <= SETTINGS_MAX_KEY_LEN &&
            strlen(value) <= SETTINGS_MAX_VALUE_LEN) {
            if (out->count >= SETTINGS_MAX_KEYS && !store_find(out, key)) {
                free(key);
                free(value);
                return true; // Too many keys to be real settings; stop here
            }
            if (!store_put(out, key, value, raw)) {
                free(key);
                free(value);
                return true;
            }
        }
        free(key);
        free(value);
    }
}

// --- Writer ----------------------------------------------------------------

typedef struct {
    char *buf;
    size_t len, cap;
    bool failed;
} TextBuilder;

static void tb_append(TextBuilder *tb, const char *s, size_t n) {
    if (tb->failed || n == 0)
        return;
    if (tb->len + n + 1 > tb->cap) {
        size_t cap = tb->cap ? tb->cap : 4096;
        while (cap < tb->len + n + 1) {
            if (cap > ((size_t) -1) / 2) {
                tb->failed = true;
                return;
            }
            cap *= 2;
        }
        char *grown = realloc(tb->buf, cap);
        if (!grown) {
            tb->failed = true;
            return;
        }
        tb->buf = grown;
        tb->cap = cap;
    }
    memcpy(tb->buf + tb->len, s, n);
    tb->len += n;
    tb->buf[tb->len] = '\0';
}

static void tb_puts(TextBuilder *tb, const char *s) {
    tb_append(tb, s, strlen(s));
}

// A JSON string literal: quote and backslash escaped, and every control
// character too (a raw tab or newline is invalid JSON to jq or Python).
// Non-ASCII UTF-8 is written as it is.
static void tb_append_json_string(TextBuilder *tb, const char *s) {
    tb_append(tb, "\"", 1);
    const char *run = s;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char) *p;
        const char *esc = NULL;
        char ubuf[8];
        switch (c) {
            case '"':
                esc = "\\\"";
                break;
            case '\\':
                esc = "\\\\";
                break;
            case '\n':
                esc = "\\n";
                break;
            case '\r':
                esc = "\\r";
                break;
            case '\t':
                esc = "\\t";
                break;
            case '\b':
                esc = "\\b";
                break;
            case '\f':
                esc = "\\f";
                break;
            default:
                if (c < 0x20) {
                    snprintf(ubuf, sizeof(ubuf), "\\u%04x", c);
                    esc = ubuf;
                }
                break;
        }
        if (esc) {
            tb_append(tb, run, (size_t) (p - run));
            tb_puts(tb, esc);
            run = p + 1;
        }
    }
    tb_puts(tb, run);
    tb_append(tb, "\"", 1);
}

// The whole store as settings.json text, in its order. NULL only when out of
// memory: the builder grows to fit, so nothing is ever silently left out (the
// old fixed-size buffer's guard stopped early and wrote a well-formed file
// missing every later setting).
static char *serialize_settings(const SettingStore *store) {
    TextBuilder tb;
    memset(&tb, 0, sizeof(tb));
    tb_puts(&tb, "{\n");
    for (const SettingEntry *e = store->head; e; e = e->next) {
        tb_puts(&tb, "  ");
        tb_append_json_string(&tb, e->key);
        tb_puts(&tb, ": ");
        if (e->raw)
            tb_puts(&tb, e->value); // A nested value from the file, verbatim
        else
            tb_append_json_string(&tb, e->value);
        tb_puts(&tb, e->next ? ",\n" : "\n");
    }
    tb_puts(&tb, "}\n");
    if (tb.failed) {
        free(tb.buf);
        NPAD_ERROR_WARNING(NPAD_ERROR_MEMORY, 0, "Settings",
                           "Out of memory writing settings; the file was left as it was");
        return NULL;
    }
    return tb.buf;
}
