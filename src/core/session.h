/*
 * npad - Session Recovery
 * Crash-recovery snapshots of unsaved documents
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef SESSION_H
#define SESSION_H

#include "file_ops.h" // TextEncoding / LineEnding / FileStamp
#include <stdbool.h>
#include <stddef.h>

// Each running instance owns a recovery "slot" identified by a short id.
// A slot is two files in the recovery directory: <slot>.txt (UTF-8 content)
// and <slot>.meta (encoding, line ending and flags, then the original path).
// Slots left behind at startup mean the owning instance did not exit cleanly.

// Room for any slot id, including its terminating NUL
#define SESSION_SLOT_ID_MAX 64

// Slot ids are "<time>-<pid>" or "<time>-<nonce>-<pid>", each part 1-16 hex
// digits: the time the owning instance started (Unix seconds), a random nonce
// that keeps two instances apart when a pid namespace (a Flatpak sandbox, a
// container) gives them the same pid in the same second, and its process id.
// npad before 0.36 wrote the two-part form; both are accepted. The pid stays
// last because older npad reads it from after the last '-' to decide whether
// a slot's owner is still running.
//
// Every id that arrives from outside - the --recover option, a directory
// listing - is checked against that shape before it becomes part of a path,
// so no id can name a file outside the recovery directory.
bool session_is_valid_slot_id(const char *id);
// Split a valid id into its start time and pid. False for an invalid id.
bool session_parse_slot_id(const char *id, unsigned long long *start_time, long *pid);
// A fresh id for this process (size should be SESSION_SLOT_ID_MAX)
void session_make_slot_id(char *out, size_t size);

// What a slot records besides its text
typedef struct {
    TextEncoding encoding;
    LineEnding line_ending;
    // The document is the part of a file before a NUL (TextFileInfo.truncated)
    // and must never be saved over that file
    bool truncated;
    // Reading only: the meta recorded `truncated` at all. npad before 0.36
    // did not, so for those slots the caller has to look at the file again.
    // A meta that has it also has decode_lossy and utf16_no_bom.
    bool has_truncated;
    // The original held bytes npad could not decode (TextFileInfo.decode_lossy),
    // so a save over it asks first and auto-save skips it
    bool decode_lossy;
    // UTF-16 written back without a byte order mark (TextFileInfo.utf16_no_bom)
    bool utf16_no_bom;
    // The text is not exactly what its file holds, because the edit control
    // could not show it exactly (see ui_text_altered_by_load): restored, it
    // must still never be saved over that file without a word. Absent (npad
    // before 0.36) reads as false.
    bool altered;
    // The original file's stamp when npad last read or wrote it, the baseline
    // for noticing an external change. size -1 when there is none.
    FileStamp stamp;
} SessionMeta;

// Write (overwrite) the snapshot for a slot. path may be NULL for an
// untitled document; meta NULL records UTF-8, the default line ending and no
// flags. Returns false on I/O failure.
bool session_write_ex(const char *dir, const char *slot_id, const char *utf8_content,
                      const char *path, const SessionMeta *meta);
// Shorthand for session_write_ex with just an encoding and line ending
bool session_write(const char *dir, const char *slot_id, const char *utf8_content, const char *path,
                   TextEncoding encoding, LineEnding line_ending);

// --- Claims ----------------------------------------------------------------
// Restoring a slot is a sequence, not one step, and other npad processes may
// be starting at the same moment. A claim marks a slot as being handled by one
// instance: <slot>.meta is renamed to <slot>.meta.claim-<claimer>, which only
// one caller can win, and a claimed slot is no longer listed. The claimer is
// that instance's own slot id, so a claim left by an instance that died is
// recognisably stale (session_recover_orphans hands it back).

// Claim a listed slot (from_claimer NULL), or take over a claim that another
// instance handed on (from_claimer is that instance's id). False when the slot
// is gone or someone else got there first.
bool session_claim(const char *dir, const char *slot_id, const char *from_claimer,
                   const char *claimer);
// Give a claimed slot back unchanged, so it is offered again later
bool session_release(const char *dir, const char *slot_id, const char *claimer);
// Delete a claimed slot outright (the user explicitly declined it)
void session_discard_claimed(const char *dir, const char *slot_id, const char *claimer);

typedef enum {
    SESSION_READ_OK,     // *content and *path are set (path NULL when untitled)
    SESSION_READ_EMPTY,  // The slot has no content file: nothing to restore
    SESSION_READ_FAILED, // The content exists but could not be read
    SESSION_READ_GONE,   // The claim is no longer there (handed back meanwhile)
} SessionReadResult;

// Read a slot this caller has claimed. Nothing is deleted, whatever happens:
// the caller decides between session_adopt, session_release and
// session_discard_claimed.
SessionReadResult session_read_claimed(const char *dir, const char *slot_id, const char *claimer,
                                       char **content, char **path, SessionMeta *meta);

// Move a claimed slot under new_slot_id (the claimer's own slot), rewriting
// its meta from path/meta. The text is renamed rather than copied, so the
// restored document is on disk from the first moment and no extra space is
// needed. On failure nothing is lost: the claim is left as it was.
bool session_adopt(const char *dir, const char *slot_id, const char *claimer,
                   const char *new_slot_id, const char *path, const SessionMeta *meta);

// Claim, read and remove a slot in one go. Returns malloc'd UTF-8 content,
// or NULL if the slot is missing, already claimed, or unreadable - in the
// last case the slot is given back rather than deleted. On success *out_path
// receives a malloc'd path copy (NULL for untitled) and *encoding /
// *line_ending are set.
char *session_take(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                   LineEnding *line_ending);
// session_take that also reports the altered flag the slot recorded (false
// for a slot written without it, including by npad before 0.36)
char *session_take_ex(const char *dir, const char *slot_id, char **out_path, TextEncoding *encoding,
                      LineEnding *line_ending, bool *altered);

// List the slot ids currently present in dir (from <slot>.meta files; claimed
// slots and names that are not valid ids are left out). Returns a malloc'd
// array of malloc'd strings; caller frees via session_free_slots. *count is
// set to the number of slots (0 => NULL).
char **session_list_slots(const char *dir, int *count);
void session_free_slots(char **slots, int count);

// Remove a slot's files (including any handoff marker)
void session_clear_slot(const char *dir, const char *slot_id);

// Whether the instance that owns (or claimed) a slot id is still running
typedef bool (*SessionLiveFn)(const char *slot_id, void *ctx);

// Startup housekeeping, run before listing: make leftovers that an interrupted
// restore or snapshot left half-done listable again, so they are offered
// instead of sitting unseen. Only slots whose owner is_live() denies are
// touched, and never own_slot_id:
//   - a claim whose claimer is gone goes back to <slot>.meta
//   - claims made by npad before 0.36 (<slot>.meta.taking, <slot>.txt.taking)
//     go back likewise
//   - a <slot>.txt with no .meta (a first snapshot cut off between its two
//     files) gets a default meta, so it is offered as an untitled document
//   - a .meta with no text, and a lone handoff marker, hold nothing and are
//     removed
// Returns the number of slots made listable again.
int session_recover_orphans(const char *dir, const char *own_slot_id, SessionLiveFn is_live,
                            void *ctx);

// Handoff markers. A marked slot was written because something else was
// closing npad - an in-app update, or Windows restarting - rather than because
// npad crashed, so the next launch restores it silently instead of asking.
bool session_mark_handoff(const char *dir, const char *slot_id);
bool session_is_handoff(const char *dir, const char *slot_id);
void session_clear_handoff(const char *dir, const char *slot_id);

#endif // SESSION_H
