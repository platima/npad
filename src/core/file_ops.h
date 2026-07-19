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

// Text file encoding as stored on disk
typedef enum {
    NPAD_ENC_UTF8 = 0, // UTF-8 without BOM (default for new files)
    NPAD_ENC_UTF8_BOM, // UTF-8 with BOM
    NPAD_ENC_UTF16_LE, // UTF-16 little endian (with BOM)
    NPAD_ENC_UTF16_BE, // UTF-16 big endian (with BOM)
    NPAD_ENC_ANSI      // System code page (bytes that are not valid UTF-8)
} TextEncoding;

// Line ending style of a text file
typedef enum {
    NPAD_EOL_CRLF = 0, // Windows
    NPAD_EOL_LF,       // Unix
    NPAD_EOL_CR        // Classic Mac
} LineEnding;

// Detected properties of a text file (filled by file_read_text_ex,
// consumed by file_write_text_ex so files round-trip unchanged)
typedef struct {
    TextEncoding encoding;
    LineEnding line_ending;
} TextFileInfo;

// File reading operations
char *file_read_text(const char *filename); // Raw bytes, NUL-terminated
bool file_read_binary(const char *filename, void **data, size_t *size);

// Read a text file, detect its encoding and line endings, and return the
// content converted to UTF-8 (line endings are preserved as-is).
// info may be NULL if the caller does not care.
char *file_read_text_ex(const char *filename, TextFileInfo *info);

// File writing operations
bool file_write_text(const char *filename, const char *content);
bool file_write_binary(const char *filename, const void *data, size_t size);

// Convert UTF-8 content back to the given encoding and line endings and
// write it atomically (temp file + rename). info == NULL writes UTF-8/CRLF.
bool file_write_text_ex(const char *filename, const char *utf8_content, const TextFileInfo *info);

// Whether saving this UTF-8 content as ANSI would lose characters
// (replaced with '?'). Used to warn the user before a lossy save.
bool file_ansi_is_lossy(const char *utf8_content);

// Atomic file operations with rollback capability
bool file_write_text_atomic(const char *filename, const char *content);
bool file_write_binary_atomic(const char *filename, const void *data, size_t size);

// Enhanced validation and checks
bool file_check_disk_space(const char *path, size_t required_bytes);
bool file_validate_permissions(const char *filename, bool for_writing);
bool file_verify_integrity(const char *filename, const char *expected_content);

// Human-readable names for status bar display
const char *file_encoding_name(TextEncoding encoding);
const char *file_line_ending_name(LineEnding line_ending);

// Line ending helpers (exposed for testing)
LineEnding file_detect_line_ending(const char *text);
char *file_convert_line_endings(const char *text, LineEnding target);

// File utility functions
bool file_exists(const char *filename);
bool file_is_readable(const char *filename);
bool file_is_writable(const char *filename);
size_t file_get_size(const char *filename);
// Heuristic sniff of the first 8 KB: NUL bytes (outside BOM-marked UTF-16)
// or a high share of control bytes mark the file as binary.
bool file_looks_binary(const char *filename);
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
