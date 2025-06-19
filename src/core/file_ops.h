/*
 * npad - File Operations
 * Cross-platform file I/O operations
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <stdbool.h>
#include <stddef.h>

// File reading operations
char *file_read_text(const char *filename);
bool file_read_binary(const char *filename, void **data, size_t *size);

// File writing operations
bool file_write_text(const char *filename, const char *content);
bool file_write_binary(const char *filename, const void *data, size_t size);

// Atomic file operations with rollback capability
bool file_write_text_atomic(const char *filename, const char *content);
bool file_write_binary_atomic(const char *filename, const void *data, size_t size);

// Enhanced validation and checks
bool file_check_disk_space(const char *path, size_t required_bytes);
bool file_validate_permissions(const char *filename, bool for_writing);
bool file_verify_integrity(const char *filename, const char *expected_content);

// File utility functions
bool file_exists(const char *filename);
bool file_is_readable(const char *filename);
bool file_is_writable(const char *filename);
size_t file_get_size(const char *filename);
bool file_delete(const char *filename);
bool file_copy(const char *source, const char *destination);

// Path utilities
char *file_get_directory(const char *filepath);
char *file_get_filename(const char *filepath);
char *file_get_extension(const char *filepath);
char *file_join_paths(const char *dir, const char *filename);
bool file_is_absolute_path(const char *path);

// Error handling
const char *file_get_last_error(void);

#endif // FILE_OPS_H