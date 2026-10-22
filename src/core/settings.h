/*
 * npad - Settings Management
 * Cross-platform settings storage and retrieval
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

// Settings initialization and cleanup
bool settings_init(void);
void settings_cleanup(void);

// Basic setting operations. A set records the key as this instance's unsaved
// change (setting the value it already holds is not a change); false for a key
// over 256 bytes or a value over 64 KB, which the reader would drop.
bool settings_set_string(const char *key, const char *value);
char *settings_get_string(const char *key, const char *default_value);

// Store a list as keys "<prefix>_0" .. "<prefix>_<capacity-1>": values[0..count)
// in order, every later position removed. The list is one unit: each position
// counts as this instance's change even where it holds that value already, and
// each removal is recorded even for a key only another window held, so a save
// replaces the whole list rather than mixing it entry by entry with another
// window's (duplicates, survivors of a clear, a gap that hides the rest).
// Recent Files and find/replace history. False on a bad argument or a value
// too long to store, with nothing changed, or when out of memory.
bool settings_set_list(const char *prefix, char *const *values, int count, int capacity);

// The typed getters return default_value for a missing key AND for text that
// is not a whole valid value of the type ("ten", "", "12abc", out of range).
// Bools read "true"/"false" (any case) and "1"/"0". Doubles are stored with a
// '.' separator whatever the locale.
bool settings_set_int(const char *key, int value);
int settings_get_int(const char *key, int default_value);

bool settings_set_bool(const char *key, bool value);
bool settings_get_bool(const char *key, bool default_value);

bool settings_set_double(const char *key, double value);
double settings_get_double(const char *key, double default_value);

// Settings management
bool settings_has_key(const char *key);
bool settings_remove_key(const char *key); // Also an unsaved change: the save removes it
// Forget every setting held in memory, and every unsaved change, without
// touching the file (tests; a reload is settings_load, which merges)
bool settings_clear_all(void);
int settings_count(void); // Number of stored entries (Debug page diagnostics)

// Has this instance set or removed anything since it last saved?
bool settings_has_unsaved_changes(void);
// Forget this instance's unsaved changes, so the next settings_load adopts the
// file as it is (Import, and dropping values a failed save left in memory)
void settings_discard_unsaved_changes(void);
// The same for one key: it stops being this instance's change (set or
// removed), so the next settings_load takes the file's value for it, or drops
// it when the file does not have it. Every other unsaved change is kept - for
// undoing just what a failed Preferences Apply wrote.
void settings_discard_unsaved_change(const char *key);

// Remove every setting whose key does NOT begin with one of keep_prefixes;
// returns the count removed. This is the "reset to defaults except the
// preserved categories" primitive: new preference keys are reset by DEFAULT,
// so callers only maintain the small, stable list of things to KEEP (recent
// files, window geometry, find/replace state) rather than an allowlist of
// everything to remove (which silently drifts as new preferences are added).
int settings_reset_except_prefixes(const char *const *keep_prefixes, int prefix_count);

// File operations. Several npad windows share the file, so:
// - settings_save re-reads it and writes back what it holds with only this
//   instance's unsaved changes applied, then adopts that result (so it also
//   picks up what other windows saved). It writes nothing when there is
//   nothing to add, and refuses (false, changes kept) when the file exists but
//   cannot be read. A file it cannot read in full is kept first, as
//   settings.json.corrupt (or .corrupt.1 and on: an earlier copy is never
//   overwritten) - copied, or renamed when it is over the 8 MB load cap, since
//   copying would read it whole.
// - settings_load reads it into a fresh store and swaps that in only when the
//   read worked, with this instance's unsaved changes put back on top. False
//   (store untouched) when the file cannot be read or is not settings at all.
bool settings_save(void);
bool settings_load(void);
const char *settings_get_file_path(void);

// Point settings at a specific file instead of the derived location.
// settings_init() sets the normal path; this exists so the save/load round
// trip can be exercised against a temporary file without touching the user's
// real settings - the escaping bug that grew a value exponentially on every
// round trip was invisible precisely because that path had no test.
bool settings_set_file_path(const char *path);

// The directory settings are stored in (malloc'd; caller frees). NULL on error.
char *settings_get_config_dir(void);

// Where settings live on Linux/Unix, as pure string logic so it can be tested
// without touching the environment: $XDG_CONFIG_HOME/npad when that is set and
// absolute, otherwise <home>/.config/npad (also what settings_resolve_legacy_dir
// returns). malloc'd; caller frees. NULL when no directory can be formed.
// Windows ignores these: its settings stay in %APPDATA%\Platima\npad.
char *settings_resolve_config_dir(const char *xdg_config_home, const char *home);
char *settings_resolve_legacy_dir(const char *home);

// Window state helpers
bool settings_save_window_state(int x, int y, int width, int height, bool maximized);
bool settings_load_window_state(int *x, int *y, int *width, int *height, bool *maximized);

// Recent files management. settings_get_recent_files is for showing the list:
// it leaves out files that do not exist right now. settings_add_recent_file
// keeps them, so an entry on a sleeping NAS or a removed USB stick comes back
// when the file is reachable again.
bool settings_add_recent_file(const char *filepath);
char **settings_get_recent_files(int *count);
void settings_free_recent_files(char **files, int count);
bool settings_clear_recent_files(void);

#endif // SETTINGS_H