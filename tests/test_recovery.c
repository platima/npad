/*
 * npad - Crash recovery and handoff tests, at the editor level
 *
 * Drives src/core/editor.c against a stub UI (tests/stub_ui_recovery.c): a
 * "launch" is editor_init plus the deferred startup event, a "crash" is
 * starting again without a clean exit, and dialogs answer as each test says.
 * Everything lives under a throwaway HOME / XDG_CONFIG_HOME in the working
 * directory, so the real ~/.config/npad is never touched.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* setenv, getcwd, chmod, geteuid under -std=c99 */
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "test_framework.h"
#include "stub_ui_recovery.h"
#include "../src/core/editor.h"
#include "../src/core/file_ops.h"
#include "../src/core/session.h"
#include "../src/core/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32

extern EditorState g_editor;

static char g_home[512];
static char g_files[600];

// Slots left by instances that are gone: their pids are not in the stub's
// table of running processes
#define DEAD_1 "5f000001-7001"
#define DEAD_2 "5f000002-1-7002"
#define DEAD_3 "5f000003-7003"

// --- Helpers ---------------------------------------------------------------------

static void remove_tree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        const struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            chmod(child, 0700);
            remove_tree(child);
        }
        closedir(d);
        rmdir(path);
    } else {
        remove(path);
    }
}

static char *recovery_dir(void) {
    static char dir[1024];
    char *d = editor_session_dir();
    snprintf(dir, sizeof(dir), "%s", d ? d : "");
    free(d);
    return dir;
}

static char *recovery_file(const char *name) {
    static char path[1200];
    snprintf(path, sizeof(path), "%s/%s", recovery_dir(), name);
    return path;
}

static char *test_file(const char *name) {
    static char path[1200];
    snprintf(path, sizeof(path), "%s/%s", g_files, name);
    return path;
}

static int listed(void) {
    int count = 0;
    char **slots = session_list_slots(recovery_dir(), &count);
    session_free_slots(slots, count);
    return count;
}

static bool is_listed(const char *slot) {
    int count = 0;
    char **slots = session_list_slots(recovery_dir(), &count);
    bool found = false;
    for (int i = 0; i < count; i++)
        found = found || strcmp(slots[i], slot) == 0;
    session_free_slots(slots, count);
    return found;
}

static void post(UIEventType type) {
    UIEvent event = { type, stub_window(), NULL };
    editor_handle_event(&event);
}

// The user types: the buffer changes and the platform reports the edit
static void type_text(const char *text) {
    stub_set_text(text);
    post(UI_EVENT_TEXT_CHANGED);
}

// A fresh npad process, after whatever the last one did (a crash included:
// nothing of its on-disk state is cleaned up). recover_arg is --recover's.
static void launch(const char *recover_arg) {
    editor_cleanup();
    stub_reset();
    if (recover_arg)
        editor_set_recover_slot(recover_arg);
    editor_init();
    editor_set_main_window(stub_window());
}

static void begin(void) {
    remove_tree(recovery_dir());
    remove_tree(g_files);
    mkdir(g_files, 0700);
    settings_set_bool("session_resume_enabled", true);
    settings_set_bool("watch_file_changes", false);
    launch(NULL);
}

static void write_file(const char *path, const char *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

static char *read_file(const char *path, size_t *size) {
    void *data = NULL;
    size_t got = 0;
    if (!file_read_binary(path, &data, &got))
        return NULL;
    if (size)
        *size = got;
    return data;
}

// A file with text, a NUL, then more: npad loads only the part before the NUL
static const char g_binary[] = { 'H', 'E', 'A', 'D', '\0', 'T', 'A',
                                 'I', 'L', '-', 'd', 'a',  't', 'a' };

// --- NP-001: truncation survives recovery -------------------------------------------

TEST_CASE(truncated_survives_a_crash) {
    begin();
    const char *bin = test_file("data.bin");
    write_file(bin, g_binary, sizeof(g_binary));

    TEST_ASSERT(editor_open_file(bin), "open (\"Open in npad\" at the binary prompt)");
    TEST_ASSERT(g_editor.file_info.truncated, "loaded truncated");
    type_text("HEAD edited");
    TEST_ASSERT_EQ(1, listed(), "the first edit is snapshotted");

    launch(NULL); // The crash, and the next launch
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.questions, "the crash prompt is shown");
    TEST_ASSERT_STR_EQ(bin, editor_get_current_file(), "restored with its path");
    TEST_ASSERT(g_editor.file_info.truncated, "and still marked truncated");

    TEST_ASSERT(!editor_save_file(), "Ctrl+S does not overwrite the original");
    TEST_ASSERT_EQ(1, g_stub.save_dialogs, "it asks for a new name instead");
    size_t size = 0;
    char *data = read_file(bin, &size);
    TEST_ASSERT(data && size == sizeof(g_binary) && memcmp(data, g_binary, size) == 0,
                "the original is intact");
    free(data);
}

TEST_CASE(truncated_survives_a_handoff) {
    begin();
    const char *bin = test_file("data.bin");
    write_file(bin, g_binary, sizeof(g_binary));
    editor_open_file(bin);
    type_text("HEAD edited");
    post(UI_EVENT_QUIT_HANDOFF); // An update closes npad
    TEST_ASSERT(g_stub.quit_called, "the window goes");

    launch(NULL);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "a handoff restores silently");
    TEST_ASSERT(editor_is_modified(), "restored");
    TEST_ASSERT(g_editor.file_info.truncated, "still truncated after a handoff");
    TEST_ASSERT(!editor_save_file(), "Ctrl+S is diverted");
    size_t size = 0;
    char *data = read_file(bin, &size);
    TEST_ASSERT(data && size == sizeof(g_binary), "the original is intact");
    free(data);
}

TEST_CASE(truncated_is_rechecked_for_a_pre_036_slot) {
    begin();
    const char *bin = test_file("data.bin");
    write_file(bin, g_binary, sizeof(g_binary));
    // A slot as npad 0.35 parked it during the update to this version
    char meta[1400];
    snprintf(meta, sizeof(meta), "0\n%d\n%s", (int) NPAD_EOL_LF, bin);
    session_write(recovery_dir(), DEAD_1, "HEAD edited", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    write_file(recovery_file(DEAD_1 ".meta"), meta, strlen(meta));
    session_mark_handoff(recovery_dir(), DEAD_1);

    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ(bin, editor_get_current_file(), "restored");
    TEST_ASSERT(g_editor.file_info.truncated, "the NUL in the original is found again");
}

// --- NP-029 / NP-240: the encoding's load flags survive recovery -----------------------

// UTF-8 with a BOM and one byte that is not UTF-8: it decodes to a
// replacement character, so a save would not write the 0xFF back
static const char g_lossy[] = "\xEF\xBB\xBF" "ok \xFF here";

TEST_CASE(decode_lossy_survives_a_handoff) {
    begin();
    const char *a = test_file("lossy.txt");
    write_file(a, g_lossy, sizeof(g_lossy) - 1);
    TEST_ASSERT(editor_open_file(a), "open");
    TEST_ASSERT(g_editor.file_info.decode_lossy, "loaded with a byte it could not decode");
    type_text("ok edited");
    post(UI_EVENT_QUIT_HANDOFF);

    launch(NULL);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ(a, editor_get_current_file(), "restored");
    TEST_ASSERT(g_editor.file_info.decode_lossy, "still marked lossy");

    g_stub.message_box_yes = false; // No to "save over the original anyway?"
    TEST_ASSERT(!editor_save_file(), "declining keeps the original");
    size_t size = 0;
    char *data = read_file(a, &size);
    TEST_ASSERT(data && size == sizeof(g_lossy) - 1 && memcmp(data, g_lossy, size) == 0,
                "the undecodable byte is still on disk");
    free(data);

    g_stub.message_box_yes = true;
    TEST_ASSERT(editor_save_file(), "Yes saves");
    TEST_ASSERT(!g_editor.file_info.decode_lossy, "and the saved file decodes cleanly");
}

TEST_CASE(decode_lossy_is_rechecked_for_a_pre_036_slot) {
    begin();
    const char *a = test_file("lossy.txt");
    write_file(a, g_lossy, sizeof(g_lossy) - 1);
    char meta[1400];
    snprintf(meta, sizeof(meta), "%d\n%d\n%s", (int) NPAD_ENC_UTF8_BOM, (int) NPAD_EOL_LF, a);
    session_write(recovery_dir(), DEAD_1, "ok edited", NULL, NPAD_ENC_UTF8_BOM, NPAD_EOL_LF);
    write_file(recovery_file(DEAD_1 ".meta"), meta, strlen(meta));
    session_mark_handoff(recovery_dir(), DEAD_1);

    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ(a, editor_get_current_file(), "restored");
    TEST_ASSERT(g_editor.file_info.decode_lossy, "the bad byte in the original is found again");
    TEST_ASSERT(!g_editor.file_info.truncated, "and nothing else is invented");
}

// --- NP-002 / NP-218: only an explicit No discards ---------------------------------------

static void make_crash_slots(void) {
    session_write(recovery_dir(), DEAD_1, "first lost doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_write(recovery_dir(), DEAD_2, "second lost doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
}

TEST_CASE(dismissed_prompt_keeps_the_slots) {
    begin();
    make_crash_slots();
    g_stub.answer = UI_ANSWER_DISMISSED; // Esc, the close box, a failed dialog
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.questions, "asked once");
    TEST_ASSERT_EQ(2, listed(), "both documents kept for next time");
    TEST_ASSERT(!editor_is_modified(), "nothing restored");

    launch(NULL);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.questions, "offered again at the next launch");
    TEST_ASSERT(editor_is_modified(), "and restored this time");
}

static void quit_for_update(void) {
    post(UI_EVENT_QUIT_HANDOFF);
}

static void session_ends(void) {
    post(UI_EVENT_HANDOFF_SNAPSHOT);
}

TEST_CASE(quit_during_prompt_keeps_the_slots) {
    begin();
    make_crash_slots();
    // Install now in another window: the handoff reaches this one while the
    // question is up, and the user then clicks Yes (GTK keeps the dialog up)
    g_stub.during_question = quit_for_update;
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT(g_stub.quit_called, "the window is going");
    TEST_ASSERT_EQ(2, listed(), "both documents survive");
    TEST_ASSERT(!editor_is_modified(), "nothing restored into a window that is closing");
    TEST_ASSERT_EQ(0, g_stub.launches, "and no windows spawned for it");
}

TEST_CASE(session_end_during_prompt_is_not_a_no) {
    begin();
    make_crash_slots();
    // Windows signing out: WM_QUERYENDSESSION, then the box ends with what
    // looks like an answer
    g_stub.during_question = session_ends;
    g_stub.answer = UI_ANSWER_NO;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(2, listed(), "No during a session end deletes nothing");
}

TEST_CASE(explicit_no_discards) {
    begin();
    make_crash_slots();
    session_mark_handoff(recovery_dir(), DEAD_3); // A stray marker of another slot
    g_stub.answer = UI_ANSWER_NO;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, listed(), "No discards them");
    TEST_ASSERT(!file_exists(recovery_file(DEAD_1 ".txt")), "text removed");
    TEST_ASSERT(!file_exists(recovery_file(DEAD_2 ".txt")), "text removed");
}

// --- NP-007: a failed read keeps the slot ----------------------------------------------

TEST_CASE(unreadable_slot_is_kept_and_reported) {
    begin();
    if (geteuid() == 0) {
        printf("(skipped as root) ");
        return;
    }
    session_write(recovery_dir(), DEAD_1, "the only copy", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    chmod(recovery_file(DEAD_1 ".txt"), 0);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.info_boxes, "the user is told");
    TEST_ASSERT(!editor_is_modified(), "nothing restored");
    chmod(recovery_file(DEAD_1 ".txt"), 0600);
    TEST_ASSERT(is_listed(DEAD_1), "the slot is still there to try again");
    char *text = file_read_text(recovery_file(DEAD_1 ".txt"));
    TEST_ASSERT_STR_EQ("the only copy", text, "with its text");
    free(text);
}

// --- NP-008: Don't Save clears the snapshot ------------------------------------------------

TEST_CASE(dont_save_on_new_clears_the_snapshot) {
    begin();
    const char *a = test_file("a.txt");
    write_file(a, "original", 8);
    editor_open_file(a);
    type_text("SECRET draft");
    TEST_ASSERT(is_listed(editor_session_slot_id()), "snapshotted");

    g_stub.save_prompt = UI_SAVE_PROMPT_DISCARD;
    TEST_ASSERT(editor_new_file(), "New, Don't Save");
    TEST_ASSERT_EQ(0, listed(), "the discarded draft is gone from disk");

    launch(NULL);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "and never comes back");
    TEST_ASSERT(!editor_is_modified(), "nothing restored");
}

TEST_CASE(dont_save_on_open_clears_the_snapshot) {
    begin();
    const char *a = test_file("a.txt");
    const char *b = test_file("b.txt");
    write_file(a, "a", 1);
    write_file(b, "b", 1);
    editor_open_file(a);
    type_text("SECRET draft");
    g_stub.save_prompt = UI_SAVE_PROMPT_DISCARD;
    TEST_ASSERT(editor_open_file(b), "Open, Don't Save");
    TEST_ASSERT_EQ(0, listed(), "the discarded draft is gone from disk");
}

TEST_CASE(handoff_of_a_clean_document_clears_a_stale_snapshot) {
    begin();
    type_text("draft");
    TEST_ASSERT_EQ(1, listed(), "snapshotted");
    editor_set_modified(false); // Back to clean (as after undoing to the save point)
    post(UI_EVENT_QUIT_HANDOFF);
    TEST_ASSERT_EQ(0, listed(), "nothing unsaved to park, and nothing stale left");
}

// --- NP-009: restored work is on disk at once -------------------------------------------

TEST_CASE(restored_document_is_on_disk_immediately) {
    begin();
    session_write(recovery_dir(), DEAD_1, "recovered text", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT(editor_is_modified(), "restored");
    TEST_ASSERT_EQ(1, listed(), "exactly one slot on disk");
    TEST_ASSERT(is_listed(editor_session_slot_id()), "under this window's own id, before any tick");
    char name[SESSION_SLOT_ID_MAX + 8];
    snprintf(name, sizeof(name), "%s.txt", editor_session_slot_id());
    char *text = file_read_text(recovery_file(name));
    TEST_ASSERT_STR_EQ("recovered text", text, "holding the restored text");
    free(text);

    launch(NULL); // The same crash again, straight away
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("recovered text", g_stub.text, "still recoverable");
}

TEST_CASE(handoff_with_recovery_off_keeps_an_on_disk_copy) {
    begin();
    settings_set_bool("session_resume_enabled", false);
    launch(NULL);
    session_write(recovery_dir(), DEAD_1, "parked", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_mark_handoff(recovery_dir(), DEAD_1);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("parked", g_stub.text, "restored");
    TEST_ASSERT(is_listed(editor_session_slot_id()), "a handoff keeps its copy even so");
    TEST_ASSERT(!is_listed(DEAD_1), "the old slot is gone");
}

TEST_CASE(crash_restore_with_recovery_off_writes_nothing) {
    begin();
    settings_set_bool("session_resume_enabled", false);
    launch(NULL);
    session_write(recovery_dir(), DEAD_1, "crashed", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("crashed", g_stub.text, "restored");
    TEST_ASSERT_EQ(0, listed(), "recovery off: no snapshot kept");
}

// --- NP-010: a launch with a file still recovers -------------------------------------------

TEST_CASE(launch_with_a_file_reopens_recovery_in_new_windows) {
    begin();
    const char *a = test_file("a.txt");
    write_file(a, "opened by double-click", 22);
    session_write(recovery_dir(), DEAD_1, "parked", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_mark_handoff(recovery_dir(), DEAD_1);
    session_write(recovery_dir(), DEAD_2, "crashed", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);

    editor_set_startup_file(a);
    editor_open_file(a);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);

    TEST_ASSERT_STR_EQ(a, editor_get_current_file(), "the file keeps this window");
    TEST_ASSERT_STR_EQ("opened by double-click", g_stub.text, "untouched");
    TEST_ASSERT_EQ(1, g_stub.questions, "the crash is still offered");
    TEST_ASSERT_EQ(2, g_stub.launches, "both documents get windows of their own");
    TEST_ASSERT_EQ(0, listed(), "claimed for those windows, so offered to nobody else");

    char expected[SESSION_SLOT_ID_MAX * 2];
    snprintf(expected, sizeof(expected), "%s:%s", DEAD_1, editor_session_slot_id());
    TEST_ASSERT_STR_EQ(expected, g_stub.launch_args[0], "the handoff goes first, with the claim");
    char child_arg[160];
    snprintf(child_arg, sizeof(child_arg), "%s", g_stub.launch_args[1]);

    // The spawned window takes over the claim and restores it
    long parent_pid = (long) getpid();
    launch(child_arg);
    stub_set_live(parent_pid, (long long) time(NULL) - 1); // The parent is still running
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("crashed", g_stub.text, "the child restores its document");
    TEST_ASSERT(!g_stub.quit_called, "and stays open");
}

// --- NP-027: no phantom "changed on disk" ---------------------------------------------------

TEST_CASE(restore_keeps_the_external_change_baseline) {
    begin();
    const char *a = test_file("a.txt");
    write_file(a, "on disk", 7);
    editor_open_file(a);
    type_text("on disk, edited");
    post(UI_EVENT_QUIT_HANDOFF);

    launch(NULL);
    settings_set_bool("watch_file_changes", true);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ(a, editor_get_current_file(), "restored");
    TEST_ASSERT_EQ(EDITOR_FILE_UNCHANGED, editor_check_external_change(),
                   "an untouched file is not reported as changed");

    write_file(a, "changed by something else", 25);
    TEST_ASSERT_EQ(EDITOR_FILE_MODIFIED, editor_check_external_change(), "a real change still is");
}

TEST_CASE(pre_036_slot_gets_a_fresh_baseline) {
    begin();
    const char *a = test_file("a.txt");
    write_file(a, "on disk", 7);
    char meta[1400];
    snprintf(meta, sizeof(meta), "0\n%d\n%s", (int) NPAD_EOL_LF, a);
    session_write(recovery_dir(), DEAD_1, "edited", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    write_file(recovery_file(DEAD_1 ".meta"), meta, strlen(meta));
    session_mark_handoff(recovery_dir(), DEAD_1);

    settings_set_bool("watch_file_changes", true);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ(a, editor_get_current_file(), "restored");
    TEST_ASSERT_EQ(EDITOR_FILE_UNCHANGED, editor_check_external_change(), "no phantom prompt");
}

// --- NP-038: recovery switched off in another window ------------------------------------------

TEST_CASE(recovery_off_elsewhere_clears_this_snapshot) {
    begin();
    type_text("draft");
    TEST_ASSERT_EQ(1, listed(), "snapshotted");
    settings_set_bool("session_resume_enabled", false); // Saved by the other window
    editor_reload_prefs();                              // ...and broadcast here
    TEST_ASSERT_EQ(0, listed(), "this window's snapshot goes too");
    settings_set_bool("session_resume_enabled", true);
    editor_reload_prefs();
    TEST_ASSERT_EQ(1, listed(), "switched back on, it is written again at once");
}

// --- NP-039: interrupted takes are offered again ------------------------------------------------

TEST_CASE(interrupted_old_take_is_offered_again) {
    begin();
    session_write(recovery_dir(), DEAD_3, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_clear_slot(recovery_dir(), DEAD_3);
    write_file(recovery_file(DEAD_1 ".meta.taking"), "0\n1\n", 4);
    write_file(recovery_file(DEAD_1 ".txt.taking"), "half-taken", 10);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.questions, "offered");
    TEST_ASSERT_STR_EQ("half-taken", g_stub.text, "and restored");
}

TEST_CASE(claim_of_a_crashed_window_is_offered_again) {
    begin();
    session_write(recovery_dir(), DEAD_1, "claimed then crashed", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    TEST_ASSERT(session_claim(recovery_dir(), DEAD_1, NULL, DEAD_2),
                "a window claims it, then dies");
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("claimed then crashed", g_stub.text, "it comes back");
}

// --- NP-040 / NP-114: --recover ----------------------------------------------------------------

TEST_CASE(recover_window_with_nothing_to_restore_closes) {
    begin();
    launch(DEAD_1); // Restart Manager relaunch; a plain launch restored it first
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT(g_stub.quit_called, "the empty window closes itself");
    TEST_ASSERT(editor_exited_unused(), "without saving window state or settings");
}

TEST_CASE(recover_restores_its_own_slot) {
    begin();
    session_write(recovery_dir(), DEAD_1, "after restart", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_mark_handoff(recovery_dir(), DEAD_1);
    launch(DEAD_1);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_STR_EQ("after restart", g_stub.text, "restored");
    TEST_ASSERT(!g_stub.quit_called, "stays open");
    TEST_ASSERT(!editor_exited_unused(), "a used window");
}

TEST_CASE(recover_refuses_crafted_ids) {
    begin();
    TEST_ASSERT(!editor_set_recover_slot("../../home/u/notes"), "traversal");
    TEST_ASSERT(!editor_set_recover_slot("5f000001-7001:../x"), "a crafted claimer");
    TEST_ASSERT(!editor_set_recover_slot("5f000001-7001/x"), "slash");
    TEST_ASSERT(!editor_set_recover_slot(""), "empty");
    TEST_ASSERT(editor_set_recover_slot(DEAD_1), "a real id");
    TEST_ASSERT(editor_set_recover_slot(DEAD_1 ":" DEAD_2), "a real id and claimer");
    editor_set_recover_slot(NULL);
}

// --- NP-041: recycled pids --------------------------------------------------------------------

TEST_CASE(recycled_pid_does_not_hide_a_leftover) {
    begin();
    long long slot_time = 0x5f000001LL;
    // After a reboot pid 0x7001 belongs to another npad that started later
    stub_set_live(0x7001, slot_time + 3600);
    session_write(recovery_dir(), DEAD_1, "hidden before", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.questions, "the leftover is offered");
    TEST_ASSERT_STR_EQ("hidden before", g_stub.text, "and restored");
}

TEST_CASE(live_owner_is_left_alone) {
    begin();
    long long slot_time = 0x5f000001LL;
    stub_set_live(0x7001, slot_time - 2); // The owner itself, still running
    session_write(recovery_dir(), DEAD_1, "live snapshot", NULL, NPAD_ENC_UTF8, NPAD_EOL_LF);
    g_stub.answer = UI_ANSWER_NO;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "a running window's snapshot is not offered");
    TEST_ASSERT(is_listed(DEAD_1), "nor touched");

    launch(NULL);
    stub_set_live(0x7001, -1); // Start time unknown: trust the pid
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "still left alone");
}

// --- NP-219: two launches at once ------------------------------------------------------------

TEST_CASE(second_launch_does_not_see_claimed_slots) {
    begin();
    make_crash_slots();
    // Launch A answers Yes: one document here, one handed to a new window
    g_stub.answer = UI_ANSWER_YES;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(1, g_stub.launches, "one window spawned");
    char child_arg[160];
    snprintf(child_arg, sizeof(child_arg), "%s", g_stub.launch_args[0]);
    long pid = (long) getpid();

    // Launch B starts before that child has taken its slot, and answers No
    launch(NULL);
    stub_set_live(pid, (long long) time(NULL) - 1); // A is running
    g_stub.answer = UI_ANSWER_NO;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "B has nothing to offer: A claimed both");

    // A's child still finds its document
    launch(child_arg);
    stub_set_live(pid, (long long) time(NULL) - 1);
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT(editor_is_modified(), "the child restores it");
    TEST_ASSERT(!g_stub.quit_called, "no blank window");
}

TEST_CASE(slot_claimed_by_a_running_window_is_untouched) {
    begin();
    session_write(recovery_dir(), DEAD_1, "being restored elsewhere", NULL, NPAD_ENC_UTF8,
                  NPAD_EOL_LF);
    session_claim(recovery_dir(), DEAD_1, NULL, "6a000001-1-501");
    stub_set_live(0x501, 0x6a000001LL - 1);
    g_stub.answer = UI_ANSWER_NO;
    post(UI_EVENT_STARTUP_DEFERRED);
    TEST_ASSERT_EQ(0, g_stub.questions, "not offered here");
    TEST_ASSERT(file_exists(recovery_file(DEAD_1 ".txt")), "and not deleted");
}

// --- NP-110: private files on Linux --------------------------------------------------------------

TEST_CASE(config_and_settings_are_private) {
    begin();
    settings_save();
    char config[1024];
    snprintf(config, sizeof(config), "%s/xdg/npad", g_home);
    struct stat st;
    TEST_ASSERT(stat(config, &st) == 0, "config dir exists");
    TEST_ASSERT_EQ(0700, (int) (st.st_mode & 0777), "config directory is 0700");
    char settings[1100];
    snprintf(settings, sizeof(settings), "%s/settings.json", config);
    TEST_ASSERT(stat(settings, &st) == 0, "settings.json exists");
    TEST_ASSERT_EQ(0600, (int) (st.st_mode & 0777), "settings.json is 0600");
}

int main(void) {
    TEST_INIT();

    char cwd[400];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("cannot read the working directory\n");
        return 1;
    }
    snprintf(g_home, sizeof(g_home), "%s/test_recovery_home", cwd);
    snprintf(g_files, sizeof(g_files), "%s/files", g_home);
    remove_tree(g_home);
    mkdir(g_home, 0700);
    char xdg[600];
    snprintf(xdg, sizeof(xdg), "%s/xdg", g_home);
    setenv("HOME", g_home, 1);
    setenv("XDG_CONFIG_HOME", xdg, 1);
    if (!settings_init()) {
        printf("settings_init failed\n");
        return 1;
    }

    RUN_TEST(truncated_survives_a_crash);
    RUN_TEST(truncated_survives_a_handoff);
    RUN_TEST(truncated_is_rechecked_for_a_pre_036_slot);
    RUN_TEST(decode_lossy_survives_a_handoff);
    RUN_TEST(decode_lossy_is_rechecked_for_a_pre_036_slot);
    RUN_TEST(dismissed_prompt_keeps_the_slots);
    RUN_TEST(quit_during_prompt_keeps_the_slots);
    RUN_TEST(session_end_during_prompt_is_not_a_no);
    RUN_TEST(explicit_no_discards);
    RUN_TEST(unreadable_slot_is_kept_and_reported);
    RUN_TEST(dont_save_on_new_clears_the_snapshot);
    RUN_TEST(dont_save_on_open_clears_the_snapshot);
    RUN_TEST(handoff_of_a_clean_document_clears_a_stale_snapshot);
    RUN_TEST(restored_document_is_on_disk_immediately);
    RUN_TEST(handoff_with_recovery_off_keeps_an_on_disk_copy);
    RUN_TEST(crash_restore_with_recovery_off_writes_nothing);
    RUN_TEST(launch_with_a_file_reopens_recovery_in_new_windows);
    RUN_TEST(restore_keeps_the_external_change_baseline);
    RUN_TEST(pre_036_slot_gets_a_fresh_baseline);
    RUN_TEST(recovery_off_elsewhere_clears_this_snapshot);
    RUN_TEST(interrupted_old_take_is_offered_again);
    RUN_TEST(claim_of_a_crashed_window_is_offered_again);
    RUN_TEST(recover_window_with_nothing_to_restore_closes);
    RUN_TEST(recover_restores_its_own_slot);
    RUN_TEST(recover_refuses_crafted_ids);
    RUN_TEST(recycled_pid_does_not_hide_a_leftover);
    RUN_TEST(live_owner_is_left_alone);
    RUN_TEST(second_launch_does_not_see_claimed_slots);
    RUN_TEST(slot_claimed_by_a_running_window_is_untouched);
    RUN_TEST(config_and_settings_are_private);

    editor_cleanup();
    settings_cleanup();
    remove_tree(g_home);

    TEST_SUMMARY();
    return 0;
}

#else // _WIN32

int main(void) {
    printf("test_recovery: POSIX only (it moves HOME/XDG_CONFIG_HOME); skipped\n");
    return 0;
}

#endif
