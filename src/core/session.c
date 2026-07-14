/*
 * npad - Session Recovery Implementation
 * Crash-recovery snapshots of unsaved documents
 *
 * Each instance owns a slot: <slot>.txt (UTF-8 content) and <slot>.meta
 * ("encoding\nline_ending\npath", path may be empty). Slots present at
 * startup are leftovers from instances that did not exit cleanly.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "session.h"
#include "file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define SESSION_MAX_SLOTS 64

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
    mkdir(dir, 0755);
#endif
}

// Build "<dir>/<slot><suffix>" (suffix e.g. ".txt")
static char *slot_path(const char *dir, const char *slot_id, const char *suffix) {
    size_t need = strlen(slot_id) + strlen(suffix) + 1;
    char *name = malloc(need);
    if (!name)
        return NULL;
    snprintf(name, need, "%s%s", slot_id, suffix);
    char *full = file_join_paths(dir, name);
    free(name);
    return full;
}

// Rename honoring UTF-8 paths on Windows. Returns true on success.
static bool rename_utf8(const char *from, const char *to) {
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

bool session_write(const char *dir, const char *slot_id, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending) {
    if (!dir || !slot_id || !utf8_content)
        return false;

    ensure_dir(dir);

    char *content_path = slot_path(dir, slot_id, ".txt");
    char *meta_path = slot_path(dir, slot_id, ".meta");
    if (!content_path || !meta_path) {
        free(content_path);
        free(meta_path);
        return false;
    }

    // Metadata: two integers then the original path (possibly empty)
    size_t path_len = path ? strlen(path) : 0;
    char *meta = malloc(path_len + 64);
    bool ok = false;
    if (meta) {
        snprintf(meta, path_len + 64, "%d\n%d\n%s", (int) encoding, (int) line_ending,
                 path ? path : "");
        // Write content first, then meta: recovery keys off the .meta file,
        // so the content is always present by the time meta appears.
        ok = file_write_text_atomic(content_path, utf8_content) &&
             file_write_text_atomic(meta_path, meta);
        free(meta);
    }

    free(content_path);
    free(meta_path);
    return ok;
}

// Parse "<encoding>\n<line_ending>\n<path>" metadata contents
static void parse_meta(const char *meta, char **out_path, TextEncoding *encoding,
                       LineEnding *line_ending) {
    int enc = 0, eol = 0;
    const char *first_nl = strchr(meta, '\n');
    const char *second_nl = first_nl ? strchr(first_nl + 1, '\n') : NULL;
    if (first_nl && second_nl) {
        enc = atoi(meta);
        eol = atoi(first_nl + 1);
        const char *path_start = second_nl + 1;
        if (*path_start && out_path) {
            *out_path = malloc(strlen(path_start) + 1);
            if (*out_path) {
                strcpy(*out_path, path_start);
            }
        }
    }
    if (encoding) {
        *encoding = (enc >= 0 && enc <= (int) NPAD_ENC_ANSI) ? (TextEncoding) enc : NPAD_ENC_UTF8;
    }
    if (line_ending) {
        *line_ending = (eol >= 0 && eol <= (int) NPAD_EOL_CR) ? (LineEnding) eol : NPAD_EOL_CRLF;
    }
}

char *session_take(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending) {
    if (out_path)
        *out_path = NULL;
    if (!dir || !slot_id)
        return NULL;

    char *content_path = slot_path(dir, slot_id, ".txt");
    char *meta_path = slot_path(dir, slot_id, ".meta");
    char *content_claim = slot_path(dir, slot_id, ".txt.taking");
    char *meta_claim = slot_path(dir, slot_id, ".meta.taking");
    char *content = NULL;

    if (!content_path || !meta_path || !content_claim || !meta_claim) {
        goto done;
    }

    // Claim the meta file first; whoever wins the rename owns the slot
    if (!rename_utf8(meta_path, meta_claim)) {
        goto done; // Missing or already claimed by another instance
    }
    // Best-effort claim of the content file too
    rename_utf8(content_path, content_claim);

    char *meta = file_read_text(meta_claim);
    content = file_read_text(content_claim);

    if (meta) {
        parse_meta(meta, out_path, encoding, line_ending);
        free(meta);
    }

    file_delete(content_claim);
    file_delete(meta_claim);

done:
    free(content_path);
    free(meta_path);
    free(content_claim);
    free(meta_claim);
    return content;
}

// Strip a trailing ".meta" and append the base name to the slot list
static void add_slot_from_meta(char ***slots, int *count, int *cap, const char *meta_name) {
    size_t len = strlen(meta_name);
    const char *ext = ".meta";
    size_t ext_len = strlen(ext);
    if (len <= ext_len || strcmp(meta_name + len - ext_len, ext) != 0) {
        return; // Not a .meta file
    }
    if (*count >= SESSION_MAX_SLOTS) {
        return;
    }

    if (*count >= *cap) {
        int new_cap = *cap ? *cap * 2 : 8;
        char **grown = realloc(*slots, (size_t) new_cap * sizeof(char *));
        if (!grown) {
            return;
        }
        *slots = grown;
        *cap = new_cap;
    }

    char *id = malloc(len - ext_len + 1);
    if (!id) {
        return;
    }
    memcpy(id, meta_name, len - ext_len);
    id[len - ext_len] = '\0';
    (*slots)[(*count)++] = id;
}

char **session_list_slots(const char *dir, int *count) {
    if (count)
        *count = 0;
    if (!dir || !count)
        return NULL;

    char **slots = NULL;
    int n = 0, cap = 0;

#ifdef _WIN32
    char *pattern = file_join_paths(dir, "*.meta");
    if (!pattern)
        return NULL;
    wchar_t wpattern[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wpattern, 1024) > 0) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(wpattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                int u8len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, NULL, 0, NULL, NULL);
                if (u8len > 0) {
                    char *name = malloc((size_t) u8len);
                    if (name) {
                        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, u8len, NULL, NULL);
                        add_slot_from_meta(&slots, &n, &cap, name);
                        free(name);
                    }
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
        while ((entry = readdir(d)) != NULL) {
            add_slot_from_meta(&slots, &n, &cap, entry->d_name);
        }
        closedir(d);
    }
#endif

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
    if (content_path) {
        file_delete(content_path);
        free(content_path);
    }
    if (meta_path) {
        file_delete(meta_path);
        free(meta_path);
    }
}
