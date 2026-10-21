/*
 * npad - Session Recovery
 * Crash-recovery snapshots of unsaved documents
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef SESSION_H
#define SESSION_H

#include "file_ops.h" // TextEncoding / LineEnding
#include <stdbool.h>

// Each running instance owns a recovery "slot" identified by a short id.
// A slot is two files in the recovery directory: <slot>.txt (UTF-8 content)
// and <slot>.meta (encoding, line ending and flags, original path). Slots left
// behind at startup mean the owning instance did not exit cleanly.

// Write (overwrite) the snapshot for a slot. path may be NULL for an
// untitled document. Returns false on I/O failure.
//
// altered records that the document's text is not exactly what its file
// holds, because the edit control could not show it exactly (see
// ui_text_altered_by_load): restored, it must still never be saved over that
// file without a word.
bool session_write_ex(const char *dir, const char *slot_id, const char *utf8_content,
                      const char *path, TextEncoding encoding, LineEnding line_ending,
                      bool altered);
// session_write_ex with altered false
bool session_write(const char *dir, const char *slot_id, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending);

// Atomically claim and read a slot: the slot files are renamed aside first,
// so only one caller can take a given slot (avoids double recovery), then
// read and removed. Returns malloc'd UTF-8 content, or NULL if the slot is
// missing or already claimed. On success *out_path receives a malloc'd path
// copy (NULL for untitled) and *encoding / *line_ending are set.
char *session_take(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending);
// session_take that also reports the altered flag session_write_ex recorded
// (false for a slot written without it, including by npad before 0.36)
char *session_take_ex(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                      LineEnding *line_ending, bool *altered);

// List the slot ids currently present in dir (from <slot>.meta files).
// Returns a malloc'd array of malloc'd strings; caller frees via
// session_free_slots. *count is set to the number of slots (0 => NULL).
char **session_list_slots(const char *dir, int *count);
void session_free_slots(char **slots, int count);

// Remove a slot's files (including any handoff marker)
void session_clear_slot(const char *dir, const char *slot_id);

// Handoff markers. A marked slot was written because something else was
// closing npad - an in-app update, or Windows restarting - rather than because
// npad crashed, so the next launch restores it silently instead of asking.
bool session_mark_handoff(const char *dir, const char *slot_id);
bool session_is_handoff(const char *dir, const char *slot_id);
void session_clear_handoff(const char *dir, const char *slot_id);

#endif // SESSION_H
