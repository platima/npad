/*
 * npad - Error System Tests
 * Unit tests for error reporting and handling
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include "../src/core/error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom error callback for testing
static npad_error_info_t last_error = { 0 };
static bool error_callback_called = false;

static void test_error_callback(const npad_error_info_t *error) {
    if (error) {
        last_error = *error;
        error_callback_called = true;
    }
}

TEST_CASE(error_init_cleanup) {
    // Test initialization and cleanup
    npad_error_init();
    npad_error_cleanup();
    
    // Should not crash
    TEST_ASSERT(true, "Error system init/cleanup should not crash");
}

TEST_CASE(error_level_to_string) {
    // Test error level string conversion
    TEST_ASSERT_STR_EQ("INFO", npad_error_level_to_string(NPAD_ERROR_INFO), 
                       "INFO level should convert correctly");
    TEST_ASSERT_STR_EQ("WARN", npad_error_level_to_string(NPAD_ERROR_WARNING), 
                       "WARNING level should convert correctly");
    TEST_ASSERT_STR_EQ("ERROR", npad_error_level_to_string(NPAD_ERROR_ERROR), 
                       "ERROR level should convert correctly");
    TEST_ASSERT_STR_EQ("FATAL", npad_error_level_to_string(NPAD_ERROR_FATAL), 
                       "FATAL level should convert correctly");
}

TEST_CASE(error_category_to_string) {
    // Test error category string conversion
    TEST_ASSERT_STR_EQ("MEMORY", npad_error_category_to_string(NPAD_ERROR_MEMORY), 
                       "MEMORY category should convert correctly");
    TEST_ASSERT_STR_EQ("FILE_IO", npad_error_category_to_string(NPAD_ERROR_FILE_IO), 
                       "FILE_IO category should convert correctly");
    TEST_ASSERT_STR_EQ("INVALID_PARAM", npad_error_category_to_string(NPAD_ERROR_INVALID_PARAM), 
                       "INVALID_PARAM category should convert correctly");
}

TEST_CASE(error_reporting_basic) {
    npad_error_init();
    npad_error_set_callback(test_error_callback);
    
    // Reset test state
    error_callback_called = false;
    memset(&last_error, 0, sizeof(last_error));
    
    // Report an error
    NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, 12, "test context", "Test error message: %d", 42);
    
    // Verify callback was called
    TEST_ASSERT(error_callback_called, "Error callback should be called");
    TEST_ASSERT_EQ(NPAD_ERROR_ERROR, last_error.level, "Error level should be correct");
    TEST_ASSERT_EQ(NPAD_ERROR_MEMORY, last_error.category, "Error category should be correct");
    TEST_ASSERT_EQ(12, last_error.code, "Error code should be correct");
    TEST_ASSERT_STR_EQ("test context", last_error.context, "Error context should be correct");
    
    npad_error_cleanup();
}

TEST_CASE(error_state_management) {
    npad_error_init();
    
    // Clear any previous errors
    npad_error_clear();
    
    // Initially should have no error
    TEST_ASSERT(!npad_error_has_error(), "Should not have error initially");
    
    // Report an error
    NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, 5, "test", "Test error");
    
    // Should have error now
    TEST_ASSERT(npad_error_has_error(), "Should have error after reporting");
    
    const npad_error_info_t *error = npad_error_get_last();
    TEST_ASSERT_NOT_NULL(error, "Should be able to get last error");
    TEST_ASSERT_EQ(NPAD_ERROR_FILE_IO, error->category, "Error category should be correct");
    
    // Clear error
    npad_error_clear();
    TEST_ASSERT(!npad_error_has_error(), "Should not have error after clearing");
    
    npad_error_cleanup();
}

TEST_CASE(error_macros) {
    npad_error_init();
    npad_error_set_callback(test_error_callback);
    
    // Test convenience macros
    error_callback_called = false;
    NPAD_ERROR_MEMORY_ALLOC("test allocation");
    TEST_ASSERT(error_callback_called, "Memory allocation error macro should work");
    TEST_ASSERT_EQ(NPAD_ERROR_MEMORY, last_error.category, "Should use MEMORY category");
    
    error_callback_called = false;
    NPAD_ERROR_INVALID_PARAM("test_param");
    TEST_ASSERT(error_callback_called, "Invalid param error macro should work");
    TEST_ASSERT_EQ(NPAD_ERROR_INVALID_PARAM, last_error.category, "Should use INVALID_PARAM category");
    
    npad_error_cleanup();
}

int main(void) {
    // Initialize test framework
    TEST_INIT();
    
    // Run error system tests
    RUN_TEST(error_init_cleanup);
    RUN_TEST(error_level_to_string);
    RUN_TEST(error_category_to_string);
    RUN_TEST(error_reporting_basic);
    RUN_TEST(error_state_management);
    RUN_TEST(error_macros);
    
    // Show test summary and exit
    TEST_SUMMARY();
    
    return 0;
}