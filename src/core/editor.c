/*
 * npad - Editor Core Implementation
 * Core editor functionality and state management
 *
 * All functions here run on the single UI thread (events are dispatched
 * from the platform message loop), so editor state needs no locking.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "editor.h"
#include "error.h"
#include "file_ops.h"
#include "session.h"
#include "settings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define npad_getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#define npad_getpid getpid
#endif

// Files larger than this (in MB, configurable via "large_file_warning_mb")
// prompt for confirmation before opening. 0 disables the prompt. Used only
// when the platform cannot report installed memory (see
// editor_default_large_file_mb).
#define DEFAULT_LARGE_FILE_WARNING_MB 100

// How often to snapshot unsaved work when session recovery is enabled
#define DEFAULT_SESSION_INTERVAL_SEC 30

// Global editor state
EditorState g_editor = { 0 };
char *g_startup_file = NULL;

// This instance's recovery slot id (unique per process), and the specific
// slot to restore when launched with --recover (NULL for a normal launch).
static char g_session_slot[64] = { 0 };
static char *g_recover_slot = NULL;

// Modal UI raised from this file that is still open. The platform keeps
// dispatching timers while a modal box or file dialog runs its own loop, so
// the auto-save tick can arrive in the middle of "Save changes?", the Save As
// dialog or the "changed by another program" question - and writing then
// would make Don't Save, Cancel or No meaningless.
static int g_modal_depth = 0;

// The auto-save note currently in the status bar, so success clears only
// what auto-save itself put there (the area is shared with other messages)
static bool g_auto_save_notice = false;

// The timer intervals last handed to the platform (-1: none yet, so the next
// apply always arms). Every settings broadcast from another instance re-reads
// the preferences, and re-arming an unchanged timer restarted its countdown -
// a busy machine could keep pushing the next auto-save away indefinitely.
static int g_armed_auto_save = -1;
static int g_armed_session = -1;

static char *duplicate_string(const char *str) {
    if (!str)
        return NULL;
    char *copy = malloc(strlen(str) + 1);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

// Push the current file's encoding/line-ending info to the status bar
static void editor_update_status_info(void) {
    if (g_editor.main_window) {
        ui_set_status_info(g_editor.main_window, file_encoding_name(g_editor.file_info.encoding),
                           file_line_ending_name(g_editor.file_info.line_ending));
    }
}

// Apply the auto-save settings to the platform timer, only when they changed
static void editor_apply_auto_save_timer(void) {
    if (!g_editor.main_window)
        return;
    int seconds = g_editor.auto_save_enabled ? g_editor.auto_save_interval : 0;
    if (seconds == g_armed_auto_save)
        return;
    g_armed_auto_save = seconds;
    ui_set_auto_save_timer(g_editor.main_window, seconds);
}

// Apply the session-recovery settings to the platform timer, only when they
// changed
static void editor_apply_session_timer(void) {
    if (!g_editor.main_window)
        return;
    int seconds = g_editor.session_resume_enabled ? g_editor.session_interval : 0;
    if (seconds == g_armed_session)
        return;
    g_armed_session = seconds;
    ui_set_session_timer(g_editor.main_window, seconds);
}

// Wrappers for the modal UI this file raises, so the auto-save tick can tell
// that one is open (see g_modal_depth)
static bool editor_ask(const char *message, bool is_question) {
    g_modal_depth++;
    bool answer = ui_show_message_box(g_editor.main_window, "npad", message, is_question);
    g_modal_depth--;
    return answer;
}

static char *editor_run_save_dialog(FileDialogParams *params) {
    g_modal_depth++;
    char *filename = ui_show_save_dialog(g_editor.main_window, params);
    g_modal_depth--;
    return filename;
}

// The directory recovery snapshots live in (a subdirectory of the config
// dir so they sit apart from settings.json). Caller frees; NULL on error.
// Public (editor.h) so the Debug preferences page can report on it.
char *editor_session_dir(void) {
    char *config = settings_get_config_dir();
    if (!config)
        return NULL;
    char *dir = file_join_paths(config, "recovery");
    free(config);
    return dir;
}

// Write a crash-recovery snapshot of the current unsaved document. Only when
// something changed since the last one: the snapshot is the whole document,
// written and read back on the UI thread, so rewriting an unchanged large file
// every interval froze the window for nothing. A failed write leaves the flag
// set, so the next tick tries again.
static void editor_snapshot_session(void) {
    if (!g_editor.session_resume_enabled || !g_editor.is_modified || !g_editor.main_window)
        return;
    if (!g_editor.snapshot_dirty)
        return;

    char *dir = editor_session_dir();
    if (!dir)
        return;

    char *content = ui_get_text(g_editor.main_window);
    if (content) {
        if (session_write(dir, g_session_slot, content, g_editor.current_file,
                          g_editor.file_info.encoding, g_editor.file_info.line_ending)) {
            g_editor.snapshot_dirty = false;
        }
        free(content);
    }
    free(dir);
}

const char *editor_session_slot_id(void) {
    return g_session_slot;
}

// Snapshot for a handoff: something else is closing npad (an in-app update, or
// Windows restarting the machine), so the document must come back afterwards
// exactly as it is now, still unsaved.
//
// Deliberately ignores session_resume_enabled. That preference means "don't
// nag me about crashes"; honouring it here would silently destroy the buffer
// of anyone who turned it off, which is the one outcome npad's own rule
// forbids. The handoff marker guarantees such a slot is never presented with
// crash wording, so the preference keeps its user-visible meaning.
// Returns false only when there WAS unsaved work and it could not be parked -
// a full or read-only recovery volume, say. The caller must not then exit
// silently: npad promised to bring the document back, and quitting on a
// promise it could not keep would destroy the buffer outright.
static bool editor_snapshot_handoff(void) {
    if (!g_editor.is_modified || !g_editor.main_window)
        return true; // Nothing unsaved to carry across

    char *dir = editor_session_dir();
    if (!dir)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content) {
        free(dir);
        return false;
    }

    bool parked = session_write(dir, g_session_slot, content, g_editor.current_file,
                                g_editor.file_info.encoding, g_editor.file_info.line_ending);
    if (parked) {
        // A failed mark is survivable: the content is safe either way, it just
        // comes back under the crash prompt instead of silently.
        session_mark_handoff(dir, g_session_slot);
    }

    free(content);
    free(dir);
    return parked;
}

// A session end was cancelled: keep the snapshot (it costs nothing and a real
// crash could still follow) but drop the handoff mark, so if npad does die
// later it is reported honestly as a crash rather than restored silently.
static void editor_cancel_handoff(void) {
    char *dir = editor_session_dir();
    if (dir) {
        session_clear_handoff(dir, g_session_slot);
        free(dir);
    }
}

// Remove this instance's recovery snapshot (after a clean save or clean exit)
static void editor_clear_session(void) {
    char *dir = editor_session_dir();
    if (dir) {
        session_clear_slot(dir, g_session_slot);
        free(dir);
    }
}

// Restore one slot's content into this window. Returns true if restored.
static bool editor_restore_slot(const char *dir, const char *slot_id) {
    char *path = NULL;
    TextFileInfo info = g_editor.file_info;
    char *content = session_take(dir, slot_id, &path, &info.encoding, &info.line_ending);
    if (!content) {
        free(path);
        return false;
    }

    ui_set_text(g_editor.main_window, content);
    free(content);

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = path; // Ownership transferred (may be NULL)
    g_editor.file_info = info;
    editor_set_modified(true);
    editor_update_status_info();
    return true;
}

// A slot id is "<time>-<pid>" in hex; a slot whose owning pid is still a
// running npad process is a live session, not a crash to recover.
static bool slot_owner_is_running(const char *slot_id) {
    const char *dash = strrchr(slot_id, '-');
    if (!dash)
        return false;
    long pid = strtol(dash + 1, NULL, 16);
    return ui_pid_is_running(pid);
}

// On startup, offer to restore work left behind by an unclean exit.
// The check is unconditional on the current enabled state: a snapshot only
// exists on disk if session resume was enabled when it was last written
// (disabling clears it), and the enabled flag may not have been persisted
// if the previous run was killed before a clean exit. When several sessions
// were open, each left its own slot; the extras reopen in new windows.
// Slots owned by still-running npad instances are skipped (they are live
// snapshots, not crashed sessions).
static void editor_check_session_recovery(void) {
    if (!g_editor.main_window)
        return;

    char *dir = editor_session_dir();
    if (!dir)
        return;

    // Launched to recover one specific slot (a window spawned for an extra
    // leftover session): restore it silently, no prompt.
    if (g_recover_slot) {
        editor_restore_slot(dir, g_recover_slot);
        free(dir);
        return;
    }

    int listed = 0;
    char **slots = session_list_slots(dir, &listed);

    // Drop slots belonging to still-running instances (compacting the array)
    int count = 0;
    for (int i = 0; i < listed; i++) {
        if (slot_owner_is_running(slots[i])) {
            free(slots[i]);
            slots[i] = NULL;
        } else {
            slots[count++] = slots[i];
        }
    }

    // Partition: handoff slots were parked by an update or a Windows restart,
    // so they come back with no prompt in either direction. Everything left is
    // a genuine leftover and keeps the crash wording. Handoffs are moved to the
    // front so the array below stays a single contiguous run.
    int handoff = 0;
    for (int i = 0; i < count; i++) {
        if (session_is_handoff(dir, slots[i])) {
            char *marked = slots[i];
            for (int j = i; j > handoff; j--) {
                slots[j] = slots[j - 1];
            }
            slots[handoff++] = marked;
        }
    }

    if (handoff > 0) {
        // Restore the first into this window and fan the rest out into new
        // ones, exactly as the crash path does - but silently.
        bool restored_here = false;
        int cascade = 1;
        for (int i = 0; i < handoff; i++) {
            session_clear_handoff(dir, slots[i]); // One-shot: never restores twice
            if (!restored_here) {
                restored_here = editor_restore_slot(dir, slots[i]);
            } else {
                ui_launch_recovery_instance(slots[i], cascade++);
            }
        }
        // Any remaining slots are real crashes, but this window is now occupied
        // by restored work, so leave them for the next launch to offer rather
        // than clobbering what we just restored.
        session_free_slots(slots, count);
        free(dir);
        return;
    }

    if (count > 0) {
        char message[256];
        if (count == 1) {
            snprintf(message, sizeof(message),
                     "npad may have closed unexpectedly.\nRestore your unsaved work?");
        } else {
            snprintf(message, sizeof(message),
                     "npad may have closed unexpectedly.\n"
                     "Restore your %d unsaved documents?",
                     count);
        }

        if (ui_show_message_box(g_editor.main_window, "npad", message, true)) {
            // Restore the first slot here; reopen the rest in new windows,
            // cascaded so they do not stack on top of each other
            bool restored_here = false;
            int cascade = 1;
            for (int i = 0; i < count; i++) {
                if (!restored_here) {
                    restored_here = editor_restore_slot(dir, slots[i]);
                } else {
                    ui_launch_recovery_instance(slots[i], cascade++);
                }
            }
        } else {
            for (int i = 0; i < count; i++) {
                session_clear_slot(dir, slots[i]);
            }
        }
    }
    session_free_slots(slots, count);

    free(dir);
}

// Encoding/line endings for new documents, from user preferences
static void load_default_file_info(TextFileInfo *info) {
    int encoding = settings_get_int("default_encoding", (int) NPAD_ENC_UTF8);
    int line_ending = settings_get_int("default_line_ending", (int) NPAD_DEFAULT_LINE_ENDING);

    if (encoding < 0 || encoding > (int) NPAD_ENC_ANSI) {
        encoding = (int) NPAD_ENC_UTF8;
    }
    if (line_ending < 0 || line_ending > (int) NPAD_EOL_CR) {
        line_ending = (int) NPAD_DEFAULT_LINE_ENDING;
    }

    info->encoding = (TextEncoding) encoding;
    info->line_ending = (LineEnding) line_ending;
}

// A file with no line breaks has nothing to detect, so it takes the user's
// default line ending (not a hard-coded one): the first Enter they type is
// then saved the way their preference says
static void apply_default_eol_if_no_breaks(TextFileInfo *info, const char *content) {
    if (!content || strpbrk(content, "\r\n"))
        return;
    TextFileInfo defaults;
    load_default_file_info(&defaults);
    info->line_ending = defaults.line_ending;
}

bool editor_init(void) {
    memset(&g_editor, 0, sizeof(EditorState));
    g_editor.file_stamp.size = -1; // Nothing stamped yet
    g_modal_depth = 0;
    g_auto_save_notice = false;
    g_armed_auto_save = -1;
    g_armed_session = -1;

    // Notepad-alignment defaults: auto-save is destructive (overwrites the
    // file from a timer) so it is opt-in; crash-recovery snapshots never
    // touch the user's file, so they are on by default
    g_editor.auto_save_enabled = settings_get_bool("auto_save_enabled", false);
    g_editor.auto_save_interval = settings_get_int("auto_save_interval", 300);
    if (g_editor.auto_save_interval < 10) {
        g_editor.auto_save_interval = 10; // Sanity floor
    }

    g_editor.session_resume_enabled = settings_get_bool("session_resume_enabled", true);
    g_editor.session_interval = settings_get_int("session_interval", DEFAULT_SESSION_INTERVAL_SEC);
    if (g_editor.session_interval < 5) {
        g_editor.session_interval = 5;
    }

    // Unique recovery slot id for this instance (time + pid). Distinct across
    // concurrently running instances so their snapshots never collide.
    snprintf(g_session_slot, sizeof(g_session_slot), "%lx-%x", (unsigned long) time(NULL),
             (unsigned) npad_getpid());

    load_default_file_info(&g_editor.file_info);

    ui_set_event_handler(editor_handle_event);

    return true;
}

void editor_cleanup(void) {
    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    if (g_startup_file) {
        free(g_startup_file);
        g_startup_file = NULL;
    }

    if (g_recover_slot) {
        free(g_recover_slot);
        g_recover_slot = NULL;
    }
}

void editor_set_recover_slot(const char *slot) {
    if (g_recover_slot) {
        free(g_recover_slot);
        g_recover_slot = NULL;
    }
    if (slot) {
        g_recover_slot = duplicate_string(slot);
    }
}

void editor_set_main_window(Window *window) {
    g_editor.main_window = window;
    editor_update_title();
    editor_update_status_info();
    // A new window has no timers yet, whatever an earlier one had
    g_armed_auto_save = -1;
    g_armed_session = -1;
    editor_apply_auto_save_timer();
    editor_apply_session_timer();
    // The crash-recovery scan runs via UI_EVENT_STARTUP_DEFERRED (shortly
    // after the first paint) so window startup stays instant
}

bool editor_new_file(void) {
    // Check if current file needs saving
    if (!editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    if (g_editor.main_window) {
        ui_clear_text(g_editor.main_window);
    }

    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    load_default_file_info(&g_editor.file_info);
    g_editor.file_stamp.size = -1;
    g_editor.file_stamp.write_time = 0;
    g_editor.auto_save_held = false;
    g_editor.suppress_change_prompt = false;

    editor_set_modified(false);
    editor_update_status_info();

    return true;
}

int editor_default_large_file_mb(void) {
    size_t mem_mb = ui_system_memory_mb();
    if (mem_mb == 0)
        return DEFAULT_LARGE_FILE_WARNING_MB;
    int mb = (int) (mem_mb / 64);
    if (mb < 50)
        mb = 50;
    if (mb > 1024)
        mb = 1024;
    return mb;
}

// The final path component (borrowed pointer into path)
static const char *path_base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    if (!separator || (backslash && backslash > separator))
        separator = backslash;
    return separator ? separator + 1 : path;
}

static bool ascii_iequal(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char) (*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char) (*b - 'A' + 'a') : *b;
        if (ca != cb)
            return false;
    }
    return *a == *b;
}

bool editor_is_program_file(const char *filename) {
    if (!filename)
        return false;

    // By name: what Windows or a Linux desktop runs, installs or follows
    // rather than opens as a document
    static const char *const k_program_extensions[] = {
        "exe",        "com",  "bat",       "cmd",         "ps1",      "psm1",       "psd1",
        "vbs",        "vbe",  "js",        "jse",         "wsf",      "wsh",        "ws",
        "msc",        "msi",  "msp",       "mst",         "scr",      "pif",        "cpl",
        "hta",        "lnk",  "reg",       "jar",         "inf",      "scf",        "url",
        "chm",        "sct",  "gadget",    "application", "appx",     "appxbundle", "msix",
        "msixbundle", "xbap", "appref-ms", "sh",          "bash",     "zsh",        "csh",
        "ksh",        "fish", "command",   "run",         "appimage", "desktop",    "py",
        "pyw",        "pl",   "rb"
    };
    const char *base = path_base_name(filename);
    const char *dot = strrchr(base, '.');
    if (dot && dot[1]) {
        for (size_t i = 0; i < sizeof(k_program_extensions) / sizeof(k_program_extensions[0]);
             i++) {
            if (ascii_iequal(dot + 1, k_program_extensions[i]))
                return true;
        }
    }

#ifndef _WIN32
    // Anything marked executable is run by some file managers' default action
    struct stat st;
    if (stat(filename, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111))
        return true;
#endif

    // By content, so a renamed program is still recognised: a PE image ("MZ"),
    // an ELF binary, or a script with an interpreter line. strncmp stops at
    // the terminator of a shorter file.
    char *head = file_read_text_limited(filename, 4);
    if (!head)
        return false;
    bool program = (head[0] == 'M' && head[1] == 'Z') ||
                   ((unsigned char) head[0] == 0x7F && strncmp(head + 1, "ELF", 3) == 0) ||
                   (head[0] == '#' && head[1] == '!');
    free(head);
    return program;
}

// "Open with the default app" on something that would run rather than open:
// say so before handing it over. Returns true to go ahead.
static bool editor_confirm_run_program(const char *filename) {
    char message[512];
    snprintf(message, sizeof(message),
             "\"%.300s\" is a program, script or shortcut.\n\n"
             "Opening it with the default app may run it. Do you want to run it?",
             path_base_name(filename));
    return editor_ask(message, true);
}

// Notepad's .LOG: a document whose first line is exactly ".LOG" (upper case,
// nothing else on that line) gets the time and date on a new line at its end
// every time it is opened
static bool text_starts_log(const char *text) {
    return text && strncmp(text, ".LOG", 4) == 0 &&
           (text[4] == '\0' || text[4] == '\r' || text[4] == '\n');
}

bool editor_open_file(const char *filename) {
    if (!filename)
        return false;

    // Pre-open checks run before the save prompt so declining either one
    // never costs the user a pointless "save changes?" round trip

    // A binary-looking file is usually a mistake: offer the system's own
    // handler for it instead of a wall of control characters
    if (file_looks_binary(filename)) {
        g_modal_depth++;
        UiOpenChoice choice = ui_prompt_binary_open(g_editor.main_window, filename);
        g_modal_depth--;
        if (choice == UI_OPEN_CANCEL)
            return false;
        if (choice == UI_OPEN_WITH_DEFAULT) {
            // The default app for a program is the program itself
            if (editor_is_program_file(filename) && !editor_confirm_run_program(filename))
                return false;
            ui_open_with_default_app(filename);
            return false; // Current document stays untouched
        }
    }

    // Warn before opening very large files
    int warn_mb = settings_get_int("large_file_warning_mb", editor_default_large_file_mb());
    if (warn_mb > 0) {
        size_t size = file_get_size(filename);
        if (size > (size_t) warn_mb * 1024 * 1024) {
            char message[512];
            snprintf(message, sizeof(message),
                     "The file is %.1f MB and may take a while to open.\n"
                     "Do you want to continue?",
                     size / (1024.0 * 1024.0));
            if (!editor_ask(message, true)) {
                return false;
            }
        }
    }

    // Check if current file needs saving
    if (!editor_prompt_save_changes()) {
        return false; // User cancelled
    }

    // Load file content, detecting encoding and line endings
    TextFileInfo info;
    char *content = file_read_text_ex(filename, &info);
    if (!content) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not open file: %.300s\n%.150s", filename,
                 file_get_last_error());
        editor_ask(error_msg, false);
        return false;
    }

    char *new_path = duplicate_string(filename);
    if (!new_path) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                         "Failed to allocate memory for current file path");
        free(content);
        return false;
    }

    // Both need the text, so before it is freed. A fragment of a binary file
    // is not a log, and stamping it would only mark it modified.
    apply_default_eol_if_no_breaks(&info, content);
    bool log_file = !info.truncated && text_starts_log(content);
    if (g_editor.main_window) {
        ui_set_text(g_editor.main_window, content);
    }
    free(content);

    // Baseline for external-change detection, and a fresh slate for the
    // per-file "don't ask again" choice and the auto-save hold
    file_get_stamp(filename, &g_editor.file_stamp);
    g_editor.suppress_change_prompt = false;
    g_editor.auto_save_held = false;

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = new_path;
    g_editor.file_info = info;

    settings_add_recent_file(filename);

    editor_set_modified(false);
    editor_update_status_info();

    // Last, once the document is fully this file: the stamp is an edit like
    // any other (undoable, modified, protected by the recovery snapshot)
    if (log_file && g_editor.main_window) {
        ui_append_time_date_line(g_editor.main_window);
        if (!g_editor.is_modified)
            editor_set_modified(true); // In case the platform sent no change event
    }

    return true;
}

bool editor_open_startup_file(const char *filename) {
    if (!filename)
        return false;

    if (!file_exists(filename)) {
        // Only when the folder is there: a mistyped folder is an error, as in
        // Notepad ("The system cannot find the path specified"), which the
        // ordinary open reports
        char *dir = file_get_directory(filename);
        bool folder_exists = dir && file_exists(dir);
        free(dir);

        if (folder_exists) {
            char message[600];
            snprintf(message, sizeof(message),
                     "Cannot find the %.400s file.\n\nDo you want to create a new file?", filename);
            if (!editor_ask(message, true))
                return false; // No: an Untitled document, as in Notepad

            // Notepad creates it there and then, so it exists under that name
            // from the start and Save goes straight back to it
            TextFileInfo info;
            load_default_file_info(&info);
            info.truncated = false;
            if (!file_write_text_ex(filename, "", &info)) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "Could not create file: %.300s\n%.150s",
                         filename, file_get_last_error());
                editor_ask(error_msg, false);
                return false;
            }
        }
    }

    return editor_open_file(filename);
}

// Warn before a user-initiated save that would replace characters the ANSI
// code page cannot represent with '?'. Returns false if the user cancels.
// Classic Notepad behaviour; auto-save never prompts (it skips instead).
// encoding is the one the save will write, which for Save As is the dialog's
// choice rather than the document's.
static bool editor_confirm_lossy_ansi(const char *content, TextEncoding encoding) {
    if (encoding != NPAD_ENC_ANSI || !content || !file_ansi_is_lossy(content)) {
        return true;
    }
    return editor_ask("This document contains characters that cannot be saved in "
                      "ANSI encoding.\nThey will be replaced with '?'. Save anyway?",
                      true);
}

// Whether current_file on disk is no longer what npad last read or wrote,
// whatever the watch_file_changes preference or a "Stop telling me" choice
// says: those decide whether to ASK, but auto-save must never overwrite
// another program's version without asking. A file that has gone counts as
// changed too - recreating something the user just deleted or moved is not a
// save. A file that did not exist when stamped and still does not is
// unchanged, so the first save of a new path proceeds.
static bool editor_disk_copy_changed(void) {
    if (!g_editor.current_file)
        return false;
    FileStamp now;
    file_get_stamp(g_editor.current_file, &now); // size -1 when it is gone
    return !file_stamp_equal(&now, &g_editor.file_stamp);
}

// --- External change detection --------------------------------------------
//
// Checked when the window is activated rather than on a timer: that is the
// moment the user would notice anyway, and it costs nothing while npad is not
// in front. Also checked before a manual save, which is the genuinely
// dangerous moment - silently overwriting someone else's edit. Auto-save uses
// editor_disk_copy_changed instead and simply does not write.
EditorExternalChange editor_check_external_change(void) {
    if (!g_editor.current_file || g_editor.suppress_change_prompt)
        return EDITOR_FILE_UNCHANGED;
    if (!settings_get_bool("watch_file_changes", false))
        return EDITOR_FILE_UNCHANGED;
    // Nothing to compare against - the file was never successfully stamped
    if (g_editor.file_stamp.size < 0)
        return EDITOR_FILE_UNCHANGED;

    FileStamp now;
    if (!file_get_stamp(g_editor.current_file, &now)) {
        return EDITOR_FILE_DELETED; // Reload is meaningless; the caller must differ
    }
    return file_stamp_equal(&now, &g_editor.file_stamp) ? EDITOR_FILE_UNCHANGED
                                                        : EDITOR_FILE_MODIFIED;
}

void editor_suppress_change_prompt(void) {
    // Session-scoped and in memory only. Persisting it would let a stale
    // suppression silently hide a real change months later; it is cleared
    // whenever a different file becomes current.
    g_editor.suppress_change_prompt = true;
}

void editor_accept_external_change(void) {
    // Adopt what is on disk now as the baseline, so the same change is not
    // reported again on every activation. If that really was a change, the
    // user has just chosen to keep their version without writing it ("Keep
    // editing: nothing is written"), so auto-save waits for their next save
    // rather than overwriting the other program's version a tick later.
    if (g_editor.current_file) {
        FileStamp now;
        file_get_stamp(g_editor.current_file, &now);
        if (!file_stamp_equal(&now, &g_editor.file_stamp))
            g_editor.auto_save_held = true;
        g_editor.file_stamp = now;
    }
}

bool editor_reload_from_disk(void) {
    if (!g_editor.current_file || !g_editor.main_window)
        return false;

    TextFileInfo info;
    char *content = file_read_text_ex(g_editor.current_file, &info);
    if (!content)
        return false;
    apply_default_eol_if_no_breaks(&info, content);

    ui_set_text(g_editor.main_window, content);
    free(content);

    g_editor.file_info = info;
    file_get_stamp(g_editor.current_file, &g_editor.file_stamp);
    g_editor.auto_save_held = false; // The buffer is the disk's version now
    editor_set_modified(false);      // Now identical to the file again
    editor_update_status_info();
    editor_clear_session(); // The unsaved copy this replaced is gone by choice
    return true;
}

// --- Saving ---------------------------------------------------------------

#ifdef _WIN32
// Volume serial plus file index: the same file under any name (hard links,
// 8.3 names, a different case or a mapped drive)
static bool get_file_identity(const char *path, BY_HANDLE_FILE_INFORMATION *out) {
    int count = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (count <= 0)
        return false;
    wchar_t *wide = malloc((size_t) count * sizeof(wchar_t));
    if (!wide)
        return false;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count);
    // No access rights asked for: only the identity is read, so this opens
    // even a file another program holds locked
    HANDLE handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    BOOL ok = GetFileInformationByHandle(handle, out);
    CloseHandle(handle);
    return ok != 0;
}
#endif

// Whether two paths name the same existing file, by identity rather than by
// spelling (a hard link or another spelling of the path is the same file)
static bool paths_are_same_file(const char *a, const char *b) {
    if (!a || !b)
        return false;

    char *full_a = file_get_absolute_path(a);
    char *full_b = file_get_absolute_path(b);
#ifdef _WIN32
    bool same = full_a && full_b && _stricmp(full_a, full_b) == 0;
#else
    bool same = full_a && full_b && strcmp(full_a, full_b) == 0;
#endif
    free(full_a);
    free(full_b);
    if (same)
        return true;

#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info_a, info_b;
    if (!get_file_identity(a, &info_a) || !get_file_identity(b, &info_b))
        return false;
    return info_a.dwVolumeSerialNumber == info_b.dwVolumeSerialNumber &&
           info_a.nFileIndexHigh == info_b.nFileIndexHigh &&
           info_a.nFileIndexLow == info_b.nFileIndexLow;
#else
    struct stat st_a, st_b;
    if (stat(a, &st_a) != 0 || stat(b, &st_b) != 0)
        return false;
    return st_a.st_dev == st_b.st_dev && st_a.st_ino == st_b.st_ino;
#endif
}

// Where Save As starts for a document npad could only partly load:
// "C:\pics\photo.png" becomes "C:\pics\photo (text).txt", so pressing Enter
// in the dialog can never pick the original. Caller frees.
static char *derived_text_name(const char *path) {
    static const char suffix[] = " (text).txt";
    if (!path)
        return duplicate_string("Untitled.txt");
    const char *base = path_base_name(path);
    const char *dot = strrchr(base, '.');
    size_t stem = (dot && dot != base) ? (size_t) (dot - path) : strlen(path);
    char *name = malloc(stem + sizeof(suffix));
    if (!name)
        return NULL;
    memcpy(name, path, stem);
    memcpy(name + stem, suffix, sizeof(suffix));
    return name;
}

static void editor_set_auto_save_notice(const char *message) {
    if (!g_editor.main_window)
        return;
    ui_set_status_message(g_editor.main_window, message);
    g_auto_save_notice = (message != NULL);
}

// Write the document over current_file and adopt the result as the new
// baseline. Shared by Save and auto-save; never prompts.
static bool editor_write_current(const char *content) {
    if (!file_write_text_ex(g_editor.current_file, content, &g_editor.file_info))
        return false;
    // npad's saves are atomic (temp + rename), so every write changes the
    // file's identity. Without re-stamping, npad would report its OWN write
    // as an external change the next time the window is activated.
    file_get_stamp(g_editor.current_file, &g_editor.file_stamp);
    g_editor.auto_save_held = false;
    editor_set_modified(false);
    editor_clear_session(); // Saved work no longer needs recovery
    if (g_auto_save_notice)
        editor_set_auto_save_notice(NULL);
    return true;
}

static bool editor_save_as_encoded(const char *filename, TextEncoding encoding);

// The Save As dialog, then the save. Only a successful write changes the
// document: a cancelled dialog, a declined ANSI warning or a failed write
// leave its file, encoding and status bar exactly as they were.
static bool editor_save_as_dialog(const char *default_name) {
    FileDialogParams params = { .title = "Save As",
                                .default_filename = default_name ? default_name : "Untitled.txt",
                                .filter = "Text Documents (*.txt)|*.txt|All Files (*.*)|*.*",
                                .save_dialog = true,
                                .encoding = g_editor.file_info.encoding };

    char *filename = editor_run_save_dialog(&params);
    if (!filename)
        return false; // User cancelled

    bool saved = editor_save_as_encoded(filename, params.encoding); // The dialog's picker
    free(filename);
    return saved;
}

// Save As for a document that is only the part of a binary file before its
// first NUL, starting from a name derived from the original
static bool editor_save_truncated_as(void) {
    char *suggested = derived_text_name(g_editor.current_file);
    bool saved = editor_save_as_dialog(suggested ? suggested : "Untitled.txt");
    free(suggested);
    return saved;
}

bool editor_save_file(void) {
    if (!g_editor.current_file) {
        // No current file, prompt for save as
        return editor_save_as_dialog("Untitled.txt");
    }

    if (!g_editor.main_window)
        return false;

    // npad could not read this file in full - it held a NUL, and npad's text is
    // NUL-terminated throughout, so only the part before it was loaded. Saving
    // would replace the original with that fragment: a 120 KB image becomes
    // nine bytes. Divert to Save As so the original is never the target.
    if (g_editor.file_info.truncated) {
        editor_ask("This file contains binary data, so npad could only load the "
                   "part before it.\n\n"
                   "Saving over the original would discard the rest, so choose a "
                   "different file to save this text to.",
                   false);
        return editor_save_truncated_as(); // Cancelled: original intact, the point
    }

    // Another program has rewritten the file since npad read it. Saving would
    // silently replace its version, so ask first; No keeps both by saving
    // this one under another name (cancelling that writes nothing at all).
    if (editor_check_external_change() == EDITOR_FILE_MODIFIED) {
        char message[640];
        snprintf(message, sizeof(message),
                 "%.400s has been changed by another program since npad opened it.\n\n"
                 "Do you want to replace it with your version?\n"
                 "Choose No to save your version as a different file instead.",
                 g_editor.current_file);
        if (!editor_ask(message, true))
            return editor_save_as_dialog(g_editor.current_file);
    }

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    if (!editor_confirm_lossy_ansi(content, g_editor.file_info.encoding)) {
        free(content);
        return false; // User declined the lossy ANSI save
    }

    bool success = editor_write_current(content);
    free(content);

    if (!success) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s",
                 g_editor.current_file, file_get_last_error());
        editor_ask(error_msg, false);
    }

    return success;
}

// Auto-save: silent by design. It runs from a timer, so it never raises a
// dialog - a failure on Win32 used to open another modal error box every
// interval, each nested inside the last. Anything that would need the user
// (a binary fragment, a lossy ANSI save, another program's change, a write
// error) skips the write and says why in the status bar instead; the next
// manual save gets the full explanation, and the recovery snapshot protects
// the text meanwhile.
static void editor_auto_save(void) {
    static bool in_progress = false;
    if (in_progress || g_modal_depth > 0)
        return; // Never inside one of npad's own questions or dialogs
    if (!g_editor.auto_save_enabled || !g_editor.is_modified || !g_editor.current_file ||
        !g_editor.main_window)
        return;
    // Saving a fragment of a binary file is a Save As, which is a dialog
    if (g_editor.file_info.truncated)
        return;

    if (g_editor.auto_save_held || editor_disk_copy_changed()) {
        editor_set_auto_save_notice(
            "Auto-save paused: the file was changed outside npad. Save to resume.");
        return;
    }

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return;
    if (g_editor.file_info.encoding == NPAD_ENC_ANSI && file_ansi_is_lossy(content)) {
        free(content);
        editor_set_auto_save_notice(
            "Auto-save skipped: some characters cannot be saved in ANSI. Save to choose.");
        return;
    }

    in_progress = true;
    bool saved = editor_write_current(content);
    in_progress = false;
    free(content);

    if (!saved) {
        char notice[256];
        snprintf(notice, sizeof(notice), "Auto-save failed: %.200s", file_get_last_error());
        editor_set_auto_save_notice(notice);
    }
}

// Save to filename in the given encoding. The encoding (and everything else
// about the document) is committed only after the write succeeded: setting
// it first meant a failed or declined Save As left the document switched to
// the new encoding while the status bar still showed the old one, and the
// next Ctrl+S re-encoded the original file without a word.
static bool editor_save_as_encoded(const char *filename, TextEncoding encoding) {
    if (!filename || !g_editor.main_window)
        return false;

    // A fragment of a binary file must never land on the file it came from,
    // whatever name the dialog was given for it (the same path, another
    // spelling of it, or a hard link)
    if (g_editor.file_info.truncated && paths_are_same_file(filename, g_editor.current_file)) {
        editor_ask("This is the file npad could only partly load, because it contains "
                   "binary data.\n\n"
                   "Saving over it would discard everything after that point, so choose a "
                   "different file to save this text to.",
                   false);
        return false;
    }

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    if (!editor_confirm_lossy_ansi(content, encoding)) {
        free(content);
        return false; // User declined the lossy ANSI save
    }

    // Whatever was loaded is about to be written in full to THIS file, so it
    // is no longer a fragment of something larger and Save may overwrite it
    TextFileInfo info = g_editor.file_info;
    info.encoding = encoding;
    info.truncated = false;

    bool success = file_write_text_ex(filename, content, &info);
    free(content);

    if (!success) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s", filename,
                 file_get_last_error());
        editor_ask(error_msg, false);
        return false;
    }

    char *new_path = duplicate_string(filename);
    if (!new_path) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                         "Failed to allocate memory for new file path");
        return false;
    }

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = new_path;
    g_editor.file_info = info;
    file_get_stamp(new_path, &g_editor.file_stamp);
    g_editor.suppress_change_prompt = false; // A different file, fresh slate
    g_editor.auto_save_held = false;

    settings_add_recent_file(filename);
    editor_set_modified(false);
    editor_clear_session(); // Saved work no longer needs recovery
    editor_update_status_info();
    if (g_auto_save_notice)
        editor_set_auto_save_notice(NULL);
    return true;
}

bool editor_save_file_as(const char *filename) {
    return editor_save_as_encoded(filename, g_editor.file_info.encoding);
}

bool editor_close_file(void) {
    return editor_new_file();
}

void editor_cut(void) {
    if (g_editor.main_window) {
        ui_cut(g_editor.main_window);
    }
}

void editor_copy(void) {
    if (g_editor.main_window) {
        ui_copy(g_editor.main_window);
    }
}

void editor_paste(void) {
    if (g_editor.main_window) {
        ui_paste(g_editor.main_window);
    }
}

void editor_select_all(void) {
    if (g_editor.main_window) {
        ui_select_all(g_editor.main_window);
    }
}

void editor_undo(void) {
    if (g_editor.main_window) {
        ui_undo(g_editor.main_window);
    }
}

void editor_redo(void) {
    if (g_editor.main_window) {
        ui_redo(g_editor.main_window);
    }
}

bool editor_is_modified(void) {
    return g_editor.is_modified;
}

void editor_set_modified(bool modified) {
    g_editor.is_modified = modified;
    // Modified: what the recovery slot holds (if anything) is out of date.
    // Unmodified: there is nothing to protect until the next edit.
    g_editor.snapshot_dirty = modified;
    if (g_editor.main_window) {
        ui_set_text_modified(g_editor.main_window, modified);
    }
    editor_update_title();
}

const char *editor_get_current_file(void) {
    return g_editor.current_file;
}

void editor_set_line_ending(LineEnding line_ending) {
    if (g_editor.file_info.line_ending == line_ending)
        return;
    g_editor.file_info.line_ending = line_ending;
    editor_set_modified(true);
    editor_update_status_info();
}

LineEnding editor_get_line_ending(void) {
    return g_editor.file_info.line_ending;
}

void editor_set_encoding(TextEncoding encoding) {
    if (g_editor.file_info.encoding == encoding)
        return;
    g_editor.file_info.encoding = encoding;
    editor_set_modified(true);
    editor_update_status_info();
}

TextEncoding editor_get_encoding(void) {
    return g_editor.file_info.encoding;
}

void editor_set_startup_file(const char *filename) {
    if (g_startup_file) {
        free(g_startup_file);
        g_startup_file = NULL;
    }

    if (filename) {
        // Resolve now, while the working directory is the one the name was
        // given relative to: Recent Files and crash recovery store this path
        // and reopen it later from somewhere else
        g_startup_file = file_get_absolute_path(filename);
        if (!g_startup_file)
            g_startup_file = duplicate_string(filename);
        if (!g_startup_file) {
            NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                             "Failed to allocate memory for startup file path");
        }
    }
}

void editor_enable_auto_save(bool enabled) {
    g_editor.auto_save_enabled = enabled;
    settings_set_bool("auto_save_enabled", enabled);
    editor_apply_auto_save_timer();
}

bool editor_is_auto_save_enabled(void) {
    return g_editor.auto_save_enabled;
}

void editor_set_auto_save_interval(int seconds) {
    if (seconds < 10) {
        seconds = 10;
    }
    g_editor.auto_save_interval = seconds;
    settings_set_int("auto_save_interval", seconds);
    editor_apply_auto_save_timer();
}

int editor_get_auto_save_interval(void) {
    return g_editor.auto_save_interval;
}

void editor_enable_session_resume(bool enabled) {
    g_editor.session_resume_enabled = enabled;
    settings_set_bool("session_resume_enabled", enabled);

    // Pick up any interval change saved alongside this toggle
    g_editor.session_interval = settings_get_int("session_interval", DEFAULT_SESSION_INTERVAL_SEC);
    if (g_editor.session_interval < 5) {
        g_editor.session_interval = 5;
    }

    // Persist immediately so the flag survives even if this run is later
    // killed without a clean exit
    settings_save();

    editor_apply_session_timer();
    if (!enabled) {
        editor_clear_session(); // Drop any snapshot when the feature is turned off
    } else if (g_editor.is_modified) {
        g_editor.snapshot_dirty = true; // Turning it off dropped the old snapshot
    }
}

bool editor_is_session_resume_enabled(void) {
    return g_editor.session_resume_enabled;
}

void editor_reload_prefs(void) {
    g_editor.auto_save_enabled = settings_get_bool("auto_save_enabled", false);
    g_editor.auto_save_interval = settings_get_int("auto_save_interval", 300);
    if (g_editor.auto_save_interval < 10) {
        g_editor.auto_save_interval = 10;
    }

    bool was_resuming = g_editor.session_resume_enabled;
    g_editor.session_resume_enabled = settings_get_bool("session_resume_enabled", true);
    g_editor.session_interval = settings_get_int("session_interval", DEFAULT_SESSION_INTERVAL_SEC);
    if (g_editor.session_interval < 5) {
        g_editor.session_interval = 5;
    }
    if (g_editor.session_resume_enabled && !was_resuming && g_editor.is_modified) {
        g_editor.snapshot_dirty = true; // Switched back on elsewhere: protect this one too
    }

    // Each apply re-arms only a timer whose interval actually changed, so the
    // broadcast every other instance sends does not keep restarting them
    editor_apply_auto_save_timer();
    editor_apply_session_timer();
}

bool editor_handle_event(const UIEvent *event) {
    if (!event)
        return false;

    switch (event->type) {
        case UI_EVENT_QUIT:
            if (!editor_prompt_save_changes()) {
                return false; // Cancel quit
            }
            editor_clear_session(); // Clean exit: nothing to recover
            ui_quit();
            return true;

        // npad is being closed by something else, so it must come back as it
        // is. No prompt, and pointedly no editor_clear_session() - that call
        // is what would destroy the snapshot we just took.
        case UI_EVENT_QUIT_HANDOFF:
            if (!editor_snapshot_handoff()) {
                return false; // Could not park it - do not exit on a broken promise
            }
            ui_quit();
            return true;

        case UI_EVENT_HANDOFF_SNAPSHOT:
            return editor_snapshot_handoff();

        case UI_EVENT_HANDOFF_CANCELLED:
            editor_cancel_handoff();
            return true;

        case UI_EVENT_FILE_NEW:
            editor_new_file();
            return true;

        case UI_EVENT_FILE_OPEN: {
            FileDialogParams params = { .title = "Open",
                                        .default_filename = "",
                                        .filter =
                                            "Text Documents (*.txt)|*.txt|All Files (*.*)|*.*",
                                        .save_dialog = false };

            g_modal_depth++;
            char *filename = ui_show_open_dialog(g_editor.main_window, &params);
            g_modal_depth--;
            if (filename) {
                editor_open_file(filename);
                free(filename);
            }
            return true;
        }

        case UI_EVENT_FILE_SAVE:
            editor_save_file();
            return true;

        case UI_EVENT_FILE_SAVE_AS:
            // Pre-fill with the current file (full path so the dialog can open
            // its folder and reuse its name) rather than always "Untitled.txt" -
            // except for a fragment of a binary file, whose original must not
            // be one Enter away
            if (g_editor.file_info.truncated && g_editor.current_file) {
                editor_save_truncated_as();
            } else {
                editor_save_as_dialog(g_editor.current_file ? g_editor.current_file
                                                            : "Untitled.txt");
            }
            return true;

        case UI_EVENT_FILE_DROPPED:
            if (event->data) {
                editor_open_file((const char *) event->data);
            }
            return true;

        case UI_EVENT_AUTO_SAVE:
            // Silent auto-save, only for documents that already have a file
            editor_auto_save();
            return true;

        case UI_EVENT_SESSION_SNAPSHOT:
            editor_snapshot_session();
            return true;

        case UI_EVENT_STARTUP_DEFERRED:
            // Offer to restore anything a previous crash left behind (only
            // when no startup file was requested, so we do not clobber an
            // explicit open). Runs after the first paint - see the platform's
            // startup timer - so launching feels instant.
            if (!g_startup_file) {
                editor_check_session_recovery();
            }
            return true;

        case UI_EVENT_EDIT_UNDO:
            editor_undo();
            return true;

        case UI_EVENT_EDIT_REDO:
            editor_redo();
            return true;

        case UI_EVENT_EDIT_CUT:
            editor_cut();
            return true;

        case UI_EVENT_EDIT_COPY:
            editor_copy();
            return true;

        case UI_EVENT_EDIT_PASTE:
            editor_paste();
            return true;

        case UI_EVENT_EDIT_SELECT_ALL:
            editor_select_all();
            return true;

        case UI_EVENT_EDIT_FIND:
            ui_show_find_dialog(g_editor.main_window);
            return true;

        case UI_EVENT_EDIT_REPLACE:
            ui_show_replace_dialog(g_editor.main_window);
            return true;

        case UI_EVENT_VIEW_TOGGLE_DARK_MODE:
            ui_set_dark_mode(!ui_is_dark_mode());
            settings_set_string("theme", ui_is_dark_mode() ? "dark" : "light");
            return true;

        case UI_EVENT_TEXT_CHANGED:
            g_editor.snapshot_dirty = true; // Every edit, not just the first
            if (!g_editor.is_modified) {
                g_editor.is_modified = true;
                editor_update_title();
                // Snapshot immediately on the FIRST edit rather than waiting
                // for the timer. Otherwise a brand-new window is unprotected
                // for a whole interval - the least protected moment being the
                // one right after you start jotting something down, which is
                // exactly how npad gets used. Costs one write per document;
                // the timer handles everything after this.
                editor_snapshot_session();
            }
            return true;

        case UI_EVENT_WINDOW_CLOSING:
            // This is handled in the platform-specific code
            return true;

        default:
            return false;
    }
}

// Notepad's three-way prompt. Returns true when the pending operation may
// proceed (saved or discarded), false when the user cancelled or the save
// failed.
bool editor_prompt_save_changes(void) {
    if (!g_editor.is_modified)
        return true;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";

    g_modal_depth++;
    SavePromptResult choice = ui_show_save_prompt(g_editor.main_window, filename);
    g_modal_depth--;

    switch (choice) {
        case UI_SAVE_PROMPT_SAVE:
            return editor_save_file();
        case UI_SAVE_PROMPT_DISCARD:
            return true;
        case UI_SAVE_PROMPT_CANCEL:
        default:
            return false;
    }
}

void editor_update_title(void) {
    if (!g_editor.main_window)
        return;

    const char *filename = g_editor.current_file ? g_editor.current_file : "Untitled";
    const char *display_name = path_base_name(filename); // Just the name, no folder

    char title[512];
    snprintf(title, sizeof(title), "%s%.400s - npad", g_editor.is_modified ? "*" : "",
             display_name);

    ui_set_window_title(g_editor.main_window, title);
}
