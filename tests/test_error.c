/*
 * npad - Error System Tests
 * Unit tests for error reporting and handling
 *
 * Author: Platima
 * https://npad.dev
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
    // Initialised: the last-error record is available and starts clear
    npad_error_init();
    TEST_ASSERT_NOT_NULL(npad_error_get_last(), "last error is available once initialised");
    TEST_ASSERT(!npad_error_has_error(), "and no error is pending");

    // Cleaned up: nothing is reported until the next init
    npad_error_cleanup();
    TEST_ASSERT_NULL(npad_error_get_last(), "no last error after cleanup");
    TEST_ASSERT(!npad_error_has_error(), "no pending error after cleanup");

    // And it can be initialised again
    npad_error_init();
    TEST_ASSERT_NOT_NULL(npad_error_get_last(), "re-initialisation works");
    npad_error_cleanup();
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

// FATAL used to abort() inside the default callback, so main()'s cleanup after
// a fatal report never ran. Reaching the assertion is the test.
TEST_CASE(fatal_report_returns_to_caller) {
    npad_error_init();
    npad_error_set_callback(NULL); // The default stderr logger
    NPAD_ERROR_FATAL(NPAD_ERROR_UI, 0, "test", "Expected fatal report (test output)");
    TEST_ASSERT_EQ(NPAD_ERROR_FATAL, npad_error_get_last()->level, "fatal recorded, not aborted");
    npad_error_cleanup();
}

// Callers report a failure and then describe it from errno; logging (which
// writes to a stream that may not exist) must not replace it
TEST_CASE(report_preserves_errno) {
    npad_error_init();
    npad_error_set_callback(NULL);
    errno = ENOENT;
    NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, ENOENT, "test", "Expected error report (test output)");
    TEST_ASSERT_EQ(ENOENT, errno, "errno unchanged by the default logger");

    npad_error_set_callback(test_error_callback);
    errno = EACCES;
    NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, EACCES, "test", "quiet");
    TEST_ASSERT_EQ(EACCES, errno, "errno unchanged with a callback");
    npad_error_cleanup();
}

// A GUI callback shows a message box, and code run from its message loop may
// report too. The callback runs outside the lock, so that cannot deadlock (a
// pthread mutex is not recursive; this test hung before).
static int g_nested_reports = 0;
static void reporting_callback(const npad_error_info_t *error) {
    (void) error;
    if (g_nested_reports++ == 0)
        NPAD_ERROR_WARNING(NPAD_ERROR_UI, 0, "nested", "Reported from inside a callback");
}

TEST_CASE(callback_may_report_again) {
    npad_error_init();
    npad_error_set_callback(reporting_callback);
    g_nested_reports = 0;
    NPAD_ERROR_ERROR(NPAD_ERROR_UI, 0, "outer", "Outer report");
    TEST_ASSERT_EQ(2, g_nested_reports, "outer and nested reports both delivered");
    TEST_ASSERT_STR_EQ("nested", npad_error_get_last()->context, "the nested one is the last");
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
    RUN_TEST(fatal_report_returns_to_caller);
    RUN_TEST(report_preserves_errno);
    RUN_TEST(callback_may_report_again);

    // Show test summary and exit
    TEST_SUMMARY();
    
    return 0;
}