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
    RUN_TEST(path_traversal_protection);
    
    // Cleanup error system
    npad_error_cleanup();
    
    // Show test summary and exit
    TEST_SUMMARY();
    
    return 0;
}