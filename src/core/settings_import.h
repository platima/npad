/*
 * npad - Settings import
 * The platform-independent half of Preferences > Backup > Import Settings.
 * Every UI layer calls settings_import_file() and shows the message for its
 * result, so the rules are identical on every platform:
 *
 *   - Only an npad settings file is accepted: a JSON object (UTF-8, with or
 *     without a BOM, or UTF-16) whose keys are mostly npad's own. Picking the
 *     wrong file (another program's settings.json, a .txt, a binary) used to
 *     replace every preference, recent file and history entry with nothing.
 *   - The current settings are kept beside the live file as settings.json.bak
 *     before anything is replaced.
 *   - The new file is written atomically (temp file + verify + rename), so a
 *     full disk or a crash leaves the previous settings.json intact.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef SETTINGS_IMPORT_H
#define SETTINGS_IMPORT_H

#include <stdbool.h>

// Largest file Import accepts. A real settings file is a few kilobytes; this
// stays well under the loader's own salvage limit even after a UTF-16 file
// grows by half on conversion to UTF-8.
#define SETTINGS_IMPORT_MAX_BYTES (4 * 1024 * 1024)

// Suffix of the copy of the previous settings kept by an import
#define SETTINGS_IMPORT_BACKUP_SUFFIX ".bak"

// SETTINGS_IMPORT_OK is 0, so never test the result with `!`: compare it with
// SETTINGS_IMPORT_OK (see the bool-to-enum lesson in LESSONS.md).
typedef enum {
    SETTINGS_IMPORT_OK = 0,
    SETTINGS_IMPORT_UNREADABLE,   // The chosen file could not be read
    SETTINGS_IMPORT_NOT_SETTINGS, // Readable, but not an npad settings file
    SETTINGS_IMPORT_SAVE_FAILED,  // A save, the backup or the write failed; nothing changed
} SettingsImportResult;

// Is this key one npad itself stores? Exact names plus npad's own families
// (recent_file_N, window_*, find_*, ...).
bool settings_import_is_npad_key(const char *key);

// Does this text look like an npad settings file? A JSON object, closed,
// with at least one key, at least half of whose top-level keys are npad's.
// Pure string logic, so it can be tested without touching the disk.
bool settings_import_looks_like_settings(const char *utf8);

// Replace the current settings with the file at path. On SETTINGS_IMPORT_OK
// the in-memory settings are flushed to disk, copied to settings.json.bak,
// replaced on disk atomically by the import, and reloaded from it: the caller
// then re-applies what can change live and tells the other instances. On any
// other result nothing was imported: the in-memory settings are untouched, and
// the settings file holds them (it may have been flushed, as an exit would).
SettingsImportResult settings_import_file(const char *path);

// The message to show the user for a result (UTF-8, static)
const char *settings_import_message(SettingsImportResult result);

#endif // SETTINGS_IMPORT_H
