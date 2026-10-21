/*
 * npad - Session Recovery Tests
 * Unit tests for per-slot crash-recovery snapshots
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* rmdir, chmod, geteuid under -std=c99 */
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <direct.h>
#include <windows.h>
#endif
#include "test_framework.h"
#include "../src/core/session.h"
#include "../src/core/file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DIR "test_session_dir"

// Slot ids must have the shape npad generates (<time>-<pid>[-<nonce>])
#define SLOT_A "5f000001-1a2b"
#define SLOT_B "5f000002-1a2c-beef"
#define SLOT_C "5f000003-99"
#define CLAIMER_1 "6a000001-501-1"
#define CLAIMER_2 "6a000002-502-2"

// Remove every file in TEST_DIR, then the directory itself
static void cleanup(void) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(TEST_DIR "\\*", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char path[512];
                snprintf(path, sizeof(path), "%s\\%s", TEST_DIR, fd.cFileName);
                DeleteFileA(path);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    _rmdir(TEST_DIR);
#else
    DIR *d = opendir(TEST_DIR);
    if (d) {
        const struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", TEST_DIR, entry->d_name);
            chmod(path, 0600); // A test may have made one unreadable
            remove(path);
        }
        closedir(d);
    }
    rmdir(TEST_DIR);
#endif
}

static char *test_path(const char *name) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, name);
    return path;
}

static bool dir_has(const char *name) {
    return file_exists(test_path(name));
}

static bool slots_contains(char **slots, int count, const char *id) {
    for (int i = 0; i < count; i++) {
        if (strcmp(slots[i], id) == 0)
            return true;
    }
    return false;
}

static int listed_count(void) {
    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    session_free_slots(slots, count);
    return count;
}

TEST_CASE(session_list_reports_all_slots) {
    cleanup();
    TEST_ASSERT(
        session_write(TEST_DIR, SLOT_A, "content A", "C:/a.txt", NPAD_ENC_UTF8, NPAD_EOL_CRLF),
        "write slot A");
    TEST_ASSERT(session_write(TEST_DIR, SLOT_B, "content B", NULL, NPAD_ENC_UTF16_LE, NPAD_EOL_LF),
                "write slot B");

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(2, count, "two slots should be listed");
    TEST_ASSERT(slots_contains(slots, count, SLOT_A), "slot A present");
    TEST_ASSERT(slots_contains(slots, count, SLOT_B), "slot B present");
    session_free_slots(slots, count);

    cleanup();
}

TEST_CASE(session_take_reads_and_removes) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "hello\nworld", "C:/note.txt", NPAD_ENC_UTF16_LE, NPAD_EOL_LF);

    char *path = NULL;
    TextEncoding enc = NPAD_ENC_UTF8;
    LineEnding eol = NPAD_EOL_CRLF;
    char *content = session_take(TEST_DIR, SLOT_A, &path, &enc, &eol);

    TEST_ASSERT_NOT_NULL(content, "take returns content");
    TEST_ASSERT_STR_EQ("hello\nworld", content, "content round-trips");
    TEST_ASSERT_NOT_NULL(path, "path round-trips");
    TEST_ASSERT_STR_EQ("C:/note.txt", path, "path matches");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, enc, "encoding round-trips");
    TEST_ASSERT_EQ(NPAD_EOL_LF, eol, "line ending round-trips");
    free(content);
    free(path);

    TEST_ASSERT_EQ(0, listed_count(), "no slots remain after take");
    TEST_ASSERT(!dir_has(SLOT_A ".txt"), "content removed after take");
    cleanup();
}

TEST_CASE(session_take_twice_second_is_null) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "once", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    char *first = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(first, "first take succeeds");
    free(first);

    char *second = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_NULL(second, "second take of the same slot returns NULL");

    cleanup();
}

TEST_CASE(session_take_untitled) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "draft", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    char *path = (char *) 0x1; // Poison to prove it is cleared
    char *content = session_take(TEST_DIR, SLOT_A, &path, NULL, NULL);
    TEST_ASSERT_NOT_NULL(content, "content read");
    TEST_ASSERT_STR_EQ("draft", content, "content matches");
    TEST_ASSERT_NULL(path, "untitled slot reports NULL path");
    free(content);

    cleanup();
}

TEST_CASE(session_clear_slot_removes_it) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_write(TEST_DIR, SLOT_B, "y", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    session_clear_slot(TEST_DIR, SLOT_A);

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "one slot remains after clearing slot A");
    TEST_ASSERT(slots_contains(slots, count, SLOT_B), "slot B still present");
    session_free_slots(slots, count);

    cleanup();
}

TEST_CASE(session_take_missing_returns_null) {
    cleanup();
    char *content = session_take(TEST_DIR, SLOT_C, NULL, NULL, NULL);
    TEST_ASSERT_NULL(content, "taking a missing slot returns NULL");
    TEST_ASSERT_EQ(0, listed_count(), "no slots listed");
}

// --- Handoff markers -------------------------------------------------------
// A marked slot was parked because something else was closing npad, so it is
// restored silently instead of under the "closed unexpectedly" prompt. Getting
// this partition wrong either nags the user after every update or silently
// reopens work that really did crash.

TEST_CASE(session_handoff_unmarked_by_default) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A), "a plain crash snapshot is not a handoff");
    cleanup();
}

TEST_CASE(session_handoff_mark_and_read_back) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(session_mark_handoff(TEST_DIR, SLOT_A), "marking succeeds");
    TEST_ASSERT(session_is_handoff(TEST_DIR, SLOT_A), "mark reads back");
    cleanup();
}

TEST_CASE(session_handoff_is_per_slot) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_write(TEST_DIR, SLOT_B, "y", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);

    TEST_ASSERT(session_is_handoff(TEST_DIR, SLOT_A), "marked slot reports true");
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_B), "unmarked sibling stays false");
    cleanup();
}

TEST_CASE(session_handoff_clear_reverts_to_crash) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);
    session_clear_handoff(TEST_DIR, SLOT_A);

    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A),
                "a cancelled session end leaves a plain crash snapshot");

    // Clearing the mark must NOT remove the snapshot itself
    TEST_ASSERT_EQ(1, listed_count(), "the snapshot survives clearing its mark");
    cleanup();
}

TEST_CASE(session_clear_slot_removes_handoff_marker) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);
    session_clear_slot(TEST_DIR, SLOT_A);

    // A marker outliving its slot would make a later slot of the same id
    // restore silently when it should have prompted
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A),
                "clearing a slot removes its handoff marker too");
    cleanup();
}

TEST_CASE(session_take_clears_handoff_marker) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);

    char *content = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(content, "slot is taken");
    free(content);

    // The --recover path restores a slot directly; an orphaned marker would
    // accumulate in the recovery directory forever
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A), "taking a slot clears its handoff marker");
    cleanup();
}

TEST_CASE(session_handoff_marker_is_not_a_slot) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);

    // Slots are enumerated from .meta files; the marker must not be counted
    // as a document of its own
    TEST_ASSERT_EQ(1, listed_count(), "the marker does not list as an extra slot");
    cleanup();
}

// --- Slot ids (NP-114) -------------------------------------------------------
// An id from --recover becomes part of file names that are renamed and
// deleted, so anything but npad's own shape must be refused.

TEST_CASE(session_slot_id_shape) {
    TEST_ASSERT(session_is_valid_slot_id("5f000001-1a2b"), "two-part id (before 0.36)");
    TEST_ASSERT(session_is_valid_slot_id("5f000001-1a2b-beef"), "three-part id");
    TEST_ASSERT(!session_is_valid_slot_id(NULL), "NULL");
    TEST_ASSERT(!session_is_valid_slot_id(""), "empty");
    TEST_ASSERT(!session_is_valid_slot_id("5f000001"), "one part");
    TEST_ASSERT(!session_is_valid_slot_id("1-2-3-4"), "four parts");
    TEST_ASSERT(!session_is_valid_slot_id("5f000001-"), "empty part");
    TEST_ASSERT(!session_is_valid_slot_id("-1a2b"), "leading dash");
    TEST_ASSERT(!session_is_valid_slot_id("5f00g001-1a2b"), "not hex");
    TEST_ASSERT(!session_is_valid_slot_id("../../etc/passwd"), "traversal");
    TEST_ASSERT(!session_is_valid_slot_id("5f000001-1a2b/../x"), "slash");
    TEST_ASSERT(!session_is_valid_slot_id("5f000001-1a2b\\x"), "backslash");
    TEST_ASSERT(!session_is_valid_slot_id("5f000001-1a2b.meta"), "dot");
    TEST_ASSERT(!session_is_valid_slot_id("C:-1"), "colon");
    TEST_ASSERT(!session_is_valid_slot_id("11111111111111111-1"), "part over 16 digits");

    unsigned long long t = 0;
    long pid = 0;
    TEST_ASSERT(session_parse_slot_id("5f000001-1a2b-beef", &t, &pid), "parse");
    TEST_ASSERT(t == 0x5f000001ULL, "time part");
    TEST_ASSERT_EQ(0x1a2b, pid, "pid part");
    TEST_ASSERT(!session_parse_slot_id("../x", &t, &pid), "parse refuses a bad id");
}

TEST_CASE(session_made_ids_are_valid_and_distinct) {
    char a[SESSION_SLOT_ID_MAX], b[SESSION_SLOT_ID_MAX];
    session_make_slot_id(a, sizeof(a));
    session_make_slot_id(b, sizeof(b));
    TEST_ASSERT(session_is_valid_slot_id(a), "a made id is valid");
    unsigned long long t = 0;
    long pid = 0;
    TEST_ASSERT(session_parse_slot_id(a, &t, &pid), "a made id parses");
#ifndef _WIN32
    TEST_ASSERT_EQ((long) getpid(), pid, "it carries this process's pid");
#endif
    // Same second, same pid: only the nonce tells them apart (a pid namespace
    // gives two instances that much in common). A clash is possible but rare;
    // three tries make a spurious failure vanishingly unlikely.
    bool differ = strcmp(a, b) != 0;
    for (int i = 0; i < 3 && !differ; i++) {
        session_make_slot_id(b, sizeof(b));
        differ = strcmp(a, b) != 0;
    }
    TEST_ASSERT(differ, "two ids made in the same second differ");
}

TEST_CASE(session_invalid_ids_touch_nothing) {
    cleanup();
    // A file named like a slot, but one directory up: a crafted id reaching
    // it would rename or delete it
    session_write(TEST_DIR, SLOT_A, "keep", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(!session_write(TEST_DIR, "../victim", "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF),
                "write refuses a crafted id");
    TEST_ASSERT(!file_exists("victim.txt"), "nothing written outside the directory");
    FILE *f = fopen("test_session_victim.meta", "w");
    TEST_ASSERT_NOT_NULL(f, "make victim");
    fputs("0\n0\n", f);
    fclose(f);
    session_clear_slot(TEST_DIR, "../test_session_victim");
    TEST_ASSERT(file_exists("test_session_victim.meta"), "clear refuses a crafted id");
    TEST_ASSERT_NULL(session_take(TEST_DIR, "../test_session_victim", NULL, NULL, NULL),
                     "take refuses a crafted id");
    TEST_ASSERT(file_exists("test_session_victim.meta"), "victim still in place");
    TEST_ASSERT(!session_claim(TEST_DIR, SLOT_A, NULL, "../x"), "a crafted claimer is refused");
    TEST_ASSERT_EQ(1, listed_count(), "the real slot is untouched");
    remove("test_session_victim.meta");
    cleanup();
}

TEST_CASE(session_listing_skips_foreign_names) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    FILE *f = fopen(test_path("notes.meta"), "w");
    TEST_ASSERT_NOT_NULL(f, "make a foreign .meta");
    fputs("0\n0\n", f);
    fclose(f);
    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "only npad's own ids are listed");
    TEST_ASSERT(slots_contains(slots, count, SLOT_A), "the real slot");
    session_free_slots(slots, count);
    cleanup();
}

// --- Meta (NP-001, NP-027) ---------------------------------------------------

TEST_CASE(session_meta_round_trips_truncated_and_stamp) {
    cleanup();
    SessionMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.encoding = NPAD_ENC_UTF16_BE;
    meta.line_ending = NPAD_EOL_LF;
    meta.truncated = true;
    meta.stamp.size = 123456;
    meta.stamp.write_time = 133000000000000123LL;
    TEST_ASSERT(session_write_ex(TEST_DIR, SLOT_A, "frag", "/data/a b.bin", &meta), "write");

    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1), "claim");
    char *content = NULL, *path = NULL;
    SessionMeta back;
    TEST_ASSERT_EQ(SESSION_READ_OK,
                   session_read_claimed(TEST_DIR, SLOT_A, CLAIMER_1, &content, &path, &back),
                   "read");
    TEST_ASSERT_STR_EQ("frag", content, "content");
    TEST_ASSERT_STR_EQ("/data/a b.bin", path, "path, with its space");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_BE, back.encoding, "encoding");
    TEST_ASSERT_EQ(NPAD_EOL_LF, back.line_ending, "line ending");
    TEST_ASSERT(back.has_truncated, "the flag is recorded");
    TEST_ASSERT(back.truncated, "truncated survives");
    TEST_ASSERT(back.stamp.size == 123456, "stamp size");
    TEST_ASSERT(back.stamp.write_time == 133000000000000123LL, "stamp time");
    free(content);
    free(path);
    cleanup();
}

TEST_CASE(session_meta_reads_pre_036_format) {
    cleanup();
    // What npad before 0.36 wrote: three lines, no attributes
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF); // Makes the dir
    FILE *f = fopen(test_path(SLOT_A ".meta"), "wb");
    TEST_ASSERT_NOT_NULL(f, "rewrite meta");
    fputs("2\n1\n/home/u/old.txt", f);
    fclose(f);

    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1), "claim");
    char *content = NULL, *path = NULL;
    SessionMeta back;
    TEST_ASSERT_EQ(SESSION_READ_OK,
                   session_read_claimed(TEST_DIR, SLOT_A, CLAIMER_1, &content, &path, &back),
                   "read");
    TEST_ASSERT_STR_EQ("/home/u/old.txt", path, "path");
    TEST_ASSERT_EQ(2, (int) back.encoding, "encoding");
    TEST_ASSERT_EQ(1, (int) back.line_ending, "line ending");
    TEST_ASSERT(!back.has_truncated, "an old meta says nothing about truncation");
    TEST_ASSERT(!back.truncated, "and is not read as truncated");
    TEST_ASSERT(back.stamp.size == -1, "no stamp");
    free(content);
    free(path);
    cleanup();
}

TEST_CASE(session_meta_stays_readable_by_pre_036) {
    cleanup();
    SessionMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.encoding = NPAD_ENC_UTF8_BOM;
    meta.line_ending = NPAD_EOL_CR;
    meta.truncated = true;
    meta.stamp.size = 5;
    session_write_ex(TEST_DIR, SLOT_A, "x", "/p", &meta);
    char *text = file_read_text(test_path(SLOT_A ".meta"));
    TEST_ASSERT_NOT_NULL(text, "meta readable");
    // The old parser: atoi of line 1 and line 2, the path after the second
    // newline. The attributes must not disturb any of that.
    const char *nl1 = strchr(text, '\n');
    const char *nl2 = nl1 ? strchr(nl1 + 1, '\n') : NULL;
    TEST_ASSERT(nl1 && nl2, "still three lines");
    TEST_ASSERT_EQ((int) NPAD_ENC_UTF8_BOM, atoi(text), "old reader's encoding");
    TEST_ASSERT_EQ((int) NPAD_EOL_CR, atoi(nl1 + 1), "old reader's line ending");
    TEST_ASSERT_STR_EQ("/p", nl2 + 1, "old reader's path");
    free(text);
    cleanup();
}

// --- Claims (NP-007, NP-219) ---------------------------------------------------

TEST_CASE(session_claimed_slot_is_not_listed_and_only_one_wins) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1), "first claim wins");
    TEST_ASSERT(!session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_2), "second claim loses");
    TEST_ASSERT_EQ(0, listed_count(), "a claimed slot is not offered to anyone else");

    TEST_ASSERT(session_release(TEST_DIR, SLOT_A, CLAIMER_1), "release");
    TEST_ASSERT_EQ(1, listed_count(), "released: offered again");
    cleanup();
}

TEST_CASE(session_claim_handover) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1), "parent claims");
    TEST_ASSERT(!session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_2),
                "the child cannot take it as an unclaimed slot");
    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, CLAIMER_1, CLAIMER_2),
                "the child takes over the parent's claim");
    TEST_ASSERT(!session_claim(TEST_DIR, SLOT_A, CLAIMER_1, CLAIMER_1),
                "the parent no longer holds it");
    char *content = NULL, *path = NULL;
    SessionMeta meta;
    TEST_ASSERT_EQ(SESSION_READ_OK,
                   session_read_claimed(TEST_DIR, SLOT_A, CLAIMER_2, &content, &path, &meta),
                   "child reads it");
    TEST_ASSERT_STR_EQ("doc", content, "content");
    free(content);
    free(path);
    cleanup();
}

TEST_CASE(session_discard_claimed_removes_everything) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);
    session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1);
    session_discard_claimed(TEST_DIR, SLOT_A, CLAIMER_1);
    TEST_ASSERT(!dir_has(SLOT_A ".txt"), "content gone");
    TEST_ASSERT(!dir_has(SLOT_A ".meta.claim-" CLAIMER_1), "claim gone");
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A), "marker gone");
    cleanup();
}

TEST_CASE(session_read_of_a_slot_without_text_is_empty) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "doc", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    remove(test_path(SLOT_A ".txt"));
    session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1);
    char *content = NULL, *path = NULL;
    SessionMeta meta;
    TEST_ASSERT_EQ(SESSION_READ_EMPTY,
                   session_read_claimed(TEST_DIR, SLOT_A, CLAIMER_1, &content, &path, &meta),
                   "no text: empty");
    TEST_ASSERT_EQ(SESSION_READ_GONE,
                   session_read_claimed(TEST_DIR, SLOT_A, CLAIMER_2, &content, &path, &meta),
                   "not this claimer's: gone");
    cleanup();
}

#ifndef _WIN32
// NP-007: the only copy of the document must survive a read that fails
TEST_CASE(session_take_keeps_an_unreadable_slot) {
    cleanup();
    if (geteuid() == 0) {
        printf("(skipped as root: permissions do not stop root) ");
        return;
    }
    session_write(TEST_DIR, SLOT_A, "precious", "/a.txt", NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    chmod(test_path(SLOT_A ".txt"), 0);
    char *content = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_NULL(content, "the read fails");
    chmod(test_path(SLOT_A ".txt"), 0600);
    TEST_ASSERT_EQ(1, listed_count(), "the slot is given back, still listed");
    char *again = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_STR_EQ("precious", again, "and restores once readable");
    free(again);
    cleanup();
}
#endif

// --- Adopt (NP-009) ---------------------------------------------------------------

TEST_CASE(session_adopt_moves_the_slot_under_the_new_id) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "restored text", "/old/path.txt", NPAD_ENC_UTF8, NPAD_EOL_LF);
    session_mark_handoff(TEST_DIR, SLOT_A);
    TEST_ASSERT(session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1), "claim");

    SessionMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.encoding = NPAD_ENC_UTF8;
    meta.line_ending = NPAD_EOL_LF;
    meta.truncated = true;
    meta.stamp.size = -1;
    TEST_ASSERT(session_adopt(TEST_DIR, SLOT_A, CLAIMER_1, CLAIMER_1, "/old/path.txt", &meta),
                "adopt");

    TEST_ASSERT(!dir_has(SLOT_A ".txt"), "old text moved");
    TEST_ASSERT(!dir_has(SLOT_A ".meta.claim-" CLAIMER_1), "claim gone");
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_A), "old marker gone");
    TEST_ASSERT(!session_is_handoff(TEST_DIR, CLAIMER_1), "the new slot is not a handoff");

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "exactly one slot: the adopted one");
    TEST_ASSERT(slots_contains(slots, count, CLAIMER_1), "under the new id");
    session_free_slots(slots, count);

    TEST_ASSERT(session_claim(TEST_DIR, CLAIMER_1, NULL, CLAIMER_2), "claim the new slot");
    char *content = NULL, *path = NULL;
    SessionMeta back;
    session_read_claimed(TEST_DIR, CLAIMER_1, CLAIMER_2, &content, &path, &back);
    TEST_ASSERT_STR_EQ("restored text", content, "text intact");
    TEST_ASSERT_STR_EQ("/old/path.txt", path, "path intact");
    TEST_ASSERT(back.truncated, "the meta was rewritten from the caller's");
    free(content);
    free(path);
    cleanup();
}

// --- Orphans (NP-039) -------------------------------------------------------------

static bool live_is_claimer_2(const char *slot_id, void *ctx) {
    (void) ctx;
    return strcmp(slot_id, CLAIMER_2) == 0;
}

static void write_raw(const char *name, const char *text) {
    FILE *f = fopen(test_path(name), "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

TEST_CASE(session_orphans_stale_claim_is_handed_back) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "a", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_write(TEST_DIR, SLOT_B, "b", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_claim(TEST_DIR, SLOT_A, NULL, CLAIMER_1); // Claimer 1 died mid-restore
    session_claim(TEST_DIR, SLOT_B, NULL, CLAIMER_2); // Claimer 2 is still at it
    TEST_ASSERT_EQ(0, listed_count(), "both claimed");

    TEST_ASSERT_EQ(1, session_recover_orphans(TEST_DIR, NULL, live_is_claimer_2, NULL),
                   "one slot made listable");
    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "the dead claimer's slot is back");
    TEST_ASSERT(slots_contains(slots, count, SLOT_A), "slot A");
    session_free_slots(slots, count);
    TEST_ASSERT(dir_has(SLOT_B ".meta.claim-" CLAIMER_2), "a live claim is left alone");
    cleanup();
}

TEST_CASE(session_orphans_old_taking_claims_come_back) {
    cleanup();
    session_write(TEST_DIR, SLOT_C, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF); // Makes the dir
    session_clear_slot(TEST_DIR, SLOT_C);
    // npad before 0.36, killed between its claim rename and its delete
    write_raw(SLOT_A ".meta.taking", "0\n0\n/a.txt");
    write_raw(SLOT_A ".txt.taking", "interrupted take");
    // The same, with the text already deleted: nothing left to offer
    write_raw(SLOT_B ".meta.taking", "0\n0\n");

    TEST_ASSERT_EQ(1, session_recover_orphans(TEST_DIR, NULL, live_is_claimer_2, NULL),
                   "one slot made listable");
    TEST_ASSERT_EQ(1, listed_count(), "the interrupted take is offered again");
    char *content = session_take(TEST_DIR, SLOT_A, NULL, NULL, NULL);
    TEST_ASSERT_STR_EQ("interrupted take", content, "with its text");
    free(content);
    TEST_ASSERT(!dir_has(SLOT_B ".meta.taking"), "the empty one is cleaned up");
    cleanup();
}

TEST_CASE(session_orphans_text_without_meta_is_offered_untitled) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "first snapshot", "/x.txt", NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    remove(test_path(SLOT_A ".meta")); // Cut off between the two files
    session_write(TEST_DIR, CLAIMER_2, "live", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    remove(test_path(CLAIMER_2 ".meta")); // A live instance mid-write

    TEST_ASSERT_EQ(1, session_recover_orphans(TEST_DIR, NULL, live_is_claimer_2, NULL),
                   "one slot made listable");
    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "only the dead owner's text is adopted");
    TEST_ASSERT(slots_contains(slots, count, SLOT_A), "slot A");
    session_free_slots(slots, count);

    char *path = (char *) 0x1;
    char *content = session_take(TEST_DIR, SLOT_A, &path, NULL, NULL);
    TEST_ASSERT_STR_EQ("first snapshot", content, "text");
    TEST_ASSERT_NULL(path, "offered untitled");
    free(content);
    cleanup();
}

TEST_CASE(session_orphans_empty_leftovers_are_removed) {
    cleanup();
    session_write(TEST_DIR, SLOT_A, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    remove(test_path(SLOT_A ".txt"));       // A meta with nothing behind it
    session_mark_handoff(TEST_DIR, SLOT_B); // A marker on its own
    session_write(TEST_DIR, SLOT_C, "own", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    remove(test_path(SLOT_C ".txt")); // ...but this one is our own slot

    session_recover_orphans(TEST_DIR, SLOT_C, live_is_claimer_2, NULL);
    TEST_ASSERT(!dir_has(SLOT_A ".meta"), "empty meta removed");
    TEST_ASSERT(!session_is_handoff(TEST_DIR, SLOT_B), "lone marker removed");
    TEST_ASSERT(dir_has(SLOT_C ".meta"), "own slot never touched");
    cleanup();
}

// --- Permissions (NP-110) ------------------------------------------------------

#ifndef _WIN32
TEST_CASE(session_files_are_private) {
    cleanup();
    // A directory left 0755 by an older npad is tightened on the next write
    TEST_ASSERT(mkdir(TEST_DIR, 0755) == 0, "make an old-style directory");
    session_write(TEST_DIR, SLOT_A, "secret", "/a.txt", NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_mark_handoff(TEST_DIR, SLOT_A);

    struct stat st;
    TEST_ASSERT(stat(TEST_DIR, &st) == 0, "stat dir");
    TEST_ASSERT_EQ(0700, (int) (st.st_mode & 0777), "recovery directory is 0700");
    TEST_ASSERT(stat(test_path(SLOT_A ".txt"), &st) == 0, "stat txt");
    TEST_ASSERT_EQ(0600, (int) (st.st_mode & 0777), "snapshot text is 0600");
    TEST_ASSERT(stat(test_path(SLOT_A ".meta"), &st) == 0, "stat meta");
    TEST_ASSERT_EQ(0600, (int) (st.st_mode & 0777), "meta is 0600");
    TEST_ASSERT(stat(test_path(SLOT_A ".handoff"), &st) == 0, "stat marker");
    TEST_ASSERT_EQ(0600, (int) (st.st_mode & 0777), "marker is 0600");
    cleanup();

    TEST_ASSERT(session_write(TEST_DIR, SLOT_B, "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF),
                "write into a fresh directory");
    TEST_ASSERT(stat(TEST_DIR, &st) == 0, "stat new dir");
    TEST_ASSERT_EQ(0700, (int) (st.st_mode & 0777), "a new recovery directory is 0700");
    cleanup();
}
#endif

int main(void) {
    TEST_INIT();

    RUN_TEST(session_list_reports_all_slots);
    RUN_TEST(session_take_reads_and_removes);
    RUN_TEST(session_take_twice_second_is_null);
    RUN_TEST(session_take_untitled);
    RUN_TEST(session_clear_slot_removes_it);
    RUN_TEST(session_take_missing_returns_null);
    RUN_TEST(session_handoff_unmarked_by_default);
    RUN_TEST(session_handoff_mark_and_read_back);
    RUN_TEST(session_handoff_is_per_slot);
    RUN_TEST(session_handoff_clear_reverts_to_crash);
    RUN_TEST(session_clear_slot_removes_handoff_marker);
    RUN_TEST(session_take_clears_handoff_marker);
    RUN_TEST(session_handoff_marker_is_not_a_slot);
    RUN_TEST(session_slot_id_shape);
    RUN_TEST(session_made_ids_are_valid_and_distinct);
    RUN_TEST(session_invalid_ids_touch_nothing);
    RUN_TEST(session_listing_skips_foreign_names);
    RUN_TEST(session_meta_round_trips_truncated_and_stamp);
    RUN_TEST(session_meta_reads_pre_036_format);
    RUN_TEST(session_meta_stays_readable_by_pre_036);
    RUN_TEST(session_claimed_slot_is_not_listed_and_only_one_wins);
    RUN_TEST(session_claim_handover);
    RUN_TEST(session_discard_claimed_removes_everything);
    RUN_TEST(session_read_of_a_slot_without_text_is_empty);
#ifndef _WIN32
    RUN_TEST(session_take_keeps_an_unreadable_slot);
#endif
    RUN_TEST(session_adopt_moves_the_slot_under_the_new_id);
    RUN_TEST(session_orphans_stale_claim_is_handed_back);
    RUN_TEST(session_orphans_old_taking_claims_come_back);
    RUN_TEST(session_orphans_text_without_meta_is_offered_untitled);
    RUN_TEST(session_orphans_empty_leftovers_are_removed);
#ifndef _WIN32
    RUN_TEST(session_files_are_private);
#endif

    TEST_SUMMARY();
    return 0;
}
