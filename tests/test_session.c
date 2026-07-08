/*
 * npad - Session Recovery Tests
 * Unit tests for per-slot crash-recovery snapshots
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/session.h"
#include "../src/core/file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DIR "test_session_dir"

static void cleanup(void) {
    session_clear_slot(TEST_DIR, "slotA");
    session_clear_slot(TEST_DIR, "slotB");
#ifdef _WIN32
    _rmdir(TEST_DIR);
#else
    rmdir(TEST_DIR);
#endif
}

static bool slots_contains(char **slots, int count, const char *id) {
    for (int i = 0; i < count; i++) {
        if (strcmp(slots[i], id) == 0)
            return true;
    }
    return false;
}

TEST_CASE(session_list_reports_all_slots) {
    TEST_ASSERT(session_write(TEST_DIR, "slotA", "content A", "C:/a.txt", NPAD_ENC_UTF8,
                              NPAD_EOL_CRLF),
                "write slotA");
    TEST_ASSERT(session_write(TEST_DIR, "slotB", "content B", NULL, NPAD_ENC_UTF16_LE, NPAD_EOL_LF),
                "write slotB");

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(2, count, "two slots should be listed");
    TEST_ASSERT(slots_contains(slots, count, "slotA"), "slotA present");
    TEST_ASSERT(slots_contains(slots, count, "slotB"), "slotB present");
    session_free_slots(slots, count);

    cleanup();
}

TEST_CASE(session_take_reads_and_removes) {
    session_write(TEST_DIR, "slotA", "hello\nworld", "C:/note.txt", NPAD_ENC_UTF16_LE,
                  NPAD_EOL_LF);

    char *path = NULL;
    TextEncoding enc = NPAD_ENC_UTF8;
    LineEnding eol = NPAD_EOL_CRLF;
    char *content = session_take(TEST_DIR, "slotA", &path, &enc, &eol);

    TEST_ASSERT_NOT_NULL(content, "take returns content");
    TEST_ASSERT_STR_EQ("hello\nworld", content, "content round-trips");
    TEST_ASSERT_NOT_NULL(path, "path round-trips");
    TEST_ASSERT_STR_EQ("C:/note.txt", path, "path matches");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, enc, "encoding round-trips");
    TEST_ASSERT_EQ(NPAD_EOL_LF, eol, "line ending round-trips");
    free(content);
    free(path);

    // Slot is gone after taking it
    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(0, count, "no slots remain after take");
    session_free_slots(slots, count);

    cleanup();
}

TEST_CASE(session_take_twice_second_is_null) {
    session_write(TEST_DIR, "slotA", "once", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    char *first = session_take(TEST_DIR, "slotA", NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(first, "first take succeeds");
    free(first);

    char *second = session_take(TEST_DIR, "slotA", NULL, NULL, NULL);
    TEST_ASSERT_NULL(second, "second take of the same slot returns NULL");

    cleanup();
}

TEST_CASE(session_take_untitled) {
    session_write(TEST_DIR, "slotA", "draft", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    char *path = (char *) 0x1; // Poison to prove it is cleared
    char *content = session_take(TEST_DIR, "slotA", &path, NULL, NULL);
    TEST_ASSERT_NOT_NULL(content, "content read");
    TEST_ASSERT_STR_EQ("draft", content, "content matches");
    TEST_ASSERT_NULL(path, "untitled slot reports NULL path");
    free(content);

    cleanup();
}

TEST_CASE(session_clear_slot_removes_it) {
    session_write(TEST_DIR, "slotA", "x", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    session_write(TEST_DIR, "slotB", "y", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);

    session_clear_slot(TEST_DIR, "slotA");

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(1, count, "one slot remains after clearing slotA");
    TEST_ASSERT(slots_contains(slots, count, "slotB"), "slotB still present");
    session_free_slots(slots, count);

    cleanup();
}

TEST_CASE(session_take_missing_returns_null) {
    cleanup();
    char *content = session_take(TEST_DIR, "nope", NULL, NULL, NULL);
    TEST_ASSERT_NULL(content, "taking a missing slot returns NULL");

    int count = 0;
    char **slots = session_list_slots(TEST_DIR, &count);
    TEST_ASSERT_EQ(0, count, "no slots listed");
    session_free_slots(slots, count);
}

int main(void) {
    TEST_INIT();

    RUN_TEST(session_list_reports_all_slots);
    RUN_TEST(session_take_reads_and_removes);
    RUN_TEST(session_take_twice_second_is_null);
    RUN_TEST(session_take_untitled);
    RUN_TEST(session_clear_slot_removes_it);
    RUN_TEST(session_take_missing_returns_null);

    TEST_SUMMARY();
    return 0;
}
