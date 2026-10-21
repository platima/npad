/*
 * npad - File Operations
 * Cross-platform file I/O operations
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <stdbool.h>
#include <stddef.h>

// Text file encoding as stored on disk
typedef enum {
    NPAD_ENC_UTF8 = 0, // UTF-8 without BOM (default for new files)
    NPAD_ENC_UTF8_BOM, // UTF-8 with BOM
    NPAD_ENC_UTF16_LE, // UTF-16 little endian (BOM unless TextFileInfo.utf16_no_bom)
    NPAD_ENC_UTF16_BE, // UTF-16 big endian (BOM unless TextFileInfo.utf16_no_bom)
    // "ANSI": bytes that are not valid UTF-8. On Windows the system ANSI code
    // page (its legacy one when the system code page is UTF-8); elsewhere
    // Windows-1252, the code page such files almost always come from.
    NPAD_ENC_ANSI
} TextEncoding;

// Line ending style of a text file
typedef enum {
    NPAD_EOL_CRLF = 0, // Windows
    NPAD_EOL_LF,       // Unix
    NPAD_EOL_CR        // Classic Mac
} LineEnding;

// The line ending a platform's own text files use, and so what new documents
// and files with no line breaks get unless the user chose otherwise
// (default_line_ending): CRLF on Windows like Notepad, LF everywhere else
#ifdef _WIN32
#define NPAD_DEFAULT_LINE_ENDING NPAD_EOL_CRLF
#else
#define NPAD_DEFAULT_LINE_ENDING NPAD_EOL_LF
#endif

// Detected properties of a text file (filled by file_read_text_ex,
// consumed by file_write_text_ex so files round-trip unchanged).
// All-false flags are the safe state, so a zeroed struct is a valid new file.
typedef struct {
    TextEncoding encoding;
    // The majority line ending. Mixed endings cannot be kept: the editor holds
    // one kind of line break, so a save writes every break in this style.
    LineEnding line_ending;
    // The file held a NUL before its end, so what was loaded is only the part
    // up to it. npad's text is NUL-terminated throughout, so a binary file
    // cannot be represented - and saving over the original would replace it
    // with the fragment. Set by file_read_text_ex; callers must not overwrite
    // the source file while it is true.
    bool truncated;
    // Some bytes were not valid in the detected encoding (a stray byte in a
    // UTF-8-with-BOM file, an unpaired surrogate or odd trailing byte in
    // UTF-16, an invalid sequence in a multi-byte ANSI code page), so the text
    // holds replacement characters where those bytes were and a save would
    // not reproduce them. Set by file_read_text_ex; callers must ask before
    // saving over the source file while it is true, and auto-save must skip.
    bool decode_lossy;
    // UTF-16 only: the file had no byte order mark, so none is written back.
    // Cleared when the user picks an encoding; new files get a BOM, as in
    // notepad.exe.
    bool utf16_no_bom;
} TextFileInfo;

// File reading operations
char *file_read_text(const char *filename); // Raw bytes, NUL-terminated

// As file_read_text, but reads at most max_bytes. For salvaging a file too
// large to load whole: the caller gets a NUL-terminated prefix rather than
// nothing at all.
char *file_read_text_limited(const char *filename, size_t max_bytes);
bool file_read_binary(const char *filename, void **data, size_t *size);

// Read a text file, detect its encoding and line endings, and return the
// content converted to UTF-8 (line endings are preserved as-is). The result is
// always valid UTF-8: bytes the encoding cannot decode become U+FFFD and set
// info->decode_lossy. NULL on failure, with file_get_last_error() saying why
// (a file too large to convert is refused, never returned empty).
// info may be NULL if the caller does not care.
char *file_read_text_ex(const char *filename, TextFileInfo *info);

// File writing operations
bool file_write_text(const char *filename, const char *content);
bool file_write_binary(const char *filename, const void *data, size_t size);

// Convert UTF-8 content back to the given encoding and line endings and
// write it atomically (temp file + rename). info == NULL writes UTF-8/CRLF.
bool file_write_text_ex(const char *filename, const char *utf8_content, const TextFileInfo *info);

// Whether saving this UTF-8 content as ANSI would lose characters. An ANSI
// save writes every character the code page lacks as '?' (never a
// similar-looking "best fit" character), and this reports exactly those.
// Used to warn the user before a lossy save.
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
// Identity stamp for change detection: size plus last-write time. Neither
// alone is enough - some tools preserve the timestamp, and an edit can leave
// the size unchanged. A content hash would be certain but costs a full re-read.
typedef struct {
    long long size;       // -1 when the file does not exist
    long long write_time; // Platform-native resolution; only compared, never shown
} FileStamp;

// Capture a file's stamp. Returns false (and a size of -1) if it is gone,
// which the caller must treat as a distinct case from "changed".
bool file_get_stamp(const char *filename, FileStamp *out);
bool file_stamp_equal(const FileStamp *a, const FileStamp *b);

bool file_exists(const char *filename);
bool file_is_readable(const char *filename);
bool file_is_writable(const char *filename);
size_t file_get_size(const char *filename);
// Heuristic sniff of the first 8 KB: NUL bytes (outside UTF-16, with or
// without a BOM) or a high share of control bytes mark the file as binary.
bool file_looks_binary(const char *filename);

// Count document statistics over UTF-8 text in one pass: words (runs of
// non-whitespace), characters (code points, line breaks excluded), and
// lines (breaks + 1; empty text counts as 1 line). NULL out-params are
// skipped; NULL text yields zeros/1.
void file_count_text_stats(const char *utf8, size_t *words, size_t *chars, size_t *lines);
bool file_delete(const char *filename);
bool file_copy(const char *source, const char *destination);

// Move/rename a file, honouring UTF-8 paths on Windows. Replaces the
// destination if it exists.
bool file_rename(const char *from, const char *to);

// Path utilities
char *file_get_directory(const char *filepath);
char *file_get_filename(const char *filepath);
char *file_get_extension(const char *filepath);
char *file_join_paths(const char *dir, const char *filename);
bool file_is_absolute_path(const char *path);

// The path made absolute against the current directory (malloc'd; NULL on
// error). A file named on the command line is usually relative, and it has
// to be resolved before anything stores it: Recent Files and crash recovery
// reopen it later, from wherever that later launch happens to run.
char *file_get_absolute_path(const char *path);

// Error handling
const char *file_get_last_error(void);

#endif // FILE_OPS_H
