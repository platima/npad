/*
 * npad - Encoding and Line Ending Tests
 * Unit tests for text encoding detection/conversion and line ending handling
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FILE "test_encoding_temp.txt"

// Write raw bytes to the test file
static void write_bytes(const char *filename, const void *data, size_t size) {
    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

// Read raw bytes back; returns malloc'd buffer, sets *size
static unsigned char *read_bytes(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t) len + 1);
    if (buf) {
        *size = fread(buf, 1, (size_t) len, f);
        buf[*size] = '\0';
    }
    fclose(f);
    return buf;
}

TEST_CASE(detect_line_ending_crlf) {
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, file_detect_line_ending("one\r\ntwo\r\nthree"),
                   "CRLF text should detect as CRLF");
}

TEST_CASE(detect_line_ending_lf) {
    TEST_ASSERT_EQ(NPAD_EOL_LF, file_detect_line_ending("one\ntwo\nthree"),
                   "LF text should detect as LF");
}

TEST_CASE(detect_line_ending_cr) {
    TEST_ASSERT_EQ(NPAD_EOL_CR, file_detect_line_ending("one\rtwo\rthree"),
                   "CR text should detect as CR");
}

TEST_CASE(detect_line_ending_no_breaks) {
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, file_detect_line_ending("no line breaks"),
                   "Text without line breaks should default to CRLF");
}

TEST_CASE(detect_line_ending_mixed_majority) {
    TEST_ASSERT_EQ(NPAD_EOL_LF, file_detect_line_ending("a\nb\nc\nd\r\ne"),
                   "Majority LF should win over minority CRLF");
}

TEST_CASE(convert_line_endings_to_lf) {
    char *converted = file_convert_line_endings("one\r\ntwo\rthree\nfour", NPAD_EOL_LF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\ntwo\nthree\nfour", converted,
                       "All line ending styles should convert to LF");
    free(converted);
}

TEST_CASE(convert_line_endings_to_crlf) {
    char *converted = file_convert_line_endings("one\ntwo\rthree", NPAD_EOL_CRLF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\r\ntwo\r\nthree", converted,
                       "All line ending styles should convert to CRLF");
    free(converted);
}

TEST_CASE(convert_line_endings_idempotent) {
    char *converted = file_convert_line_endings("one\r\ntwo\r\n", NPAD_EOL_CRLF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\r\ntwo\r\n", converted, "CRLF to CRLF should be unchanged");
    free(converted);
}

TEST_CASE(read_utf8_plain) {
    write_bytes(TEST_FILE, "plain ascii\r\n", 13);

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("plain ascii\r\n", content, "Content should round-trip");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, info.encoding, "Plain ASCII should detect as UTF-8");
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, info.line_ending, "Line ending should be CRLF");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf8_bom) {
    const unsigned char data[] = { 0xEF, 0xBB, 0xBF, 'h', 'i', '\n' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("hi\n", content, "BOM should be stripped from content");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8_BOM, info.encoding, "UTF-8 BOM should be detected");
    TEST_ASSERT_EQ(NPAD_EOL_LF, info.line_ending, "Line ending should be LF");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf16_le_bom) {
    // "Hi\r\n" in UTF-16 LE with BOM
    const unsigned char data[] = { 0xFF, 0xFE, 'H', 0, 'i', 0, '\r', 0, '\n', 0 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("Hi\r\n", content, "UTF-16 LE should convert to UTF-8");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, info.encoding, "UTF-16 LE BOM should be detected");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf16_be_bom) {
    // "Hi" in UTF-16 BE with BOM
    const unsigned char data[] = { 0xFE, 0xFF, 0, 'H', 0, 'i' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("Hi", content, "UTF-16 BE should convert to UTF-8");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_BE, info.encoding, "UTF-16 BE BOM should be detected");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf8_multibyte) {
    // "héllo" in UTF-8 (é = 0xC3 0xA9)
    const unsigned char data[] = { 'h', 0xC3, 0xA9, 'l', 'l', 'o' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, info.encoding, "Valid multi-byte UTF-8 should detect as UTF-8");
    TEST_ASSERT_EQ(6, (long) strlen(content), "UTF-8 bytes should be preserved");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(roundtrip_utf16_le) {
    // Write a UTF-16 LE file, read it, save it back, verify identical bytes
    const unsigned char data[] = { 0xFF, 0xFE, 'A', 0, '\r', 0, '\n', 0, 'B', 0 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");

    bool written = file_write_text_ex(TEST_FILE, content, &info);
    TEST_ASSERT(written, "Write should succeed");
    free(content);

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(sizeof(data), size, "Round-tripped file should have identical size");
    TEST_ASSERT_EQ(0, memcmp(raw, data, sizeof(data)), "Round-tripped bytes should be identical");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(detect_and_roundtrip_ansi_high_byte) {
    // A byte outside ASCII that is not valid UTF-8 marks the file as ANSI
    // (Windows code page; Latin-1 on other platforms). Saving it back as ANSI
    // must reproduce the original byte, so a "save as ANSI" / reopen keeps ANSI
    // rather than reverting to UTF-8. (Pure-ASCII files stay UTF-8 by design,
    // since ANSI and UTF-8 are byte-identical for ASCII.)
    const unsigned char data[] = { 'c', 'a', 'f', 0xE9 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_ENC_ANSI, info.encoding, "High-byte non-UTF-8 text should detect as ANSI");

    bool written = file_write_text_ex(TEST_FILE, content, &info);
    TEST_ASSERT(written, "Write should succeed");
    free(content);

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(sizeof(data), size, "Round-tripped ANSI file should have identical size");
    TEST_ASSERT_EQ(0, memcmp(raw, data, sizeof(data)), "Round-tripped ANSI bytes should match");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(roundtrip_lf_preserved) {
    // An LF file saved with CRLF content from the editor keeps LF on disk
    write_bytes(TEST_FILE, "one\ntwo\n", 8);

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_EOL_LF, info.line_ending, "LF should be detected");
    free(content);

    // The editor control hands back CRLF; the writer must convert to LF
    bool written = file_write_text_ex(TEST_FILE, "one\r\ntwo\r\nthree\r\n", &info);
    TEST_ASSERT(written, "Write should succeed");

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_STR_EQ("one\ntwo\nthree\n", (char *) raw,
                       "Saved file should use the original LF endings");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(write_failure_preserves_original) {
    // A failed write must never delete the existing file
    write_bytes(TEST_FILE, "original", 8);

    // Writing to an invalid path must fail cleanly...
    bool result = file_write_text("", "new content");
    TEST_ASSERT(!result, "Write to empty path should fail");

    // ...and the unrelated original file is intact
    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Original file should still exist");
    TEST_ASSERT_STR_EQ("original", (char *) raw, "Original content should be intact");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(encoding_names) {
    TEST_ASSERT_STR_EQ("UTF-8", file_encoding_name(NPAD_ENC_UTF8), "UTF-8 name");
    TEST_ASSERT_STR_EQ("UTF-16 LE", file_encoding_name(NPAD_ENC_UTF16_LE), "UTF-16 LE name");
    TEST_ASSERT_STR_EQ("Windows (CRLF)", file_line_ending_name(NPAD_EOL_CRLF), "CRLF name");
    TEST_ASSERT_STR_EQ("Unix (LF)", file_line_ending_name(NPAD_EOL_LF), "LF name");
}

TEST_CASE(open_relative_parent_path) {
    // Paths containing ".." are legitimate for a desktop editor and must
    // not be rejected by path validation (regression test: earlier
    // versions blocked every path containing "..")
    write_bytes(TEST_FILE, "content", 7);

    char *content = file_read_text("tests/../" TEST_FILE);
    TEST_ASSERT_NOT_NULL(content, "Reading via a path containing .. should work");
    TEST_ASSERT_STR_EQ("content", content, "Content should match");
    free(content);

    test_cleanup_temp_file(TEST_FILE);
}

int main(void) {
    TEST_INIT();

    RUN_TEST(detect_line_ending_crlf);
    RUN_TEST(detect_line_ending_lf);
    RUN_TEST(detect_line_ending_cr);
    RUN_TEST(detect_line_ending_no_breaks);
    RUN_TEST(detect_line_ending_mixed_majority);
    RUN_TEST(convert_line_endings_to_lf);
    RUN_TEST(convert_line_endings_to_crlf);
    RUN_TEST(convert_line_endings_idempotent);
    RUN_TEST(read_utf8_plain);
    RUN_TEST(read_utf8_bom);
    RUN_TEST(read_utf16_le_bom);
    RUN_TEST(read_utf16_be_bom);
    RUN_TEST(read_utf8_multibyte);
    RUN_TEST(roundtrip_utf16_le);
    RUN_TEST(detect_and_roundtrip_ansi_high_byte);
    RUN_TEST(roundtrip_lf_preserved);
    RUN_TEST(write_failure_preserves_original);
    RUN_TEST(encoding_names);
    RUN_TEST(open_relative_parent_path);

    TEST_SUMMARY();
    return 0;
}
