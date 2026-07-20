/*
 * npad - File Operations Tests
 * Unit tests for file I/O operations
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include "../src/core/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test data
#define TEST_FILE "test_temp.txt"
#define TEST_CONTENT "Hello, npad testing!\nThis is a test file.\n"

TEST_CASE(file_read_text_valid) {
    // Create a test file
    test_create_temp_file(TEST_FILE, TEST_CONTENT);
    
    // Read the file
    char *content = file_read_text(TEST_FILE);
    
    // Verify content
    TEST_ASSERT_NOT_NULL(content, "file_read_text should return valid content");
    TEST_ASSERT_STR_EQ(TEST_CONTENT, content, "Content should match what was written");
    
    // Cleanup
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_read_text_null_filename) {
    // Test with NULL filename
    char *content = file_read_text(NULL);
    
    // Should return NULL
    TEST_ASSERT_NULL(content, "file_read_text should return NULL for NULL filename");
}

TEST_CASE(file_read_text_empty_filename) {
    // Test with empty filename
    char *content = file_read_text("");
    
    // Should return NULL
    TEST_ASSERT_NULL(content, "file_read_text should return NULL for empty filename");
}

TEST_CASE(file_read_text_nonexistent) {
    // Test with non-existent file
    char *content = file_read_text("nonexistent_file_xyz123.txt");
    
    // Should return NULL
    TEST_ASSERT_NULL(content, "file_read_text should return NULL for non-existent file");
}

TEST_CASE(file_write_text_valid) {
    // Write test content
    bool result = file_write_text(TEST_FILE, TEST_CONTENT);
    
    // Verify write succeeded
    TEST_ASSERT(result, "file_write_text should succeed");
    TEST_ASSERT(test_file_exists(TEST_FILE), "File should exist after writing");
    
    // Read back and verify
    char *content = file_read_text(TEST_FILE);
    TEST_ASSERT_NOT_NULL(content, "Should be able to read back written content");
    TEST_ASSERT_STR_EQ(TEST_CONTENT, content, "Read content should match written content");
    
    // Cleanup
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_write_text_null_filename) {
    // Test with NULL filename
    bool result = file_write_text(NULL, TEST_CONTENT);
    
    // Should fail
    TEST_ASSERT(!result, "file_write_text should fail with NULL filename");
}

TEST_CASE(file_write_text_null_content) {
    // Test with NULL content
    bool result = file_write_text(TEST_FILE, NULL);
    
    // Should fail
    TEST_ASSERT(!result, "file_write_text should fail with NULL content");
}

TEST_CASE(file_exists_valid) {
    // Create test file
    test_create_temp_file(TEST_FILE, TEST_CONTENT);
    
    // Test file_exists
    TEST_ASSERT(file_exists(TEST_FILE), "file_exists should return true for existing file");
    
    // Cleanup
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_exists_nonexistent) {
    // Test with non-existent file
    TEST_ASSERT(!file_exists("nonexistent_file_xyz123.txt"), 
                "file_exists should return false for non-existent file");
}

TEST_CASE(file_get_size_valid) {
    // Create test file
    test_create_temp_file(TEST_FILE, TEST_CONTENT);
    
    // Get file size
    size_t size = file_get_size(TEST_FILE);
    size_t expected_size = strlen(TEST_CONTENT);
    
    // Verify size
    TEST_ASSERT_EQ(expected_size, size, "file_get_size should return correct size");
    
    // Cleanup
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_looks_binary_text) {
    test_create_temp_file(TEST_FILE, "plain text\r\nwith tabs\tand lines\n");
    TEST_ASSERT(!file_looks_binary(TEST_FILE), "plain text should not look binary");
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_looks_binary_nul_bytes) {
    const char data[] = { 'M', 'Z', 0x00, 0x03, 'a', 'b' };
    TEST_ASSERT(file_write_binary(TEST_FILE, data, sizeof(data)),
                "writing the binary fixture should succeed");
    TEST_ASSERT(file_looks_binary(TEST_FILE), "NUL bytes should look binary");
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_looks_binary_utf16_bom_is_text) {
    // UTF-16 LE BOM followed by "hi" - full of NULs but valid text
    const char data[] = { (char) 0xFF, (char) 0xFE, 'h', 0x00, 'i', 0x00 };
    TEST_ASSERT(file_write_binary(TEST_FILE, data, sizeof(data)),
                "writing the UTF-16 fixture should succeed");
    TEST_ASSERT(!file_looks_binary(TEST_FILE), "BOM-marked UTF-16 is text, not binary");
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_looks_binary_control_soup) {
    // No NULs, but mostly control bytes (e.g. a compressed stream)
    char data[64];
    for (int i = 0; i < 64; i++)
        data[i] = (char) ((i % 2) ? 0x01 : 0x02);
    TEST_ASSERT(file_write_binary(TEST_FILE, data, sizeof(data)),
                "writing the control fixture should succeed");
    TEST_ASSERT(file_looks_binary(TEST_FILE), "control-byte soup should look binary");
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(file_looks_binary_empty_and_missing) {
    test_create_temp_file(TEST_FILE, "");
    TEST_ASSERT(!file_looks_binary(TEST_FILE), "empty file is not binary");
    test_cleanup_temp_file(TEST_FILE);
    TEST_ASSERT(!file_looks_binary("nonexistent_file_xyz123.bin"),
                "missing file is not binary");
    TEST_ASSERT(!file_looks_binary(NULL), "NULL path is not binary");
}

TEST_CASE(count_stats_basic) {
    size_t w, c, l;
    file_count_text_stats("hello world\r\nsecond line", &w, &c, &l);
    TEST_ASSERT_EQ((size_t) 4, w, "four words");
    TEST_ASSERT_EQ((size_t) 22, c, "22 characters excluding the break");
    TEST_ASSERT_EQ((size_t) 2, l, "two lines");
}

TEST_CASE(count_stats_empty_and_null) {
    size_t w, c, l;
    file_count_text_stats("", &w, &c, &l);
    TEST_ASSERT_EQ((size_t) 0, w, "empty text has no words");
    TEST_ASSERT_EQ((size_t) 0, c, "empty text has no characters");
    TEST_ASSERT_EQ((size_t) 1, l, "empty text is one line");
    file_count_text_stats(NULL, &w, &c, &l);
    TEST_ASSERT_EQ((size_t) 1, l, "NULL treated as empty");
}

TEST_CASE(count_stats_unicode_and_eols) {
    size_t w, c, l;
    // "café" (é = 2 UTF-8 bytes, 1 code point) + LF + CR + CRLF endings
    file_count_text_stats("caf\xC3\xA9 x\ny\rz\r\n", &w, &c, &l);
    TEST_ASSERT_EQ((size_t) 4, w, "four words across mixed line endings");
    TEST_ASSERT_EQ((size_t) 8, c, "code points counted once, breaks excluded");
    TEST_ASSERT_EQ((size_t) 4, l, "LF, CR and CRLF each end a line");
}

TEST_CASE(count_stats_whitespace_runs) {
    size_t w, c, l;
    file_count_text_stats("  one\t\ttwo   three  ", &w, &c, &l);
    TEST_ASSERT_EQ((size_t) 3, w, "whitespace runs separate words");
    TEST_ASSERT_EQ((size_t) 1, l, "single line");
    (void) c;
}

TEST_CASE(path_traversal_protection) {
    // Test path traversal attempts
    char *content1 = file_read_text("../../../etc/passwd");
    char *content2 = file_read_text("..\\..\\windows\\system32\\config\\sam");
    
    // Should be blocked
    TEST_ASSERT_NULL(content1, "Path traversal with forward slashes should be blocked");
    TEST_ASSERT_NULL(content2, "Path traversal with backslashes should be blocked");
}

int main(void) {
    // Initialize error system for testing
    npad_error_init();
    
    // Initialize test framework
    TEST_INIT();
    
    // Run file operation tests
    RUN_TEST(file_read_text_valid);
    RUN_TEST(file_read_text_null_filename);
    RUN_TEST(file_read_text_empty_filename);
    RUN_TEST(file_read_text_nonexistent);
    RUN_TEST(file_write_text_valid);
    RUN_TEST(file_write_text_null_filename);
    RUN_TEST(file_write_text_null_content);
    RUN_TEST(file_exists_valid);
    RUN_TEST(file_exists_nonexistent);
    RUN_TEST(file_get_size_valid);
    RUN_TEST(file_looks_binary_text);
    RUN_TEST(file_looks_binary_nul_bytes);
    RUN_TEST(file_looks_binary_utf16_bom_is_text);
    RUN_TEST(file_looks_binary_control_soup);
    RUN_TEST(file_looks_binary_empty_and_missing);
    RUN_TEST(count_stats_basic);
    RUN_TEST(count_stats_empty_and_null);
    RUN_TEST(count_stats_unicode_and_eols);
    RUN_TEST(count_stats_whitespace_runs);
    RUN_TEST(path_traversal_protection);
    
    // Cleanup error system
    npad_error_cleanup();
    
    // Show test summary and exit
    TEST_SUMMARY();
    
    return 0;
}