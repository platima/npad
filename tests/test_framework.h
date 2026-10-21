/*
 * npad - Testing Framework
 * Lightweight unit testing framework for npad
 *
 * Every suite runs in a private, freshly created temporary directory:
 * TEST_INIT() creates it, makes it the working directory (so relative fixture
 * names never land in the checkout), and points the settings location at it
 * before any test runs - on Linux HOME and XDG_CONFIG_HOME, on Windows
 * NPAD_TEST_CONFIG_DIR (honoured by builds compiled with -DNPAD_TEST_HOOKS).
 * The directory is removed when the suite exits, so a run that was aborted
 * half way cannot leave anything behind that breaks the next one. Set
 * NPAD_TEST_KEEP=1 to keep it for inspection (its path is printed).
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test statistics
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    int assertions_run;
    int assertions_passed;
    int assertions_failed;
} test_stats_t;

// Global test statistics
extern test_stats_t g_test_stats;

// Set by any failed assertion during the current test case. A test is counted
// (and reported) once: as PASSED when this is still false at its end, as
// FAILED otherwise. Assertions in helper functions mark the test too, even
// though their `return` only leaves the helper.
extern bool g_test_current_failed;

// Create the suite's temporary working directory and isolate the settings
// location (see the header comment). Exits the process if that fails: running
// on without isolation could touch the user's real settings.
void test_framework_begin(void);

// The suite's temporary directory (absolute, UTF-8), or NULL before
// test_framework_begin.
const char *test_work_dir(void);

// Record a failed assertion: marks the current test failed and prints the
// FAILED line for it once, followed by the message and location.
void test_report_failure(const char *message, const char *file, int line);

// The detail line for a failed TEST_ASSERT_EQ (both operands, as evaluated)
void test_report_eq_values(long long expected, long long actual);

// Run one test case: counts it and prints exactly one verdict
void test_run_case(const char *name, void (*fn)(void));

// Print the summary and exit: 0 when every test passed, 1 otherwise
void test_finish(void);

// Test framework macros
#define TEST_INIT()                                                                                \
    do {                                                                                           \
        memset(&g_test_stats, 0, sizeof(g_test_stats));                                            \
        test_framework_begin();                                                                    \
        printf("npad Test Suite\n");                                                               \
        printf("===============\n\n");                                                             \
    } while (0)

#define TEST_CASE(name)                                                                            \
    static void test_##name(void);                                                                 \
    static void run_test_##name(void) {                                                            \
        test_run_case(#name, test_##name);                                                         \
    }                                                                                              \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        g_test_stats.assertions_run++;                                                             \
        if (!(condition)) {                                                                        \
            test_report_failure((message), __FILE__, __LINE__);                                    \
            return;                                                                                \
        }                                                                                          \
        g_test_stats.assertions_passed++;                                                          \
    } while (0)

// Each operand is evaluated exactly once (so a call with side effects, such as
// a registry write, never runs twice), and compared as a long long so neither
// a 64-bit size nor a negative int is truncated or reinterpreted on the way.
#define TEST_ASSERT_EQ(expected, actual, message)                                                  \
    do {                                                                                           \
        const long long test_eq_expected_ = (long long) (expected);                                \
        const long long test_eq_actual_ = (long long) (actual);                                    \
        g_test_stats.assertions_run++;                                                             \
        if (test_eq_expected_ != test_eq_actual_) {                                                \
            test_report_failure((message), __FILE__, __LINE__);                                    \
            test_report_eq_values(test_eq_expected_, test_eq_actual_);                             \
            return;                                                                                \
        }                                                                                          \
        g_test_stats.assertions_passed++;                                                          \
    } while (0)

#define TEST_ASSERT_STR_EQ(expected, actual, message)                                              \
    do {                                                                                           \
        const char *test_str_expected_ = (expected);                                               \
        const char *test_str_actual_ = (actual);                                                   \
        g_test_stats.assertions_run++;                                                             \
        if (!test_str_expected_ || !test_str_actual_ ||                                            \
            strcmp(test_str_expected_, test_str_actual_) != 0) {                                   \
            test_report_failure((message), __FILE__, __LINE__);                                    \
            printf("  Expected: \"%s\", Actual: \"%s\"\n",                                         \
                   test_str_expected_ ? test_str_expected_ : "(null)",                             \
                   test_str_actual_ ? test_str_actual_ : "(null)");                                \
            return;                                                                                \
        }                                                                                          \
        g_test_stats.assertions_passed++;                                                          \
    } while (0)

#define TEST_ASSERT_NULL(ptr, message)                                                             \
    do {                                                                                           \
        const void *test_ptr_ = (ptr);                                                             \
        g_test_stats.assertions_run++;                                                             \
        if (test_ptr_ != NULL) {                                                                   \
            test_report_failure((message), __FILE__, __LINE__);                                    \
            printf("  Expected NULL, got non-NULL pointer\n");                                     \
            return;                                                                                \
        }                                                                                          \
        g_test_stats.assertions_passed++;                                                          \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr, message)                                                         \
    do {                                                                                           \
        const void *test_ptr_ = (ptr);                                                             \
        g_test_stats.assertions_run++;                                                             \
        if (test_ptr_ == NULL) {                                                                   \
            test_report_failure((message), __FILE__, __LINE__);                                    \
            printf("  Expected non-NULL, got NULL pointer\n");                                     \
            return;                                                                                \
        }                                                                                          \
        g_test_stats.assertions_passed++;                                                          \
    } while (0)

#define TEST_SUMMARY() test_finish()

// Helper functions. Relative names resolve inside the suite's temporary
// working directory.
//
// Writes content byte for byte (binary mode, so no CRLF translation on
// Windows); exits the process if the file cannot be created.
void test_create_temp_file(const char *filename, const char *content);
void test_cleanup_temp_file(const char *filename);
bool test_file_exists(const char *filename);

// Create one directory level; true when it exists afterwards
bool test_make_dir(const char *path);

// Remove a file or a directory tree. Symbolic links (and Windows reparse
// points) are removed, never followed. Missing is success.
bool test_remove_tree(const char *path);

#endif // TEST_FRAMEWORK_H
