/*
 * npad - Editor core tests
 * Runs src/core/editor.c - open, save, Save As, auto-save, external changes,
 * timers and the recovery snapshot cadence - against the stub UI layer in
 * tests/stub_ui_editor.c, with real files in a scratch directory under the
 * current one. Settings and recovery snapshots live there too
 * (settings_set_file_path), so the user's own are never touched.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* link, mkdir, rmdir under -std=c99 */
#include <sys/stat.h>
#include <unistd.h>
#else
#include <direct.h>
#endif

#include "test_framework.h"
#include "stub_ui_editor.h"
#include "../src/core/editor.h"
#include "../src/core/file_ops.h"
#include "../src/core/session.h"
#include "../src/core/settings.h"
#include "../src/core/thread_safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DIR "test_editor_dir"
#define P(name) TEST_DIR "/" name

extern EditorState g_editor; // White-box access for the failure cases

// Every file a test may create, removed before and after the run
static const char *const k_files[] = {
    P("a.txt"),         P("ansi.txt"),     P("utf16.txt"),   P("blob.dat"),
    P("blob (text).txt"), P("blob_link.dat"), P("runme"),     P("renamed.dat"),
    P("plain.dat"),     P("tool.exe"),     P("pic.png"),     P("log.txt"),
    P("new.txt"),       P("other.txt"),    P("snap.txt"),    P("settings.json"),
};

static void make_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void remove_dir(const char *path) {
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void remove_files(void) {
    for (size_t i = 0; i < sizeof(k_files) / sizeof(k_files[0]); i++)
        remove(k_files[i]);
}

static char *dup_text(const char *text) {
    char *copy = malloc(strlen(text) + 1);
    if (copy)
        strcpy(copy, text);
    return copy;
}

static void write_bytes(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, len, f);
    fclose(f);
}

static void write_text(const char *path, const char *text) {
    write_bytes(path, text, strlen(text));
}

// The file's bytes, NUL-terminated (caller frees); NULL when it is missing
static char *read_disk(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = malloc(1 << 16);
    size_t got = buf ? fread(buf, 1, (1 << 16) - 1, f) : 0;
    fclose(f);
    if (buf)
        buf[got] = '\0';
    if (len)
        *len = got;
    return buf;
}

static bool disk_equals(const char *path, const char *expected) {
    char *bytes = read_disk(path, NULL);
    bool same = bytes && strcmp(bytes, expected) == 0;
    free(bytes);
    return same;
}

static long disk_size(const char *path) {
    size_t len = 0;
    char *bytes = read_disk(path, &len);
    if (!bytes)
        return -1;
    free(bytes);
    return (long) len;
}

// A fresh editor with default preferences, attached to the stub window
static void setup(void) {
    editor_cleanup();
    stub_reset();
    settings_clear_all();
    editor_init();
    editor_set_main_window(stub_window());
}

// The content of a recovery slot, removing it (session_take); NULL if none
static char *take_slot(const char *dir, const char *slot) {
    char *path = NULL;
    TextEncoding encoding;
    LineEnding line_ending;
    char *content = session_take(dir, slot, &path, &encoding, &line_ending);
    free(path);
    return content;
}

static void tick_auto_save(void) {
    stub_post(UI_EVENT_AUTO_SAVE, NULL);
}

TEST_CASE(save_writes_and_restamps) {
    setup();
    settings_set_bool("watch_file_changes", true);
    write_text(P("a.txt"), "one\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    stub_type("two\n");
    TEST_ASSERT(editor_is_modified(), "typing marks modified");
    TEST_ASSERT(editor_save_file(), "saves");
    TEST_ASSERT(disk_equals(P("a.txt"), "two\n"), "the edit is on disk");
    TEST_ASSERT(!editor_is_modified(), "unmodified after saving");
    TEST_ASSERT_EQ(0, g_stub.message_boxes, "no questions for an ordinary save");
    TEST_ASSERT_EQ(EDITOR_FILE_UNCHANGED, editor_check_external_change(),
                   "npad's own write is not an external change");
}

// What the activation check reports from inside the Save As dialog
static EditorExternalChange g_check_in_dialog;
static void check_external_in_dialog(void) {
    g_check_in_dialog = editor_check_external_change();
}

// NP-004: Ctrl+S used to overwrite another program's version without a word
TEST_CASE(manual_save_asks_before_overwriting_external_change) {
    setup();
    settings_set_bool("watch_file_changes", true);
    editor_enable_auto_save(true);
    write_text(P("a.txt"), "mine\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    stub_type("mine, edited\n");
    write_text(P("a.txt"), "THEIRS, written by another program\n");

    // No, then cancel the Save As it leads to: nothing may be written - not
    // even by an auto-save tick arriving while the question is open
    // The activation check that closing the question posts runs inside the
    // Save As dialog's modal loop; it must not ask about the same change again
    g_stub.during_message_box = tick_auto_save;
    g_stub.during_save_dialog = check_external_in_dialog;
    g_check_in_dialog = EDITOR_FILE_MODIFIED;
    stub_answer(false);
    g_stub.save_dialog_result = NULL;
    TEST_ASSERT(!editor_save_file(), "declined: not saved");
    TEST_ASSERT(disk_equals(P("a.txt"), "THEIRS, written by another program\n"),
                "their version is intact");
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "asked once");
    TEST_ASSERT_EQ(1, g_stub.save_dialogs, "No offers Save As");
    TEST_ASSERT_STR_EQ(P("a.txt"), g_stub.save_dialog_default, "Save As starts at the file");
    TEST_ASSERT_EQ(EDITOR_FILE_UNCHANGED, g_check_in_dialog,
                   "answered once: the check inside Save As stays quiet");
    TEST_ASSERT(editor_is_modified(), "still unsaved");
    tick_auto_save();
    TEST_ASSERT(disk_equals(P("a.txt"), "THEIRS, written by another program\n"),
                "a cancelled Save As leaves auto-save held");

    // Yes replaces it (another change, so another question)
    g_stub.during_message_box = NULL;
    g_stub.during_save_dialog = NULL;
    write_text(P("a.txt"), "THEIRS again\n");
    int boxes = g_stub.message_boxes;
    stub_answer(true);
    TEST_ASSERT(editor_save_file(), "saved on Yes");
    TEST_ASSERT_EQ(boxes + 1, g_stub.message_boxes, "asked about the new change");
    TEST_ASSERT(disk_equals(P("a.txt"), "mine, edited\n"), "replaced with npad's version");
    TEST_ASSERT_EQ(EDITOR_FILE_UNCHANGED, editor_check_external_change(),
                   "the write is the new baseline");
}

// NP-004: auto-save overwrote external edits, with or without the watch
// preference, and re-stamped the file so no prompt ever appeared
TEST_CASE(auto_save_never_overwrites_external_change) {
    setup(); // watch_file_changes is off by default
    editor_enable_auto_save(true);
    write_text(P("a.txt"), "v1\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    stub_type("v1 mine\n");
    write_text(P("a.txt"), "v2 from elsewhere\n");

    tick_auto_save();
    TEST_ASSERT(disk_equals(P("a.txt"), "v2 from elsewhere\n"), "auto-save did not write");
    TEST_ASSERT_EQ(0, g_stub.message_boxes, "and asked nothing from the timer");
    TEST_ASSERT(strstr(g_stub.status_message, "paused") != NULL, "the status bar says why");

    // "Keep editing" adopts the disk version as the baseline but promises
    // nothing is written, so auto-save keeps waiting
    editor_accept_external_change();
    tick_auto_save();
    TEST_ASSERT(disk_equals(P("a.txt"), "v2 from elsewhere\n"), "still held after Keep editing");

    // A manual save is the user's decision (no prompt with watching off)
    TEST_ASSERT(editor_save_file(), "manual save");
    TEST_ASSERT(disk_equals(P("a.txt"), "v1 mine\n"), "the user's version");
    TEST_ASSERT_STR_EQ("", g_stub.status_message, "the auto-save note is cleared");

    stub_type("v1 mine, again\n");
    tick_auto_save();
    TEST_ASSERT(disk_equals(P("a.txt"), "v1 mine, again\n"), "auto-save resumes");
    TEST_ASSERT(!editor_is_modified(), "and the document is saved");

    // A file deleted (or moved) behind npad's back is not recreated by a timer
    remove(P("a.txt"));
    stub_type("after delete\n");
    tick_auto_save();
    TEST_ASSERT_EQ(-1, disk_size(P("a.txt")), "a deleted file stays deleted");
}

// NP-019: a failing auto-save opened a modal error box from the timer, and
// on Win32 another one on top of it every interval
TEST_CASE(auto_save_failure_is_silent) {
    setup();
    editor_enable_auto_save(true);
    write_text(P("a.txt"), "x\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    stub_type("y\n");

    // Point the document somewhere that cannot be written, with a baseline
    // that matches (nothing there before, nothing there now)
    free(g_editor.current_file);
    g_editor.current_file = dup_text(P("missing_dir/x.txt"));
    g_editor.file_stamp.size = -1;
    g_editor.file_stamp.write_time = 0;

    for (int i = 0; i < 3; i++)
        tick_auto_save();
    TEST_ASSERT_EQ(0, g_stub.message_boxes, "no modal box from the timer");
    TEST_ASSERT(strstr(g_stub.status_message, "Auto-save failed") != NULL,
                "the failure is in the status bar");
    TEST_ASSERT(editor_is_modified(), "still modified");

    // A manual save still explains the failure
    TEST_ASSERT(!editor_save_file(), "manual save fails too");
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "with one error box");
}

// A timer tick during one of npad's own questions must not save
TEST_CASE(auto_save_waits_while_a_question_is_open) {
    setup();
    editor_enable_auto_save(true);
    write_text(P("a.txt"), "original\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    stub_type("draft the user is about to discard\n");

    g_stub.during_message_box = tick_auto_save;
    g_stub.save_prompt_answer = UI_SAVE_PROMPT_DISCARD;
    TEST_ASSERT(editor_new_file(), "New after Don't Save");
    TEST_ASSERT_EQ(1, g_stub.save_prompts, "asked to save");
    TEST_ASSERT(disk_equals(P("a.txt"), "original\n"), "Don't Save meant nothing was written");
}

// NP-006: a failed or declined Save As switched the encoding anyway, and the
// next Ctrl+S re-encoded the original
TEST_CASE(failed_save_as_keeps_the_encoding) {
    setup();
    write_text(P("a.txt"), "notes caf\xC3\xA9\n");
    TEST_ASSERT(editor_open_file(P("a.txt")), "opens");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, editor_get_encoding(), "UTF-8 file");

    g_stub.save_dialog_result = P("missing_dir/out.txt");
    g_stub.save_dialog_picks_encoding = true;
    g_stub.save_dialog_encoding = NPAD_ENC_UTF16_LE;
    stub_post(UI_EVENT_FILE_SAVE_AS, NULL);
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "the write error is reported");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, editor_get_encoding(), "encoding unchanged after failure");
    TEST_ASSERT_STR_EQ("UTF-8", g_stub.status_encoding, "status bar agrees");
    TEST_ASSERT_STR_EQ(P("a.txt"), editor_get_current_file(), "still the same file");

    stub_type("notes caf\xC3\xA9 edited\n");
    TEST_ASSERT(editor_save_file(), "Ctrl+S");
    TEST_ASSERT(disk_equals(P("a.txt"), "notes caf\xC3\xA9 edited\n"), "still written as UTF-8");

    // Declining the lossy ANSI warning changes nothing either
    g_stub.save_dialog_result = P("ansi.txt");
    g_stub.save_dialog_encoding = NPAD_ENC_ANSI;
    stub_type("snowman \xE2\x98\x83\n");
    stub_answer(false);
    stub_post(UI_EVENT_FILE_SAVE_AS, NULL);
    TEST_ASSERT_EQ(-1, disk_size(P("ansi.txt")), "declined: nothing written");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, editor_get_encoding(), "encoding unchanged after No");

    // A successful Save As commits the choice
    g_stub.save_dialog_result = P("utf16.txt");
    g_stub.save_dialog_encoding = NPAD_ENC_UTF16_LE;
    stub_post(UI_EVENT_FILE_SAVE_AS, NULL);
    size_t len = 0;
    char *bytes = read_disk(P("utf16.txt"), &len);
    TEST_ASSERT_NOT_NULL(bytes, "written");
    bool bom = len >= 2 && (unsigned char) bytes[0] == 0xFF && (unsigned char) bytes[1] == 0xFE;
    free(bytes);
    TEST_ASSERT(bom, "as UTF-16 LE");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, editor_get_encoding(), "the document is UTF-16 LE now");
    TEST_ASSERT_STR_EQ("UTF-16 LE", g_stub.status_encoding, "and the status bar says so");
    TEST_ASSERT_STR_EQ(P("utf16.txt"), editor_get_current_file(), "the new file is current");
}

// NP-005: Save As on a binary fragment offered the original's own name, and
// Enter plus Replace overwrote it with the fragment
TEST_CASE(truncated_save_as_never_targets_the_original) {
    setup();
    unsigned char blob[210];
    memcpy(blob, "head text", 9);
    blob[9] = 0;
    memset(blob + 10, 'B', sizeof(blob) - 10);
    write_bytes(P("blob.dat"), blob, sizeof(blob));

    TEST_ASSERT(editor_open_file(P("blob.dat")), "opens in npad");
    TEST_ASSERT_EQ(1, g_stub.binary_prompts, "after the binary question");
    TEST_ASSERT(g_editor.file_info.truncated, "only the part before the NUL is loaded");

    g_stub.save_dialog_result = P("blob.dat");
    stub_post(UI_EVENT_FILE_SAVE_AS, NULL);
    TEST_ASSERT_STR_EQ(P("blob (text).txt"), g_stub.save_dialog_default,
                       "Save As offers a derived name, not the original");
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "picking the original anyway is refused");
    TEST_ASSERT_EQ(210, disk_size(P("blob.dat")), "the original is intact");
    TEST_ASSERT(g_editor.file_info.truncated, "and the guard stays on");

#ifndef _WIN32
    // Another name for the same file is the same file
    remove(P("blob_link.dat"));
    TEST_ASSERT_EQ(0, link(P("blob.dat"), P("blob_link.dat")), "hard link made");
    g_stub.save_dialog_result = P("blob_link.dat");
    stub_post(UI_EVENT_FILE_SAVE_AS, NULL);
    TEST_ASSERT_EQ(2, g_stub.message_boxes, "a hard link to the original is refused too");
    TEST_ASSERT_EQ(210, disk_size(P("blob.dat")), "still intact");
#endif

    // Ctrl+S explains, then saves to the derived name
    int boxes = g_stub.message_boxes;
    g_stub.save_dialog_result = P("blob (text).txt");
    TEST_ASSERT(editor_save_file(), "saved elsewhere");
    TEST_ASSERT_EQ(boxes + 1, g_stub.message_boxes, "one explanation");
    TEST_ASSERT(disk_equals(P("blob (text).txt"), "head text"), "the fragment went to the new file");
    TEST_ASSERT_EQ(210, disk_size(P("blob.dat")), "the original is untouched");
    TEST_ASSERT(!g_editor.file_info.truncated, "the new file is complete, so Save may overwrite it");
}

// NP-115: "Open with the default app" on a binary file ran programs silently
TEST_CASE(programs_ask_before_the_default_app) {
    setup();
    TEST_ASSERT(editor_is_program_file(P("setup.EXE")), "by extension, any case");
    TEST_ASSERT(editor_is_program_file("C:\\Users\\x\\script.ps1"), "a PowerShell script");
    TEST_ASSERT(!editor_is_program_file(P("notes.txt")), "a missing text file is not");

    write_text(P("runme"), "#!/bin/sh\necho hi\n");
    TEST_ASSERT(editor_is_program_file(P("runme")), "a script by its interpreter line");
    static const unsigned char pe[] = { 'M', 'Z', 0x90, 0x00, 0x03, 0x00, 0x00, 0x00 };
    write_bytes(P("renamed.dat"), pe, sizeof(pe));
    TEST_ASSERT(editor_is_program_file(P("renamed.dat")), "a renamed PE image by content");
    write_text(P("plain.dat"), "plain\n");
    TEST_ASSERT(!editor_is_program_file(P("plain.dat")), "plain data is not");

    write_bytes(P("tool.exe"), pe, sizeof(pe));
    g_stub.binary_choice = UI_OPEN_WITH_DEFAULT;
    stub_answer(false);
    TEST_ASSERT(!editor_open_file(P("tool.exe")), "not opened in npad");
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "asked first");
    TEST_ASSERT(strstr(g_stub.last_message, "may run it") != NULL, "naming the risk");
    TEST_ASSERT_EQ(0, g_stub.default_app_opens, "No hands nothing over");

    stub_answer(true);
    editor_open_file(P("tool.exe"));
    TEST_ASSERT_EQ(1, g_stub.default_app_opens, "Yes hands it over");

    static const unsigned char png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
                                         0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R' };
    write_bytes(P("pic.png"), png, sizeof(png));
    int boxes = g_stub.message_boxes;
    editor_open_file(P("pic.png"));
    TEST_ASSERT_EQ(boxes, g_stub.message_boxes, "an image opens without the extra question");
    TEST_ASSERT_EQ(2, g_stub.default_app_opens, "and goes to its default app");
}

// NP-212: Notepad's .LOG
TEST_CASE(log_files_get_a_time_stamp) {
    setup();
    write_text(P("log.txt"), ".LOG\r\nfirst entry\r\n");
    TEST_ASSERT(editor_open_file(P("log.txt")), "opens");
    TEST_ASSERT_EQ(1, g_stub.stamps_appended, "stamped once");
    size_t len = strlen(g_stub.text);
    const char *tail = "first entry\r\n\n12:34 21/10/2026\n";
    TEST_ASSERT(len >= strlen(tail) && strcmp(g_stub.text + len - strlen(tail), tail) == 0,
                "on a new line at the end");
    TEST_ASSERT(editor_is_modified(), "the stamp is an unsaved edit, as in Notepad");
    TEST_ASSERT(g_stub.title[0] == '*', "and the title says so");
    TEST_ASSERT(disk_equals(P("log.txt"), ".LOG\r\nfirst entry\r\n"), "nothing written yet");

    setup();
    write_text(P("log.txt"), ".LOG");
    editor_open_file(P("log.txt"));
    TEST_ASSERT_EQ(1, g_stub.stamps_appended, "a file that is only .LOG");

    setup();
    write_text(P("log.txt"), ".LOGGING notes\n");
    editor_open_file(P("log.txt"));
    TEST_ASSERT_EQ(0, g_stub.stamps_appended, "the first line must be exactly .LOG");
    TEST_ASSERT(!editor_is_modified(), "an ordinary file opens unmodified");

    setup();
    write_text(P("log.txt"), ".log\n");
    editor_open_file(P("log.txt"));
    TEST_ASSERT_EQ(0, g_stub.stamps_appended, "upper case only, as in Notepad");

    setup();
    editor_reload_from_disk(); // No file: a no-op, and never a stamp
    write_text(P("log.txt"), ".LOG\n");
    editor_open_file(P("log.txt"));
    TEST_ASSERT_EQ(1, g_stub.stamps_appended, "stamped on open");
    TEST_ASSERT(editor_reload_from_disk(), "reload");
    TEST_ASSERT_EQ(1, g_stub.stamps_appended, "but not again on reload");
}

// NP-178: Notepad's "Cannot find the file. Do you want to create a new file?"
TEST_CASE(missing_startup_file_offers_to_create_it) {
    setup();
    remove(P("new.txt"));
    stub_answer(true);
    TEST_ASSERT(editor_open_startup_file(P("new.txt")), "created and opened");
    TEST_ASSERT(strstr(g_stub.last_message, "Cannot find the") != NULL, "Notepad's question");
    TEST_ASSERT(strstr(g_stub.last_message, "Do you want to create a new file?") != NULL,
                "worded as Notepad words it");
    TEST_ASSERT_EQ(0, disk_size(P("new.txt")), "an empty file now exists");
    TEST_ASSERT_STR_EQ(P("new.txt"), editor_get_current_file(), "and is the current file");
    TEST_ASSERT(!editor_is_modified(), "unmodified");

    // Save goes straight back to it, no Save As
    stub_type("todo\n");
    TEST_ASSERT(editor_save_file(), "Ctrl+S");
    TEST_ASSERT_EQ(0, g_stub.save_dialogs, "without a Save As dialog");
    TEST_ASSERT(disk_equals(P("new.txt"), "todo\n"), "into the named file");

    setup();
    remove(P("other.txt"));
    stub_answer(false);
    TEST_ASSERT(!editor_open_startup_file(P("other.txt")), "No: nothing opened");
    TEST_ASSERT_EQ(-1, disk_size(P("other.txt")), "and nothing created");
    TEST_ASSERT_NULL(editor_get_current_file(), "an Untitled document");

    setup();
    TEST_ASSERT(!editor_open_startup_file(P("no_such_dir/x.txt")), "missing folder");
    TEST_ASSERT_EQ(1, g_stub.message_boxes, "one box");
    TEST_ASSERT(strstr(g_stub.last_message, "Could not open file") != NULL,
                "the ordinary error, not the create question");

    setup();
    write_text(P("a.txt"), "exists\n");
    TEST_ASSERT(editor_open_startup_file(P("a.txt")), "an existing file just opens");
    TEST_ASSERT_EQ(0, g_stub.message_boxes, "with no question");
}

// NP-021: the snapshot rewrote the whole unchanged document every interval
TEST_CASE(snapshot_only_when_something_changed) {
    setup();
    write_text(P("snap.txt"), "s\n");
    TEST_ASSERT(editor_open_file(P("snap.txt")), "opens");
    char *dir = editor_session_dir();
    TEST_ASSERT_NOT_NULL(dir, "recovery directory");
    const char *slot = editor_session_slot_id();

    // Reading a slot takes it (removes it), so each check starts from none
    stub_type("s1\n"); // The first edit snapshots at once
    char *saved = take_slot(dir, slot);
    TEST_ASSERT(saved && strcmp(saved, "s1\n") == 0, "first edit snapshotted");
    free(saved);

    stub_post(UI_EVENT_SESSION_SNAPSHOT, NULL);
    saved = take_slot(dir, slot);
    TEST_ASSERT_NULL(saved, "an unchanged document is not rewritten");

    stub_type("s2\n");
    stub_post(UI_EVENT_SESSION_SNAPSHOT, NULL);
    saved = take_slot(dir, slot);
    TEST_ASSERT(saved && strcmp(saved, "s2\n") == 0, "a changed one is");
    free(saved);

    // A line-ending change is a change too (the snapshot records it)
    editor_set_line_ending(NPAD_EOL_CRLF);
    stub_post(UI_EVENT_SESSION_SNAPSHOT, NULL);
    saved = take_slot(dir, slot);
    TEST_ASSERT_NOT_NULL(saved, "format changes are snapshotted");
    free(saved);

    session_clear_slot(dir, slot);
    free(dir);
}

// NP-208: every settings broadcast re-armed both timers, restarting them
TEST_CASE(timers_rearm_only_when_changed) {
    setup();
    TEST_ASSERT_EQ(1, g_stub.auto_save_arms, "armed once on attach");
    TEST_ASSERT_EQ(1, g_stub.session_arms, "both of them");
    TEST_ASSERT_EQ(0, g_stub.auto_save_seconds, "auto-save is off by default");
    TEST_ASSERT_EQ(30, g_stub.session_seconds, "snapshots every 30 s by default");

    for (int i = 0; i < 3; i++)
        editor_reload_prefs(); // Three broadcasts from other instances
    TEST_ASSERT_EQ(1, g_stub.auto_save_arms, "unchanged auto-save timer left running");
    TEST_ASSERT_EQ(1, g_stub.session_arms, "unchanged session timer left running");

    settings_set_bool("auto_save_enabled", true);
    settings_set_int("auto_save_interval", 60);
    editor_reload_prefs();
    TEST_ASSERT_EQ(2, g_stub.auto_save_arms, "a real change re-arms");
    TEST_ASSERT_EQ(60, g_stub.auto_save_seconds, "at the new interval");
    TEST_ASSERT_EQ(1, g_stub.session_arms, "and only that timer");
}

int main(void) {
    TEST_INIT();
    thread_safety_init();
    make_dir(TEST_DIR);
    remove_files();
    // Settings, and so the recovery directory, live in the scratch directory
    settings_set_file_path(P("settings.json"));

    RUN_TEST(save_writes_and_restamps);
    RUN_TEST(manual_save_asks_before_overwriting_external_change);
    RUN_TEST(auto_save_never_overwrites_external_change);
    RUN_TEST(auto_save_failure_is_silent);
    RUN_TEST(auto_save_waits_while_a_question_is_open);
    RUN_TEST(failed_save_as_keeps_the_encoding);
    RUN_TEST(truncated_save_as_never_targets_the_original);
    RUN_TEST(programs_ask_before_the_default_app);
    RUN_TEST(log_files_get_a_time_stamp);
    RUN_TEST(missing_startup_file_offers_to_create_it);
    RUN_TEST(snapshot_only_when_something_changed);
    RUN_TEST(timers_rearm_only_when_changed);

    editor_cleanup();
    char *dir = editor_session_dir();
    if (dir) {
        session_clear_slot(dir, editor_session_slot_id());
        remove_dir(dir);
        free(dir);
    }
    remove_files();
    remove_dir(TEST_DIR);
    settings_cleanup();
    TEST_SUMMARY();
}
