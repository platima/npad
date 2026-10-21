/*
 * npad - Settings import
 *
 * Author: Platima
 * https://npad.dev
 */

#include "settings_import.h"
#include "file_ops.h"
#include "settings.h"

#include <stdlib.h>
#include <string.h>

// Keys npad stores under their own exact names. Families with a shared prefix
// are in NPAD_KEY_PREFIXES below, so most new keys need no entry here.
static const char *const NPAD_KEYS[] = {
    "auto_save_enabled",
    "auto_save_interval",
    "auto_update_defaults",
    "ctrl_n_new_window",
    "default_encoding",
    "default_font_mono",
    "default_line_ending",
    "default_zoom",
    "font_italic",
    "font_size",
    "font_weight",
    "icon_style",
    "large_file_warning_mb",
    "monospace_enabled", // Pre-0.7 font type
    "monospace_font",
    "opendyslexic_enabled",
    "opendyslexic_font",
    "proportional_font",
    "recent_files_max",
    "session_interval",
    "session_resume_enabled",
    "status_bar_visible",
    "status_show_counts",
    "sync_view_state",
    "tab_inserts_spaces",
    "tab_width",
    "theme",
    "watch_file_changes",
    "word_wrap",
};

// npad's own key families: recent files, window geometry, Find / Replace
// options and history, print setup, updates, the list tools and the delimiter
// dialog's history
static const char *const NPAD_KEY_PREFIXES[] = {
    "recent_file_", "window_",   "find_", "replace_", "print_",
    "update_",      "markdown_", "list_", "delim_",
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

bool settings_import_is_npad_key(const char *key) {
    if (!key || !key[0])
        return false;
    for (size_t i = 0; i < COUNT_OF(NPAD_KEYS); i++) {
        if (strcmp(key, NPAD_KEYS[i]) == 0)
            return true;
    }
    for (size_t i = 0; i < COUNT_OF(NPAD_KEY_PREFIXES); i++) {
        size_t n = strlen(NPAD_KEY_PREFIXES[i]);
        if (strncmp(key, NPAD_KEY_PREFIXES[i], n) == 0 && key[n] != '\0')
            return true;
    }
    return false;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

// p points at an opening quote. Returns the position after the closing quote,
// or NULL when the string never closes. When out is given it receives the raw
// characters, or an empty string when they do not fit (an over-long key is not
// one of npad's, and must not match a prefix through truncation).
static const char *scan_string(const char *p, char *out, size_t cap) {
    size_t n = 0;
    bool fits = true;
    p++;
    while (*p && *p != '"') {
        size_t step = (*p == '\\') ? 2 : 1;
        if (step == 2 && !p[1])
            return NULL;
        if (out) {
            if (n + step < cap) {
                memcpy(out + n, p, step);
                n += step;
            } else {
                fits = false;
            }
        }
        p += step;
    }
    if (*p != '"')
        return NULL;
    if (out)
        out[fits ? n : 0] = '\0';
    return p + 1;
}

// p points at the first character of a value. Returns the position after it,
// or NULL when it is malformed. Nested objects and arrays are skipped whole
// (npad writes none, but another program's file may).
static const char *skip_value(const char *p) {
    if (*p == '"')
        return scan_string(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        long depth = 0;
        while (*p) {
            if (*p == '"') {
                p = scan_string(p, NULL, 0);
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
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' &&
           *p != '\n')
        p++;
    return p == start ? NULL : p;
}

bool settings_import_looks_like_settings(const char *utf8) {
    if (!utf8)
        return false;
    const char *p = utf8;
    if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB &&
        (unsigned char) p[2] == 0xBF)
        p += 3; // A BOM the reader did not strip
    p = skip_ws(p);
    if (*p != '{')
        return false;
    p = skip_ws(p + 1);

    long total = 0;
    long ours = 0;
    while (*p != '}') {
        if (*p != '"')
            return false;
        char key[128];
        p = scan_string(p, key, sizeof(key));
        if (!p)
            return false;
        p = skip_ws(p);
        if (*p != ':')
            return false;
        p = skip_value(skip_ws(p + 1));
        if (!p)
            return false;
        total++;
        if (settings_import_is_npad_key(key))
            ours++;
        p = skip_ws(p);
        if (*p == ',')
            p = skip_ws(p + 1); // A trailing comma before } is tolerated
        else if (*p != '}')
            return false; // Missing separator, or the file stops mid-object
    }
    // Mostly npad's keys: another program's settings file that happens to
    // share a generic name or two ("theme", "font_size") is still refused
    return ours > 0 && ours * 2 >= total;
}

// Allocate path + suffix
static char *path_with_suffix(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    char *out = malloc(a + b + 1);
    if (out) {
        memcpy(out, path, a);
        memcpy(out + a, suffix, b + 1);
    }
    return out;
}

// Copy the settings file to its backup, atomically. Nothing to back up is fine.
static bool write_backup(const char *settings_path) {
    if (!file_exists(settings_path))
        return true;
    char *backup = path_with_suffix(settings_path, SETTINGS_IMPORT_BACKUP_SUFFIX);
    if (!backup)
        return false;
    void *data = NULL;
    size_t size = 0;
    bool ok = file_read_binary(settings_path, &data, &size) &&
              file_write_binary_atomic(backup, data, size);
    free(data);
    free(backup);
    return ok;
}

SettingsImportResult settings_import_file(const char *path) {
    const char *live = settings_get_file_path();
    if (!path || !live)
        return SETTINGS_IMPORT_SAVE_FAILED;
    if (!file_exists(path))
        return SETTINGS_IMPORT_UNREADABLE;
    if (file_get_size(path) > SETTINGS_IMPORT_MAX_BYTES)
        return SETTINGS_IMPORT_NOT_SETTINGS;

    // Read (and convert, for a file another editor re-saved as UTF-16) before
    // anything is written: importing settings.json onto itself must still see
    // the file as it was chosen
    TextFileInfo info;
    char *text = file_read_text_ex(path, &info);
    if (!text)
        return SETTINGS_IMPORT_UNREADABLE;
    // A NUL before the end (binary data) or bytes that are not UTF-8 are not
    // something npad wrote, and could not be carried faithfully anyway
    if (info.truncated || info.encoding == NPAD_ENC_ANSI ||
        strlen(text) > SETTINGS_IMPORT_MAX_BYTES || !settings_import_looks_like_settings(text)) {
        free(text);
        return SETTINGS_IMPORT_NOT_SETTINGS;
    }

    // The backup must hold what the user has now, including anything not yet
    // written (an exit would write it too). Every step up to the final write
    // leaves the live file as it was if it fails.
    if (!settings_save() || !write_backup(live)) {
        free(text);
        return SETTINGS_IMPORT_SAVE_FAILED;
    }
    bool written = file_write_text_atomic(live, text);
    free(text);
    if (!written)
        return SETTINGS_IMPORT_SAVE_FAILED;

    settings_clear_all();
    settings_load();
    return SETTINGS_IMPORT_OK;
}

const char *settings_import_message(SettingsImportResult result) {
    switch (result) {
        case SETTINGS_IMPORT_OK:
            return "Settings imported. A few options (window size, default encoding) take effect "
                   "on restart.\n\nYour previous settings were kept as settings.json.bak.";
        case SETTINGS_IMPORT_UNREADABLE:
            return "The file could not be read, so nothing was imported.";
        case SETTINGS_IMPORT_NOT_SETTINGS:
            return "That file is not an npad settings file, so nothing was imported.\n\nChoose a "
                   "file made by Export Settings.";
        case SETTINGS_IMPORT_SAVE_FAILED:
        default:
            return "npad could not save the settings, so nothing was imported. Check that the "
                   "settings folder is writable, then try again.";
    }
}
