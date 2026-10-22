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

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
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

// A process holding a slot's pid that started more than this long after the
// time recorded in the slot id is a recycled pid, not the slot's owner. The
// owner always starts before it makes its id; the margin only absorbs clock
// rounding and small clock corrections.
#define SLOT_START_SLACK_SEC 60

// Above this size the original of a pre-0.36 slot is not re-read to see
// whether it held a NUL; it is assumed to have (see probe_original)
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

// The edit control could not hold the loaded text exactly (Win32 RichEdit
// turns U+2028/2029 into line breaks and U+FFFD, BEL and some noncharacters
// into spaces; see ui_text_altered_by_load). What it shows is then not the
// file, so like a truncated load it must never be saved over the file without
// a word: Save diverts to Save As and auto-save skips it. The mark travels
// with the recovery snapshot, so a crash or handoff restore keeps it.
static bool g_text_altered = false;

// Shown when ui_set_text could not load a file at all
#define EDITOR_SET_TEXT_FAILED                                                                     \
    "The file is too large to show, or there is not enough memory to open it."

// Shown when ui_get_text could not read the document back for a save
#define EDITOR_GET_TEXT_FAILED                                                                     \
    "npad could not read the document's text (it may be too large, or memory is low)."

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
    meta->decode_lossy = g_editor.file_info.decode_lossy;
    meta->utf16_no_bom = g_editor.file_info.utf16_no_bom;
    meta->altered = g_text_altered;
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
        if (editor_write_own_slot(dir, content))
            g_editor.snapshot_dirty = false;
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

// npad before 0.36 recorded none of the original's load flags in the slot,
// so for one of its slots the original is looked at again. A NUL before its
// end means the document was only ever the part before it (truncated), and
// bytes it could not decode mean a save would not reproduce them
// (decode_lossy): either way Save must not overwrite the original without a
// word. A BOM-less UTF-16 file keeps that form only while the document is
// still in that encoding. A file too large to check is assumed to be
// truncated - the safe direction, where Save asks for a new name instead. A
// file that is gone or unreadable has no bytes to lose.
static void probe_original(const char *path, TextFileInfo *info) {
    if (!path || !file_exists(path))
        return; // No original left to protect
    if (file_get_size(path) > TRUNCATION_RECHECK_MAX_BYTES) {
        info->truncated = true;
        return;
    }
    TextFileInfo probe;
    memset(&probe, 0, sizeof(probe));
    char *text = file_read_text_ex(path, &probe);
    if (!text)
        return;
    free(text);
    info->truncated = probe.truncated;
    info->decode_lossy = probe.decode_lossy;
    info->utf16_no_bom = probe.encoding == info->encoding && probe.utf16_no_bom;
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
            editor_ask(message, false);
            return RESTORE_FAILED;
        }
    }

    if (!ui_set_text(g_editor.main_window, content)) {
        // The window could not take the text (too large, or out of memory)
        // and still shows what it had. Nothing has been deleted: give the
        // claim back, so the work is offered again rather than lost.
        free(content);
        free(path);
        session_release(dir, slot_id, g_session_slot);
        char message[512];
        snprintf(message, sizeof(message),
                 "npad could not show a document it kept for recovery.\n%s\n\n"
                 "It has been kept, and npad will offer it again the next time it starts.",
                 EDITOR_SET_TEXT_FAILED);
        editor_ask(message, false);
        return RESTORE_FAILED;
    }
    // The snapshot already holds the control's substitutes, so this load is
    // exact; the mark from the original load is what keeps Save off the file
    g_text_altered = meta.altered || ui_text_altered_by_load(g_editor.main_window);
    free(content);

    if (g_editor.current_file) {
        free(g_editor.current_file);
    }
    g_editor.current_file = path; // Ownership transferred (may be NULL)

    TextFileInfo info;
    memset(&info, 0, sizeof(info));
    info.encoding = meta.encoding;
    info.line_ending = meta.line_ending;
    if (meta.has_truncated) {
        // Written by this version, which records every load flag
        info.truncated = meta.truncated;
        info.decode_lossy = meta.decode_lossy;
        info.utf16_no_bom = meta.utf16_no_bom;
    } else {
        probe_original(path, &info);
    }
    g_editor.file_info = info;

    // External-change baseline: the original as npad last saw it, so a change
    // made while npad was away is reported and an untouched file is not. A
    // slot from before 0.36 has no stamp; the file as it is now is the best
    // baseline left.
    memset(&g_editor.file_stamp, 0, sizeof(g_editor.file_stamp));
    g_editor.file_stamp.size = -1;
    if (path) {
        if (meta.stamp.size >= 0)
            g_editor.file_stamp = meta.stamp;
        else
            file_get_stamp(path, &g_editor.file_stamp);
    }
    g_editor.suppress_change_prompt = false;
    g_editor.auto_save_held = false;

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
            g_modal_depth++;
            answer = ui_ask_question(g_editor.main_window, "npad", message);
            g_modal_depth--;
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
    // A new document is nobody's fragment, and is written in standard form
    info->truncated = false;
    info->decode_lossy = false;
    info->utf16_no_bom = false;
}

// The user chose an encoding (status bar, menu or the Save As picker). A
// deliberate choice gets that encoding's standard form, so a BOM-less UTF-16
// file converted to another encoding and back gains a BOM, as in notepad.exe;
// keeping the same encoding keeps the file's own form.
static void editor_choose_encoding(TextEncoding encoding) {
    if (g_editor.file_info.encoding != encoding)
        g_editor.file_info.utf16_no_bom = false;
    g_editor.file_info.encoding = encoding;
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

    // Unique recovery slot id for this instance (time + nonce + pid). Distinct
    // across concurrently running instances so their snapshots never collide.
    session_make_slot_id(g_session_slot, sizeof(g_session_slot));
    g_leaving = false;
    g_session_ending = false;
    g_exit_unused = false;
    g_held_claim[0] = '\0';

    load_default_file_info(&g_editor.file_info);

    // An in-place save's safety copy of the original must survive the power
    // cut it exists for, so it goes beside the recovery snapshots, not /tmp
    char *backup_dir = editor_session_dir();
    file_ops_set_backup_dir(backup_dir);
    free(backup_dir);

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
    g_text_altered = false;

    if (g_editor.current_file) {
        free(g_editor.current_file);
        g_editor.current_file = NULL;
    }

    load_default_file_info(&g_editor.file_info);
    memset(&g_editor.file_stamp, 0, sizeof(g_editor.file_stamp));
    g_editor.file_stamp.size = -1;
    g_editor.auto_save_held = false;
    g_editor.suppress_change_prompt = false;

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
    bool altered = false;
    if (g_editor.main_window) {
        if (!ui_set_text(g_editor.main_window, content)) {
            // The window still shows the previous document: binding this
            // path to it would let Save write that text over this file
            free(content);
            free(new_path);
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Could not open file: %.300s\n%s", filename,
                     EDITOR_SET_TEXT_FAILED);
            editor_ask(error_msg, false);
            return false;
        }
        altered = ui_text_altered_by_load(g_editor.main_window);
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
    g_text_altered = altered;

    settings_add_recent_file(filename);

    editor_set_modified(false);
    editor_update_status_info();
    editor_clear_session(); // The previous document's snapshot, as for New

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

// Ask before saving over a file npad could not decode exactly (see
// TextFileInfo.decode_lossy): its invalid bytes are replacement characters in
// the editor, so the save would change them. Saving to a different file needs
// no question - the original is not touched. Returns false if the user
// declines. The encoding is not named: decode_lossy outlives an encoding
// change, and the bytes were invalid in the one the file was read in, which
// need not be the one the document has now.
static bool editor_confirm_lossy_decode(void) {
    if (!g_editor.file_info.decode_lossy)
        return true;
    return editor_ask("Some bytes in this file could not be read, so npad cannot save them back "
                      "exactly as they were.\n\nSave over the original anyway?",
                      true);
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

    if (!ui_set_text(g_editor.main_window, content)) {
        // Nothing changed: the window keeps its text, flags and encoding
        free(content);
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not reload file: %.300s\n%s",
                 g_editor.current_file, EDITOR_SET_TEXT_FAILED);
        editor_ask(error_msg, false);
        return false;
    }
    g_text_altered = ui_text_altered_by_load(g_editor.main_window);
    free(content);

    g_editor.file_info = info;
    file_get_stamp(g_editor.current_file, &g_editor.file_stamp);
    g_editor.auto_save_held = false; // The buffer is the disk's version now
    editor_set_modified(false);      // Now identical to the file again
    editor_update_status_info();
    editor_clear_session(); // The unsaved copy this replaced is gone by choice
    return true;
}

// A save could not read the text back from the control. Said out loud: the
// platform only logs it, and a Save that silently does nothing (or a close
// prompt's Save that just leaves the window open) gives no hint why. Auto-save
// never gets here: it skips an unreadable document silently.
static void editor_report_get_text_failed(const char *path) {
    char error_msg[512];
    snprintf(error_msg, sizeof(error_msg), "Could not save file: %.300s\n%s", path ? path : "",
             EDITOR_GET_TEXT_FAILED);
    editor_ask(error_msg, false);
}

// --- Saving ---------------------------------------------------------------

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
    // Otherwise by identity: a hard link, an 8.3 name, a mapped drive
    return same || file_same(a, b);
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
    // The file now holds exactly the text shown, so it decodes cleanly
    g_editor.file_info.decode_lossy = false;
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
    // The same goes for text the edit control could not hold exactly: saving
    // would write its substitutes over the characters still in the file.
    if (g_text_altered) {
        editor_ask("This file contains characters npad cannot show exactly (such as Unicode "
                   "line separators or U+FFFD), so it displays substitutes for them.\n\n"
                   "Saving over the original would replace those characters, so choose a "
                   "different file to save this text to.",
                   false);
        return editor_save_as_dialog("Untitled.txt"); // Cancelled: original intact
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
        bool replace = editor_ask(message, true);
        // The user has now answered for this change, so adopt the disk version
        // as the baseline before anything else can open. Closing the box
        // re-activates the main window, and both platforms run the activation
        // check from inside the next modal loop (the Save As dialog, an error
        // box), which would otherwise stack the "changed on disk" question -
        // with its Reload that discards these edits - on top of this save.
        // This also holds auto-save, so a cancelled Save As writes nothing;
        // a successful write re-stamps and releases it.
        editor_accept_external_change();
        if (!replace)
            return editor_save_as_dialog(g_editor.current_file);
    }

    if (!editor_confirm_lossy_decode())
        return false; // Declined - the original keeps its bytes

    char *content = ui_get_text(g_editor.main_window);
    if (!content) {
        editor_report_get_text_failed(g_editor.current_file);
        return false;
    }

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
    // Saving a fragment of a binary file is a Save As, which is a dialog, and
    // so is saving text the edit control could not hold exactly. A file npad
    // could not decode exactly needs the question a manual save asks before
    // its replacement characters go over the original bytes.
    if (g_editor.file_info.truncated || g_text_altered || g_editor.file_info.decode_lossy)
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
    bool same_file = paths_are_same_file(filename, g_editor.current_file);
    if (g_editor.file_info.truncated && same_file) {
        editor_ask("This is the file npad could only partly load, because it contains "
                   "binary data.\n\n"
                   "Saving over it would discard everything after that point, so choose a "
                   "different file to save this text to.",
                   false);
        return false;
    }
    // Save As onto the file that is already open overwrites the original
    // exactly as Save does, so it gets Save's question about bytes npad could
    // not decode. The OS "replace existing file?" prompt says nothing of them.
    if (same_file && !editor_confirm_lossy_decode())
        return false; // Declined - the original keeps its bytes

    char *content = ui_get_text(g_editor.main_window);
    if (!content) {
        editor_report_get_text_failed(filename);
        return false;
    }

    if (!editor_confirm_lossy_ansi(content, encoding)) {
        free(content);
        return false; // User declined the lossy ANSI save
    }

    // Whatever was loaded is about to be written in full to THIS file, so it
    // is no longer a fragment of something larger and Save may overwrite it
    TextFileInfo info = g_editor.file_info;
    if (info.encoding != encoding)
        info.utf16_no_bom = false; // A chosen encoding is written in its standard form
    info.encoding = encoding;
    info.truncated = false;
    info.decode_lossy = false; // Likewise: THIS file will hold the text as shown

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
    g_text_altered = false; // THIS file now holds exactly what is shown
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
    editor_choose_encoding(encoding);
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
        if (g_editor.is_modified)
            g_editor.snapshot_dirty = true; // Turning it off dropped the old snapshot
        editor_snapshot_session();          // Protect what is already unsaved, now
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

    // Each apply re-arms only a timer whose interval actually changed, so the
    // broadcast every other instance sends does not keep restarting them
    editor_apply_auto_save_timer();
    editor_apply_session_timer();

    // Recovery switched in another window applies here too, snapshot and all:
    // turned off, this window's snapshot goes, exactly as it does in the
    // window where the switch was made; turned on, it is written now
    if (was_enabled && !g_editor.session_resume_enabled) {
        editor_clear_session();
    } else if (!was_enabled && g_editor.session_resume_enabled) {
        if (g_editor.is_modified)
            g_editor.snapshot_dirty = true; // Switched back on elsewhere: protect this one too
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

// --- Inserting a file (Ctrl+drop) ------------------------------------------
//
// Ctrl+drop used to read any file straight into the document: a 1 GB log froze
// the window, a PNG pasted the bytes before its first NUL, and on Windows a
// locked file did nothing at all. It now asks the same questions Open does,
// with the same large-file threshold.

// The last component of a path, for prompts (Windows or POSIX separators)
static const char *insert_display_name(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name[0] ? name : path;
}

char *editor_read_file_for_insert(const char *filename) {
    if (!filename || !filename[0])
        return NULL;
    const char *name = insert_display_name(filename);
    char message[640];

    // Binary first, as Open does. There is no "open with the default app"
    // choice here: the user asked for this file's text in this document.
    bool confirmed_binary = false;
    if (file_looks_binary(filename)) {
        snprintf(message, sizeof(message),
                 "\"%.300s\" does not look like a text file.\n\n"
                 "Inserting it would add unreadable characters, and npad can only insert "
                 "the part before any binary data.\n\nInsert it anyway?",
                 name);
        if (!ui_show_message_box(g_editor.main_window, "npad", message, true))
            return NULL;
        confirmed_binary = true;
    }

    // The large-file threshold Open uses (0 turns the warning off)
    int warn_mb = settings_get_int("large_file_warning_mb", editor_default_large_file_mb());
    if (warn_mb > 0) {
        size_t size = file_get_size(filename);
        if (size > (size_t) warn_mb * 1024 * 1024) {
            snprintf(message, sizeof(message),
                     "\"%.300s\" is %.1f MB and may take a while to insert.\n"
                     "Do you want to continue?",
                     name, size / (1024.0 * 1024.0));
            if (!ui_show_message_box(g_editor.main_window, "npad", message, true))
                return NULL;
        }
    }

    TextFileInfo info;
    char *content = file_read_text_ex(filename, &info);
    if (!content) {
        snprintf(message, sizeof(message), "Could not insert file: %.300s\n%.150s", filename,
                 file_get_last_error());
        ui_show_message_box(g_editor.main_window, "npad", message, false);
        return NULL;
    }

    // A NUL cut the text short. A binary-looking file was already confirmed on
    // exactly that understanding; any other file is asked about now, since
    // inserting a fragment as if it were the whole file would mislead.
    if (info.truncated && !confirmed_binary) {
        snprintf(message, sizeof(message),
                 "\"%.300s\" contains binary data, so npad can only insert the part "
                 "before it.\n\nInsert that part?",
                 name);
        if (!ui_show_message_box(g_editor.main_window, "npad", message, true)) {
            free(content);
            return NULL;
        }
    }
    return content;
}
