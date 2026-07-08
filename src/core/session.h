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

// Each running instance owns a recovery "slot" identified by a short id.
// A slot is two files in the recovery directory: <slot>.txt (UTF-8 content)
// and <slot>.meta (encoding, line ending, original path). Slots left behind
// at startup mean the owning instance did not exit cleanly.

// Write (overwrite) the snapshot for a slot. path may be NULL for an
// untitled document. Returns false on I/O failure.
bool session_write(const char *dir, const char *slot_id, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending);

// Atomically claim and read a slot: the slot files are renamed aside first,
// so only one caller can take a given slot (avoids double recovery), then
// read and removed. Returns malloc'd UTF-8 content, or NULL if the slot is
// missing or already claimed. On success *out_path receives a malloc'd path
// copy (NULL for untitled) and *encoding / *line_ending are set.
char *session_take(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending);

// List the slot ids currently present in dir (from <slot>.meta files).
// Returns a malloc'd array of malloc'd strings; caller frees via
// session_free_slots. *count is set to the number of slots (0 => NULL).
char **session_list_slots(const char *dir, int *count);
void session_free_slots(char **slots, int count);

// Remove a slot's files
void session_clear_slot(const char *dir, const char *slot_id);

#endif // SESSION_H
