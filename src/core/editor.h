/*
 * npad - Editor Core
 * Core editor functionality and state management
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef EDITOR_H
#define EDITOR_H

#include "../ui_interface.h"
#include "file_ops.h"
#include <stdbool.h>

// Editor state. All editor functions run on the single UI thread; the
// state is not shared across threads.
typedef struct {
    char *current_file; // UTF-8 path, NULL for a new/untitled document
    bool is_modified;
    bool auto_save_enabled;
    int auto_save_interval;      // seconds
    bool session_resume_enabled; // Crash-recovery snapshots
    int session_interval;        // seconds
    TextFileInfo file_info;      // Encoding and line endings of the current file
    // Identity of current_file as npad last read or wrote it. Compared on
    // window activation to notice an edit made by something else. Re-captured
    // after EVERY write, including auto-save - npad's own atomic saves change
    // the file's identity, so without that it would report its own writes.
    FileStamp file_stamp;
    bool suppress_change_prompt; // "Don't ask again for this file", session only
    // The user chose to keep editing after another program changed the file
    // ("Keep editing: nothing is written"), so auto-save must not overwrite
    // that version either. Cleared by a manual save, Save As, reload or a new
    // document.
    bool auto_save_held;
    // The text (or its encoding / line ending) changed since the last crash
    // recovery snapshot was written, so the next session tick has something
    // new to save. Without it a large modified document was rewritten in
    // full, identically, on every tick.
    bool snapshot_dirty;
    Window *main_window;
} EditorState;

// Core editor functions
bool editor_init(void);
void editor_cleanup(void);

// Attach the main window (applies title, status bar info, auto-save timer)
void editor_set_main_window(Window *window);

// File operations
bool editor_new_file(void);
bool editor_open_file(const char *filename);

// Open the file named on the command line. Like Notepad, a path that does not
// exist (in a folder that does) asks "Cannot find the ... file. Do you want to
// create a new file?" and on Yes creates it empty and opens it, so Save goes
// straight back to that name. Anything else behaves as editor_open_file.
bool editor_open_startup_file(const char *filename);

// Whether handing this file to the system's default application could run it
// (a program, script, shortcut or installer, by name or by content). Asked
// before "Open with the default app" on a binary-looking file.
bool editor_is_program_file(const char *filename);

// Has current_file been changed on disk by something other than npad? Called
// when the window is activated and before a manual save (auto-save checks the
// disk itself, without the watch_file_changes preference, and never writes
// over a change). Returns the kind of change so the caller can offer the right
// choices - a deleted file cannot be reloaded, so it needs different wording.
typedef enum {
    EDITOR_FILE_UNCHANGED = 0,
    EDITOR_FILE_MODIFIED,
    EDITOR_FILE_DELETED
} EditorExternalChange;

EditorExternalChange editor_check_external_change(void);
// Stop reporting this change ("Keep editing"). Also holds auto-save until the
// user saves, because keeping their version means nothing is written yet.
void editor_accept_external_change(void);
void editor_suppress_change_prompt(void); // For this file, this session only
// Discards unsaved edits deliberately. Asks Open's binary and large-file
// questions first and reports a read failure itself; false (declined or
// failed) leaves the document and its baseline untouched.
bool editor_reload_from_disk(void);
bool editor_save_file(void);
// Save to filename in the document's current encoding. Nothing about the
// document changes unless the write succeeds.
bool editor_save_file_as(const char *filename);
bool editor_close_file(void);

// Text operations
void editor_cut(void);
void editor_copy(void);
void editor_paste(void);
void editor_select_all(void);
void editor_undo(void);
void editor_redo(void);

// State management
bool editor_is_modified(void);
void editor_set_modified(bool modified);
const char *editor_get_current_file(void);
void editor_set_startup_file(const char *filename);

// This instance's recovery slot id, for pointing a restart registration at the
// document this particular process is holding
const char *editor_session_slot_id(void);

// Restore a specific recovery slot at startup (set from the --recover option
// on windows spawned to reopen extra crashed sessions). Accepts "<slot>" or
// "<slot>:<claimer>"; returns false, and sets nothing, for anything that is
// not a slot id npad could have made (see session_is_valid_slot_id).
bool editor_set_recover_slot(const char *slot);

// The window was a --recover one with nothing left to restore and closed
// itself: it was never used, so it has no window state or settings to save
bool editor_exited_unused(void);

// Directory recovery snapshots live in (malloc'd; caller frees). NULL on
// error. Exposed for the Debug preferences page diagnostics.
char *editor_session_dir(void);

// Document format (applied when the file is next saved; marks modified)
void editor_set_line_ending(LineEnding line_ending);
LineEnding editor_get_line_ending(void);
void editor_set_encoding(TextEncoding encoding);
TextEncoding editor_get_encoding(void);

// Auto-save functionality
void editor_enable_auto_save(bool enabled);
bool editor_is_auto_save_enabled(void);
void editor_set_auto_save_interval(int seconds);
int editor_get_auto_save_interval(void);

// Session recovery (crash protection)
void editor_enable_session_resume(bool enabled);
bool editor_is_session_resume_enabled(void);

// Re-read preferences from settings and re-apply timers (used after another
// instance changes settings, or after an import)
void editor_reload_prefs(void);

// Event handling
bool editor_handle_event(const UIEvent *event);

// Utility functions
bool editor_prompt_save_changes(void);
// Effective default for large_file_warning_mb when the preference was never
// set: scales with installed RAM (1/64th, clamped 50..1024 MB), falling
// back to a fixed 100 MB when memory size is unavailable.
int editor_default_large_file_mb(void);
void editor_update_title(void);

// Read a file to insert into the document (Ctrl+drop), after the checks Open
// makes: a binary-looking file and one over the large-file threshold are each
// confirmed first, a read failure is reported, and a file npad can only read in
// part (a NUL in it) is confirmed before that part is used. Returns the text as
// malloc'd UTF-8 for the caller to insert and free, or NULL when there is
// nothing to insert - the user declined or the failure was already shown.
char *editor_read_file_for_insert(const char *filename);

#endif // EDITOR_H
