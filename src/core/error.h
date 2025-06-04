/*
 * npad - Error Handling System
 * Centralized error reporting and logging
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stddef.h>

// Error severity levels
typedef enum {
    NPAD_ERROR_INFO,
    NPAD_ERROR_WARNING,
    NPAD_ERROR_ERROR,
    NPAD_ERROR_FATAL
} npad_error_level_t;

// Error categories
typedef enum {
    NPAD_ERROR_NONE,
    NPAD_ERROR_MEMORY,
    NPAD_ERROR_FILE_IO,
    NPAD_ERROR_NETWORK,
    NPAD_ERROR_INVALID_PARAM,
    NPAD_ERROR_SYSTEM,
    NPAD_ERROR_UI,
    NPAD_ERROR_EDITOR,
    NPAD_ERROR_SETTINGS,
    NPAD_ERROR_THREAD,
    NPAD_ERROR_UNKNOWN
} npad_error_category_t;

// Error information structure
typedef struct {
    npad_error_level_t level;
    npad_error_category_t category;
    int code;
    char message[512];
    char context[256];
    char file[64];
    int line;
    char function[64];
} npad_error_info_t;

// Error callback function type
typedef void (*npad_error_callback_t)(const npad_error_info_t *error);

// Core error functions
void npad_error_init(void);
void npad_error_cleanup(void);
void npad_error_set_callback(npad_error_callback_t callback);

// Error reporting functions
void npad_error_report(npad_error_level_t level, npad_error_category_t category, int code,
                       const char *file, int line, const char *function, const char *context,
                       const char *format, ...);

// Convenience macros for error reporting
#define NPAD_ERROR_INFO(category, code, context, ...)                                              \
    npad_error_report(NPAD_ERROR_INFO, category, code, __FILE__, __LINE__, __func__, context,      \
                      __VA_ARGS__)

#define NPAD_ERROR_WARNING(category, code, context, ...)                                           \
    npad_error_report(NPAD_ERROR_WARNING, category, code, __FILE__, __LINE__, __func__, context,   \
                      __VA_ARGS__)

#define NPAD_ERROR_ERROR(category, code, context, ...)                                             \
    npad_error_report(NPAD_ERROR_ERROR, category, code, __FILE__, __LINE__, __func__, context,     \
                      __VA_ARGS__)

#define NPAD_ERROR_FATAL(category, code, context, ...)                                             \
    npad_error_report(NPAD_ERROR_FATAL, category, code, __FILE__, __LINE__, __func__, context,     \
                      __VA_ARGS__)

// Specialized macros for common error types
#define NPAD_ERROR_MEMORY_ALLOC(context)                                                           \
    NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, context, "Memory allocation failed")

#define NPAD_ERROR_INVALID_PARAM(param_name)                                                       \
    NPAD_ERROR_ERROR(NPAD_ERROR_INVALID_PARAM, 0, param_name, "Invalid parameter: %s", param_name)

#define NPAD_ERROR_FILE_IO(operation, filename)                                                    \
    NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "File %s failed: %s", operation,         \
                     strerror(errno))

// Error state functions
const npad_error_info_t *npad_error_get_last(void);
bool npad_error_has_error(void);
void npad_error_clear(void);

// Error string conversion
const char *npad_error_level_to_string(npad_error_level_t level);
const char *npad_error_category_to_string(npad_error_category_t category);

#endif // ERROR_H