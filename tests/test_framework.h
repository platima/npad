/*
 * npad - Testing Framework
 * Lightweight unit testing framework for npad
 *
 * Author: Platima
 * https://github.com/platima/npad
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

// Test framework macros
#define TEST_INIT() do { \
    memset(&g_test_stats, 0, sizeof(g_test_stats)); \
    printf("npad Test Suite\n"); \
    printf("===============\n\n"); \
} while(0)

#define TEST_CASE(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("Running: %s ... ", #name); \
        fflush(stdout); \
        g_test_stats.tests_run++; \
        test_##name(); \
        g_test_stats.tests_passed++; \
        printf("PASSED\n"); \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

#define TEST_ASSERT(condition, message) do { \
    g_test_stats.assertions_run++; \
    if (!(condition)) { \
        g_test_stats.assertions_failed++; \
        g_test_stats.tests_failed++; \
        g_test_stats.tests_passed--; \
        printf("FAILED\n"); \
        printf("  Assertion failed: %s\n", message); \
        printf("  File: %s, Line: %d\n", __FILE__, __LINE__); \
        return; \
    } \
    g_test_stats.assertions_passed++; \
} while(0)

#define TEST_ASSERT_EQ(expected, actual, message) do { \
    g_test_stats.assertions_run++; \
    if ((expected) != (actual)) { \
        g_test_stats.assertions_failed++; \
        g_test_stats.tests_failed++; \
        g_test_stats.tests_passed--; \
        printf("FAILED\n"); \
        printf("  Assertion failed: %s\n", message); \
        printf("  Expected: %ld, Actual: %ld\n", (long)(expected), (long)(actual)); \
        printf("  File: %s, Line: %d\n", __FILE__, __LINE__); \
        return; \
    } \
    g_test_stats.assertions_passed++; \
} while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) do { \
    g_test_stats.assertions_run++; \
    if (!expected || !actual || strcmp(expected, actual) != 0) { \
        g_test_stats.assertions_failed++; \
        g_test_stats.tests_failed++; \
        g_test_stats.tests_passed--; \
        printf("FAILED\n"); \
        printf("  Assertion failed: %s\n", message); \
        printf("  Expected: \"%s\", Actual: \"%s\"\n", \
               expected ? expected : "(null)", \
               actual ? actual : "(null)"); \
        printf("  File: %s, Line: %d\n", __FILE__, __LINE__); \
        return; \
    } \
    g_test_stats.assertions_passed++; \
} while(0)

#define TEST_ASSERT_NULL(ptr, message) do { \
    g_test_stats.assertions_run++; \
    if ((ptr) != NULL) { \
        g_test_stats.assertions_failed++; \
        g_test_stats.tests_failed++; \
        g_test_stats.tests_passed--; \
        printf("FAILED\n"); \
        printf("  Assertion failed: %s\n", message); \
        printf("  Expected NULL, got non-NULL pointer\n"); \
        printf("  File: %s, Line: %d\n", __FILE__, __LINE__); \
        return; \
    } \
    g_test_stats.assertions_passed++; \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) do { \
    g_test_stats.assertions_run++; \
    if ((ptr) == NULL) { \
        g_test_stats.assertions_failed++; \
        g_test_stats.tests_failed++; \
        g_test_stats.tests_passed--; \
        printf("FAILED\n"); \
        printf("  Assertion failed: %s\n", message); \
        printf("  Expected non-NULL, got NULL pointer\n"); \
        printf("  File: %s, Line: %d\n", __FILE__, __LINE__); \
        return; \
    } \
    g_test_stats.assertions_passed++; \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\nTest Summary\n"); \
    printf("============\n"); \
    printf("Tests run:    %d\n", g_test_stats.tests_run); \
    printf("Tests passed: %d\n", g_test_stats.tests_passed); \
    printf("Tests failed: %d\n", g_test_stats.tests_failed); \
    printf("Assertions:   %d/%d passed\n", \
           g_test_stats.assertions_passed, g_test_stats.assertions_run); \
    printf("\n"); \
    if (g_test_stats.tests_failed > 0) { \
        printf("SOME TESTS FAILED!\n"); \
        exit(1); \
    } else { \
        printf("ALL TESTS PASSED!\n"); \
        exit(0); \
    } \
} while(0)

// Helper functions
void test_create_temp_file(const char *filename, const char *content);
void test_cleanup_temp_file(const char *filename);
bool test_file_exists(const char *filename);

#endif // TEST_FRAMEWORK_H