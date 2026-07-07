/*
 * npad - Session Recovery Tests
 * Unit tests for crash-recovery snapshot read/write
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
    session_clear(TEST_DIR);
#ifdef _WIN32
    _rmdir(TEST_DIR);
#else
    rmdir(TEST_DIR);
#endif
}

TEST_CASE(session_roundtrip_with_path) {
    bool ok = session_write(TEST_DIR, "hello\nworld", "C:/tmp/note.txt", NPAD_ENC_UTF16_LE,
                            NPAD_EOL_LF);
    TEST_ASSERT(ok, "session_write should succeed");
    TEST_ASSERT(session_exists(TEST_DIR), "snapshot should exist after write");

    char *path = NULL;
    TextEncoding enc = NPAD_ENC_UTF8;
    LineEnding eol = NPAD_EOL_CRLF;
    char *content = session_read(TEST_DIR, &path, &enc, &eol);

    TEST_ASSERT_NOT_NULL(content, "session_read should return content");
    TEST_ASSERT_STR_EQ("hello\nworld", content, "content should round-trip");
    TEST_ASSERT_NOT_NULL(path, "path should round-trip");
    TEST_ASSERT_STR_EQ("C:/tmp/note.txt", path, "path should match");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, enc, "encoding should round-trip");
    TEST_ASSERT_EQ(NPAD_EOL_LF, eol, "line ending should round-trip");

    free(content);
    free(path);
    cleanup();
}

TEST_CASE(session_roundtrip_untitled) {
    bool ok = session_write(TEST_DIR, "draft text", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(ok, "session_write with NULL path should succeed");

    char *path = (char *) 0x1; // Poison to prove it is cleared
    TextEncoding enc;
    LineEnding eol;
    char *content = session_read(TEST_DIR, &path, &enc, &eol);

    TEST_ASSERT_NOT_NULL(content, "content should be read");
    TEST_ASSERT_STR_EQ("draft text", content, "content should match");
    TEST_ASSERT_NULL(path, "untitled document should report NULL path");

    free(content);
    cleanup();
}

TEST_CASE(session_clear_removes_snapshot) {
    session_write(TEST_DIR, "temp", NULL, NPAD_ENC_UTF8, NPAD_EOL_CRLF);
    TEST_ASSERT(session_exists(TEST_DIR), "snapshot should exist");

    session_clear(TEST_DIR);
    TEST_ASSERT(!session_exists(TEST_DIR), "snapshot should be gone after clear");

    cleanup();
}

TEST_CASE(session_read_missing_returns_null) {
    cleanup(); // Ensure nothing present
    TEST_ASSERT(!session_exists(TEST_DIR), "no snapshot should exist");

    char *path = NULL;
    TextEncoding enc;
    LineEnding eol;
    char *content = session_read(TEST_DIR, &path, &enc, &eol);
    TEST_ASSERT_NULL(content, "reading a missing snapshot should return NULL");
    TEST_ASSERT_NULL(path, "path should remain NULL");
}

int main(void) {
    TEST_INIT();

    RUN_TEST(session_roundtrip_with_path);
    RUN_TEST(session_roundtrip_untitled);
    RUN_TEST(session_clear_removes_snapshot);
    RUN_TEST(session_read_missing_returns_null);

    TEST_SUMMARY();
    return 0;
}
