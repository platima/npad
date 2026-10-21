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

// A process holding a slot's pid that started more than this long after the
// time recorded in the slot id is a recycled pid, not the slot's owner. The
// owner always starts before it makes its id; the margin only absorbs clock
// rounding and small clock corrections.
#define SLOT_START_SLACK_SEC 60

// Above this size the original of a pre-0.36 slot is not re-read to see
// whether it held a NUL; it is assumed to have (see original_is_truncated)
#define TRUNCATION_RECHECK_MAX_BYTES ((size_t) 512 * 1024 * 1024)

// This instance's recovery slot id (unique per process), and the specific
// slot to restore when launched with --recover (NULL for a normal launch),
// with the id of the window that claimed it and handed it over (NULL when
// Windows relaunched this instance with its own old slot).
static char g_session_slot[SESSION_SLOT_ID_MAX] = { 0 };
static char *g_recover_slot = NULL;
static char *g_recover_from = NULL;

// A quit was requested (by the user, Close All, an update's handoff), or the
// desktop session is ending. Checked after the recovery question, which a
// quit can end without an answer, or reach while it is still up.
static bool g_leaving = false;
static bool g_session_ending = false;

// A --recover window that found nothing to restore and closed itself
static bool g_exit_unused = false;

// A claimed slot this window restored but could not move under its own id
// (see editor_restore_claimed). It stays on disk as the crash copy until this
// window's own snapshot replaces it, or the document is saved or discarded.
static char g_held_claim[SESSION_SLOT_ID_MAX] = { 0 };

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

// Apply the auto-save settings to the platform timer
static void editor_apply_auto_save_timer(void) {
    if (g_editor.main_window) {
        ui_set_auto_save_timer(g_editor.main_window,
                               g_editor.auto_save_enabled ? g_editor.auto_save_interval : 0);
    }
}

// Apply the session-recovery settings to the platform timer
static void editor_apply_session_timer(void) {
    if (g_editor.main_window) {
        ui_set_session_timer(g_editor.main_window,
                             g_editor.session_resume_enabled ? g_editor.session_interval : 0);
    }
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

// What a snapshot of the current document records besides its text. The
// truncated flag must travel with it: a restored fragment of a binary file
// that forgot it would let Ctrl+S write the fragment over the original. The
// stamp is the external-change baseline, so a restored document is not
// reported as "changed on disk" just because npad was away.
static void current_session_meta(SessionMeta *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->encoding = g_editor.file_info.encoding;
    meta->line_ending = g_editor.file_info.line_ending;
    meta->truncated = g_editor.file_info.truncated;
    meta->has_truncated = true;
    meta->stamp.size = -1;
    if (g_editor.current_file)
        meta->stamp = g_editor.file_stamp;
}

// Drop a claimed slot held as this window's crash copy (g_held_claim)
static void editor_drop_held_claim(const char *dir) {
    if (g_held_claim[0] && dir) {
        session_discard_claimed(dir, g_held_claim, g_session_slot);
        g_held_claim[0] = '\0';
    }
}

// Write this window's slot: its text and current_session_meta
static bool editor_write_own_slot(const char *dir, const char *content) {
    SessionMeta meta;
    current_session_meta(&meta);
    bool written = session_write_ex(dir, g_session_slot, content, g_editor.current_file, &meta);
    if (written)
        editor_drop_held_claim(dir); // The fresh snapshot supersedes it
    return written;
}

// Write a crash-recovery snapshot of the current unsaved document
static void editor_snapshot_session(void) {
    if (!g_editor.session_resume_enabled || !g_editor.is_modified || !g_editor.main_window)
        return;

    char *dir = editor_session_dir();
    if (!dir)
        return;

    char *content = ui_get_text(g_editor.main_window);
    if (content) {
        editor_write_own_slot(dir, content);
        free(content);
    }
    free(dir);
}

// Remove this instance's recovery snapshot (after a clean save or clean exit,
// or when the document it held was replaced or discarded)
static void editor_clear_session(void) {
    char *dir = editor_session_dir();
    if (dir) {
        session_clear_slot(dir, g_session_slot);
        editor_drop_held_claim(dir);
        free(dir);
    }
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
    if (!g_editor.main_window)
        return true;
    if (!g_editor.is_modified) {
        // Nothing unsaved to carry across. A snapshot still on disk is of
        // text the user has since saved or thrown away; left there, the next
        // launch would bring the discarded draft back as unsaved work.
        editor_clear_session();
        return true;
    }

    char *dir = editor_session_dir();
    if (!dir)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content) {
        free(dir);
        return false;
    }

    bool parked = editor_write_own_slot(dir, content);
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

// Whether a quit, an update's handoff or a session end is under way
static bool editor_is_leaving(void) {
    return g_leaving || g_session_ending;
}

bool editor_exited_unused(void) {
    return g_exit_unused;
}

// A window that has nothing to show and nobody asked for closes again
static void editor_exit_unused(void) {
    g_exit_unused = true;
    g_leaving = true;
    ui_quit();
}

// npad before 0.36 did not record truncation in the slot, so for one of its
// slots the original is looked at again: a NUL before its end means the
// document was only ever the part before it, and Save must not overwrite the
// original with it. A file too large to check is assumed to be truncated -
// the safe direction, where Save asks for a new name instead.
static bool original_is_truncated(const char *path) {
    if (!path || !file_exists(path))
        return false; // No original left to protect
    if (file_get_size(path) > TRUNCATION_RECHECK_MAX_BYTES)
        return true;
    TextFileInfo info;
    memset(&info, 0, sizeof(info));
    char *text = file_read_text_ex(path, &info);
    if (!text)
        return false;
    free(text);
    return info.truncated;
}

typedef enum {
    RESTORE_OK,      // The document is in this window now
    RESTORE_NOTHING, // The slot was gone or empty: nothing to show
    RESTORE_FAILED   // It could not be read; it was kept and the user told
} RestoreResult;

// Restore a slot this instance has claimed into this window.
//
// Nothing is deleted before the text is safely in the window, and the slot is
// then moved under this instance's own id rather than removed: the restored
// document has an on-disk copy from the first moment, instead of waiting for
// the next snapshot tick (or never getting one, for a handoff restored with
// recovery turned off).
static RestoreResult editor_restore_claimed(const char *dir, const char *slot_id) {
    char *content = NULL;
    char *path = NULL;
    SessionMeta meta;
    bool handoff = session_is_handoff(dir, slot_id);
    switch (session_read_claimed(dir, slot_id, g_session_slot, &content, &path, &meta)) {
        case SESSION_READ_OK:
            break;
        case SESSION_READ_GONE:
            return RESTORE_NOTHING;
        case SESSION_READ_EMPTY:
            session_discard_claimed(dir, slot_id, g_session_slot); // Holds nothing
            return RESTORE_NOTHING;
        case SESSION_READ_FAILED:
        default: {
            // Locked by a scanner, out of memory, a permissions problem: the
            // only copy must survive it. Give it back so it is offered again.
            session_release(dir, slot_id, g_session_slot);
            char message[512];
            snprintf(message, sizeof(message),
                     "npad could not read a document it kept for recovery.\n%.200s\n\n"
                     "It has been kept, and npad will offer it again the next time it starts.",
                     file_get_last_error());
            ui_show_message_box(g_editor.main_window, "npad", message, false);
            return RESTORE_FAILED;
        }
    }

    ui_set_text(g_editor.main_window, content);
    free(content);

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = path; // Ownership transferred (may be NULL)

    TextFileInfo info;
    memset(&info, 0, sizeof(info));
    info.encoding = meta.encoding;
    info.line_ending = meta.line_ending;
    info.truncated = meta.has_truncated ? meta.truncated : original_is_truncated(path);
    g_editor.file_info = info;

    // External-change baseline: the original as npad last saw it, so a change
    // made while npad was away is reported and an untouched file is not. A
    // slot from before 0.36 has no stamp; the file as it is now is the best
    // baseline left.
    g_editor.file_stamp.size = -1;
    g_editor.file_stamp.write_time = 0;
    if (path) {
        if (meta.stamp.size >= 0)
            g_editor.file_stamp = meta.stamp;
        else
            file_get_stamp(path, &g_editor.file_stamp);
    }
    g_editor.suppress_change_prompt = false;

    editor_set_modified(true);
    editor_update_status_info();

    if (g_editor.session_resume_enabled || handoff) {
        SessionMeta now;
        current_session_meta(&now);
        if (!session_adopt(dir, slot_id, g_session_slot, g_session_slot, g_editor.current_file,
                           &now)) {
            // Could not be moved: keep holding the claim as the crash copy. If
            // this window dies it is handed back and offered again.
            snprintf(g_held_claim, sizeof(g_held_claim), "%s", slot_id);
        }
    } else {
        // Recovery is off and this was a crash leftover: no snapshots wanted
        session_discard_claimed(dir, slot_id, g_session_slot);
    }
    return RESTORE_OK;
}

// A slot id records the owner's pid and when it started. The slot belongs to a
// live session only while that pid is a running npad that started no later
// than that: after a reboot a recycled pid can belong to a newer npad (on
// Linux even to this very process), and its leftovers would otherwise never
// be offered.
static bool slot_owner_is_running(const char *slot_id, void *ctx) {
    (void) ctx;
    if (strcmp(slot_id, g_session_slot) == 0)
        return true; // This instance
    unsigned long long started_by = 0;
    long pid = 0;
    if (!session_parse_slot_id(slot_id, &started_by, &pid))
        return false;
    long long started = -1;
    if (!ui_pid_is_running(pid, &started))
        return false;
    if (started >= 0 && (unsigned long long) started > started_by + SLOT_START_SLACK_SEC)
        return false; // Same pid, later process: the owner is gone
    return true;
}

// Reopen a slot this instance claimed in a new window of its own. The child is
// told who holds the claim, so it can take it over (and nobody else can).
static void editor_reopen_in_new_window(const char *slot_id, int cascade) {
    char arg[SESSION_SLOT_ID_MAX * 2];
    snprintf(arg, sizeof(arg), "%s:%s", slot_id, g_session_slot);
    ui_launch_recovery_instance(arg, cascade);
}

// Restore a claimed slot here while this window is free, else in a new one
static void editor_place_slot(const char *dir, const char *slot_id, bool *window_free,
                              int *cascade) {
    if (*window_free) {
        if (editor_restore_claimed(dir, slot_id) == RESTORE_OK)
            *window_free = false;
    } else {
        editor_reopen_in_new_window(slot_id, (*cascade)++);
    }
}

// Launched with --recover: restore that one slot, silently. The slot is still
// listed when Windows relaunches npad after a restart (--recover <slot>), or
// claimed by the window that spawned this one (--recover <slot>:<its id>).
static void editor_recover_requested_slot(const char *dir) {
    bool claimed = false;
    if (g_recover_from)
        claimed = session_claim(dir, g_recover_slot, g_recover_from, g_session_slot);
    if (!claimed)
        claimed = session_claim(dir, g_recover_slot, NULL, g_session_slot);

    RestoreResult result = claimed ? editor_restore_claimed(dir, g_recover_slot) : RESTORE_NOTHING;
    if (result != RESTORE_OK && !g_editor.is_modified && !g_editor.current_file) {
        // Another window got there first (a plain launch racing a restart's
        // relaunch), or there was nothing to restore. An empty window that
        // nobody asked for would only puzzle; close it again.
        editor_exit_unused();
    }
}

// On startup, offer to restore work left behind by an unclean exit.
// The check is unconditional on the current enabled state: a snapshot only
// exists on disk if session resume was enabled when it was last written
// (disabling clears it), and the enabled flag may not have been persisted
// if the previous run was killed before a clean exit. When several sessions
// were open, each left its own slot; the extras reopen in new windows.
// Slots owned by still-running npad instances are skipped (they are live
// snapshots, not crashed sessions).
//
// Runs on every launch, including one given a file to open: parked and
// crashed documents are then reopened in windows of their own instead of
// waiting, unseen, for a launch without a file.
static void editor_check_session_recovery(void) {
    if (!g_editor.main_window)
        return;

    char *dir = editor_session_dir();
    if (!dir)
        return;

    if (g_recover_slot) {
        editor_recover_requested_slot(dir);
        free(dir);
        return;
    }

    // Put back whatever an interrupted restore or snapshot left half-done
    session_recover_orphans(dir, g_session_slot, slot_owner_is_running, NULL);

    int listed = 0;
    char **slots = session_list_slots(dir, &listed);

    // Claim every leftover before anything is shown. Two npads starting
    // together would otherwise both offer the same documents, and No in one
    // would delete what the other is restoring. A slot another instance
    // claimed first is simply not ours to offer.
    int count = 0;
    for (int i = 0; i < listed; i++) {
        if (!slot_owner_is_running(slots[i], NULL) &&
            session_claim(dir, slots[i], NULL, g_session_slot)) {
            slots[count++] = slots[i];
        } else {
            free(slots[i]);
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

    // This window takes the first document only when it is free: a file
    // given on the command line, or anything typed already, keeps it. The
    // rest open in new windows, cascaded so they do not stack.
    bool window_free = !g_editor.current_file && !g_editor.is_modified;
    int cascade = 1;
    for (int i = 0; i < handoff; i++) {
        editor_place_slot(dir, slots[i], &window_free, &cascade);
    }

    int crashed = count - handoff;
    if (crashed > 0) {
        UiAnswer answer = UI_ANSWER_DISMISSED;
        if (!editor_is_leaving()) {
            char message[256];
            if (crashed == 1) {
                snprintf(message, sizeof(message),
                         "npad may have closed unexpectedly.\nRestore your unsaved work?");
            } else {
                snprintf(message, sizeof(message),
                         "npad may have closed unexpectedly.\n"
                         "Restore your %d unsaved documents?",
                         crashed);
            }
            answer = ui_ask_question(g_editor.main_window, "npad", message);
            // A quit, an update's handoff or a session end that reached the
            // question ended it, or will close this window straight after
            // whatever was answered. Either way it was no decision about these
            // documents: keep them for the next launch.
            if (editor_is_leaving())
                answer = UI_ANSWER_DISMISSED;
        }

        for (int i = handoff; i < count; i++) {
            switch (answer) {
                case UI_ANSWER_YES:
                    editor_place_slot(dir, slots[i], &window_free, &cascade);
                    break;
                case UI_ANSWER_NO:
                    // Only an explicit No discards them
                    session_discard_claimed(dir, slots[i], g_session_slot);
                    break;
                case UI_ANSWER_DISMISSED:
                default:
                    session_release(dir, slots[i], g_session_slot);
                    break;
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

    // Unique recovery slot id for this instance (time + nonce + pid). Distinct
    // across concurrently running instances so their snapshots never collide.
    session_make_slot_id(g_session_slot, sizeof(g_session_slot));
    g_leaving = false;
    g_session_ending = false;
    g_exit_unused = false;
    g_held_claim[0] = '\0';

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

    editor_set_recover_slot(NULL);
}

bool editor_set_recover_slot(const char *slot) {
    free(g_recover_slot);
    g_recover_slot = NULL;
    free(g_recover_from);
    g_recover_from = NULL;
    if (!slot)
        return true;

    // "<slot>" or "<slot>:<claimer>". Both must be ids npad could have made:
    // the slot id becomes part of file names that are renamed and deleted.
    char slot_id[SESSION_SLOT_ID_MAX];
    char claimer[SESSION_SLOT_ID_MAX] = "";
    const char *colon = strchr(slot, ':');
    size_t slot_len = colon ? (size_t) (colon - slot) : strlen(slot);
    if (slot_len >= sizeof(slot_id) || (colon && strlen(colon + 1) >= sizeof(claimer)))
        return false;
    memcpy(slot_id, slot, slot_len);
    slot_id[slot_len] = '\0';
    if (colon)
        strcpy(claimer, colon + 1);
    if (!session_is_valid_slot_id(slot_id) || (colon && !session_is_valid_slot_id(claimer)))
        return false;

    g_recover_slot = duplicate_string(slot_id);
    if (colon)
        g_recover_from = duplicate_string(claimer);
    return g_recover_slot != NULL;
}

void editor_set_main_window(Window *window) {
    g_editor.main_window = window;
    editor_update_title();
    editor_update_status_info();
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

    editor_set_modified(false);
    editor_update_status_info();
    // The document the snapshot held is gone - saved, or discarded with
    // Don't Save. Left on disk, a later crash or handoff would bring the
    // discarded draft back as unsaved work bound to its old file.
    editor_clear_session();

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

bool editor_open_file(const char *filename) {
    if (!filename)
        return false;

    // Pre-open checks run before the save prompt so declining either one
    // never costs the user a pointless "save changes?" round trip

    // A binary-looking file is usually a mistake: offer the system's own
    // handler for it instead of a wall of control characters
    if (file_looks_binary(filename)) {
        UiOpenChoice choice = ui_prompt_binary_open(g_editor.main_window, filename);
        if (choice == UI_OPEN_CANCEL)
            return false;
        if (choice == UI_OPEN_WITH_DEFAULT) {
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
            if (!ui_show_message_box(g_editor.main_window, "npad", message, true)) {
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
        ui_show_message_box(g_editor.main_window, "npad", error_msg, false);
        return false;
    }

    char *new_path = duplicate_string(filename);
    if (!new_path) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, filename,
                         "Failed to allocate memory for current file path");
        free(content);
        return false;
    }

    apply_default_eol_if_no_breaks(&info, content); // Before content is freed
    if (g_editor.main_window) {
        ui_set_text(g_editor.main_window, content);
    }
    // Needs the text, so before it is freed
    apply_default_eol_if_no_breaks(&info, content);
    free(content);

    // Baseline for external-change detection, and a fresh slate for the
    // per-file "don't ask again" choice
    file_get_stamp(filename, &g_editor.file_stamp);
    g_editor.suppress_change_prompt = false;

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = new_path;
    g_editor.file_info = info;

    settings_add_recent_file(filename);

    editor_set_modified(false);
    editor_update_status_info();
    editor_clear_session(); // The previous document's snapshot, as for New

    return true;
}

// Warn before a user-initiated save that would replace characters the ANSI
// code page cannot represent with '?'. Returns false if the user cancels.
// Classic Notepad behaviour; auto-save never prompts (it skips instead).
static bool editor_confirm_lossy_ansi(const char *content) {
    if (g_editor.file_info.encoding != NPAD_ENC_ANSI || !content || !file_ansi_is_lossy(content)) {
        return true;
    }
    return ui_show_message_box(g_editor.main_window, "npad",
                               "This document contains characters that cannot be saved in "
                               "ANSI encoding.\nThey will be replaced with '?'. Save anyway?",
                               true);
}

// --- External change detection --------------------------------------------
//
// Checked when the window is activated rather than on a timer: that is the
// moment the user would notice anyway, and it costs nothing while npad is not
// in front. Also checked before a save, which is the genuinely dangerous
// moment - silently overwriting someone else's edit.
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
    // reported again on every activation
    if (g_editor.current_file) {
        file_get_stamp(g_editor.current_file, &g_editor.file_stamp);
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
    editor_set_modified(false); // Now identical to the file again
    editor_update_status_info();
    editor_clear_session(); // The unsaved copy this replaced is gone by choice
    return true;
}

bool editor_save_file(void) {
    if (!g_editor.current_file) {
        // No current file, prompt for save as
        FileDialogParams params = { .title = "Save As",
                                    .default_filename = "Untitled.txt",
                                    .filter = "Text Documents (*.txt)|*.txt|All Files (*.*)|*.*",
                                    .save_dialog = true,
                                    .encoding = g_editor.file_info.encoding };

        char *filename = ui_show_save_dialog(g_editor.main_window, &params);
        if (!filename) {
            return false; // User cancelled
        }

        g_editor.file_info.encoding = params.encoding; // From the dialog's picker
        bool result = editor_save_file_as(filename);
        free(filename);
        return result;
    }

    if (!g_editor.main_window)
        return false;

    // npad could not read this file in full - it held a NUL, and npad's text is
    // NUL-terminated throughout, so only the part before it was loaded. Saving
    // would replace the original with that fragment: a 120 KB image becomes
    // nine bytes. Divert to Save As so the original is never the target.
    if (g_editor.file_info.truncated) {
        ui_show_message_box(g_editor.main_window, "npad",
                            "This file contains binary data, so npad could only load the "
                            "part before it.\n\n"
                            "Saving over the original would discard the rest, so choose a "
                            "different file to save this text to.",
                            false);

        FileDialogParams params = { .title = "Save As",
                                    .default_filename = "Untitled.txt",
                                    .filter = "Text Documents (*.txt)|*.txt|All Files (*.*)|*.*",
                                    .save_dialog = true,
                                    .encoding = g_editor.file_info.encoding };
        char *filename = ui_show_save_dialog(g_editor.main_window, &params);
        if (!filename)
            return false; // Cancelled - original still intact, which is the point

        g_editor.file_info.encoding = params.encoding;
        bool saved = editor_save_file_as(filename);
        free(filename);
        return saved;
    }

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    if (!editor_confirm_lossy_ansi(content)) {
        free(content);
        return false; // User declined the lossy ANSI save
    }

    bool success = file_write_text_ex(g_editor.current_file, content, &g_editor.file_info);
    if (success) {
        // npad's saves are atomic (temp + rename), so every write changes the
        // file's identity. Without re-stamping, npad would report its OWN
        // write as an external change the next time the window is activated.
        file_get_stamp(g_editor.current_file, &g_editor.file_stamp);
    }
    free(content);

    if (success) {
        editor_set_modified(false);
        editor_clear_session(); // Saved work no longer needs recovery
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s",
                 g_editor.current_file, file_get_last_error());
        ui_show_message_box(g_editor.main_window, "npad", error_msg, false);
    }

    return success;
}

bool editor_save_file_as(const char *filename) {
    if (!filename || !g_editor.main_window)
        return false;

    char *content = ui_get_text(g_editor.main_window);
    if (!content)
        return false;

    if (!editor_confirm_lossy_ansi(content)) {
        free(content);
        return false; // User declined the lossy ANSI save
    }

    bool success = file_write_text_ex(filename, content, &g_editor.file_info);
    free(content);

    if (success) {
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

        // Whatever was loaded has now been written in full to THIS file, so it
        // is no longer a fragment of something larger and Save may overwrite it
        g_editor.file_info.truncated = false;
        file_get_stamp(new_path, &g_editor.file_stamp);
        g_editor.suppress_change_prompt = false; // A different file, fresh slate

        settings_add_recent_file(filename);
        editor_set_modified(false);
        editor_clear_session(); // Saved work no longer needs recovery
        editor_update_status_info();
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%.150s", filename,
                 file_get_last_error());
        ui_show_message_box(g_editor.main_window, "npad", error_msg, false);
    }

    return success;
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
    } else {
        editor_snapshot_session(); // Protect what is already unsaved, now
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

    bool was_enabled = g_editor.session_resume_enabled;
    g_editor.session_resume_enabled = settings_get_bool("session_resume_enabled", true);
    g_editor.session_interval = settings_get_int("session_interval", DEFAULT_SESSION_INTERVAL_SEC);
    if (g_editor.session_interval < 5) {
        g_editor.session_interval = 5;
    }

    editor_apply_auto_save_timer();
    editor_apply_session_timer();

    // Recovery switched in another window applies here too, snapshot and all:
    // turned off, this window's snapshot goes, exactly as it does in the
    // window where the switch was made; turned on, it is written now
    if (was_enabled && !g_editor.session_resume_enabled) {
        editor_clear_session();
    } else if (!was_enabled && g_editor.session_resume_enabled) {
        editor_snapshot_session();
    }
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
            g_leaving = true;
            ui_quit();
            return true;

        // npad is being closed by something else, so it must come back as it
        // is. No prompt, and pointedly no editor_clear_session() - that call
        // is what would destroy the snapshot we just took.
        case UI_EVENT_QUIT_HANDOFF:
            if (!editor_snapshot_handoff()) {
                return false; // Could not park it - do not exit on a broken promise
            }
            g_leaving = true;
            ui_quit();
            return true;

        // The desktop session is ending (Windows: WM_QUERYENDSESSION); the
        // process may be stopped at any moment after this
        case UI_EVENT_HANDOFF_SNAPSHOT:
            g_session_ending = true;
            return editor_snapshot_handoff();

        case UI_EVENT_HANDOFF_CANCELLED:
            g_session_ending = false;
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

            char *filename = ui_show_open_dialog(g_editor.main_window, &params);
            if (filename) {
                editor_open_file(filename);
                free(filename);
            }
            return true;
        }

        case UI_EVENT_FILE_SAVE:
            editor_save_file();
            return true;

        case UI_EVENT_FILE_SAVE_AS: {
            // Pre-fill with the current file (full path so the dialog can open
            // its folder and reuse its name) rather than always "Untitled.txt"
            FileDialogParams params = {
                .title = "Save As",
                .default_filename = g_editor.current_file ? g_editor.current_file : "Untitled.txt",
                .filter = "Text Documents (*.txt)|*.txt|All Files (*.*)|*.*",
                .save_dialog = true,
                .encoding = g_editor.file_info.encoding
            };

            char *filename = ui_show_save_dialog(g_editor.main_window, &params);
            if (filename) {
                g_editor.file_info.encoding = params.encoding; // From the dialog's picker
                editor_save_file_as(filename);
                free(filename);
            }
            return true;
        }

        case UI_EVENT_FILE_DROPPED:
            if (event->data) {
                editor_open_file((const char *) event->data);
            }
            return true;

        case UI_EVENT_AUTO_SAVE:
            // Silent auto-save: only for documents that already have a file.
            // A lossy ANSI save is skipped rather than prompting from a timer
            // (the user gets the prompt on their next manual save; session
            // snapshots still protect the unsaved content meanwhile).
            if (g_editor.auto_save_enabled && g_editor.is_modified && g_editor.current_file) {
                // A truncated document is skipped outright: editor_save_file
                // diverts it to Save As, and a modal file dialog raised by a
                // timer is exactly what this path avoids. The snapshot still
                // protects the text, and a manual save gets the explanation.
                bool skip = g_editor.file_info.truncated;
                if (!skip && g_editor.file_info.encoding == NPAD_ENC_ANSI && g_editor.main_window) {
                    char *content = ui_get_text(g_editor.main_window);
                    if (content) {
                        skip = file_ansi_is_lossy(content);
                        free(content);
                    }
                }
                if (!skip) {
                    editor_save_file();
                }
            }
            return true;

        case UI_EVENT_SESSION_SNAPSHOT:
            editor_snapshot_session();
            return true;

        case UI_EVENT_STARTUP_DEFERRED:
            // Offer to restore anything a previous crash left behind, and bring
            // back what an update or a restart parked. With a startup file the
            // file keeps this window and recovered documents get windows of
            // their own. Runs after the first paint - see the platform's
            // startup timer - so launching feels instant.
            editor_check_session_recovery();
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

    switch (ui_show_save_prompt(g_editor.main_window, filename)) {
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

    // Extract just the filename from the full path
    const char *display_name = filename;
    const char *last_slash = strrchr(filename, '/');
    const char *last_backslash = strrchr(filename, '\\');
    const char *separator = last_slash;
    if (!separator || (last_backslash && last_backslash > separator)) {
        separator = last_backslash;
    }
    if (separator) {
        display_name = separator + 1;
    }

    char title[512];
    snprintf(title, sizeof(title), "%s%.400s - npad", g_editor.is_modified ? "*" : "",
             display_name);

    ui_set_window_title(g_editor.main_window, title);
}
