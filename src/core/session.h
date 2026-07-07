/*
 * npad - Session Recovery
 * Crash-recovery snapshots of unsaved documents
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef SESSION_H
#define SESSION_H

#include "file_ops.h" // TextEncoding / LineEnding
#include <stdbool.h>

// Write a recovery snapshot into `dir` (created if needed): the document
// content plus enough metadata to restore its identity and save format.
// path may be NULL for an untitled document. Returns false on I/O failure.
bool session_write(const char *dir, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending);

// Read the recovery snapshot from `dir`. Returns malloc'd UTF-8 content (or
// NULL if none / on error). On success *out_path receives a malloc'd path
// copy (NULL for an untitled document) and *encoding / *line_ending are set.
char *session_read(const char *dir, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending);

// Whether a recovery snapshot is present in `dir`
bool session_exists(const char *dir);

// Remove any recovery snapshot from `dir`
void session_clear(const char *dir);

#endif // SESSION_H
