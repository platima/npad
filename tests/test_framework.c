/*
 * npad - Testing Framework Implementation
 * Lightweight unit testing framework for npad
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

// Global test statistics
test_stats_t g_test_stats = { 0 };

void test_create_temp_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("ERROR: Failed to create temp file: %s\n", filename);
        exit(1);
    }
    
    if (content) {
        fputs(content, file);
    }
    
    fclose(file);
}

void test_cleanup_temp_file(const char *filename) {
    if (test_file_exists(filename)) {
        remove(filename);
    }
}

bool test_file_exists(const char *filename) {
    return access(filename, F_OK) == 0;
}