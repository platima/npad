/*
 * npad - Session Recovery Implementation
 * Crash-recovery snapshots of unsaved documents
 *
 * The snapshot is two files in the recovery directory:
 *   recovery.txt   - the document content, UTF-8
 *   recovery.meta  - "encoding\nline_ending\npath" (path may be empty)
 * A snapshot existing at startup means the previous run did not exit
 * cleanly, so its unsaved work can be offered for recovery.
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
#include <sys/stat.h>
#endif

static char *join(const char *dir, const char *name) {
    return file_join_paths(dir, name);
}

static void ensure_dir(const char *dir) {
    if (!dir)
        return;
#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
}

bool session_write(const char *dir, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending) {
    if (!dir || !utf8_content)
        return false;

    ensure_dir(dir);

    char *content_path = join(dir, "recovery.txt");
    char *meta_path = join(dir, "recovery.meta");
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
        ok = file_write_text_atomic(content_path, utf8_content) &&
             file_write_text_atomic(meta_path, meta);
        free(meta);
    }

    free(content_path);
    free(meta_path);
    return ok;
}

char *session_read(const char *dir, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending) {
    if (out_path)
        *out_path = NULL;
    if (!dir)
        return NULL;

    char *content_path = join(dir, "recovery.txt");
    char *meta_path = join(dir, "recovery.meta");
    if (!content_path || !meta_path) {
        free(content_path);
        free(meta_path);
        return NULL;
    }

    char *content = file_read_text(content_path);
    char *meta = file_read_text(meta_path);

    free(content_path);
    free(meta_path);

    if (!content || !meta) {
        free(content);
        free(meta);
        return NULL;
    }

    // Parse "encoding\nline_ending\npath"
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
    free(meta);

    if (encoding) {
        *encoding = (enc >= 0 && enc <= (int) NPAD_ENC_ANSI) ? (TextEncoding) enc : NPAD_ENC_UTF8;
    }
    if (line_ending) {
        *line_ending = (eol >= 0 && eol <= (int) NPAD_EOL_CR) ? (LineEnding) eol : NPAD_EOL_CRLF;
    }

    return content;
}

bool session_exists(const char *dir) {
    if (!dir)
        return false;

    char *content_path = join(dir, "recovery.txt");
    if (!content_path)
        return false;

    bool exists = file_exists(content_path);
    free(content_path);
    return exists;
}

void session_clear(const char *dir) {
    if (!dir)
        return;

    char *content_path = join(dir, "recovery.txt");
    char *meta_path = join(dir, "recovery.meta");
    if (content_path) {
        file_delete(content_path);
        free(content_path);
    }
    if (meta_path) {
        file_delete(meta_path);
        free(meta_path);
    }
}
