/*
 * npad - File Operations Implementation
 * Cross-platform file I/O operations
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "file_ops.h"
#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define access _access
#define F_OK 0
#define R_OK 4
#define W_OK 2
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// Global error message
static char g_last_error[512] = { 0 };

static void set_error(const char *message) {
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static void set_errno_error(const char *operation, const char *filename) {
    snprintf(g_last_error, sizeof(g_last_error), "%.100s '%.200s': %.150s",
             operation ? operation : "Unknown", filename ? filename : "Unknown", strerror(errno));
}

// FIXED: Enhanced path validation to prevent directory traversal
static bool is_safe_path(const char *filename) {
    if (!filename || strlen(filename) == 0) {
        return false;
    }

    size_t len = strlen(filename);
    if (len > 1024) {
        return false; // Path too long
    }

    // Check for various path traversal patterns
    if (strstr(filename, "..") != NULL) {
        return false; // Any ".." is suspicious
    }

// Check for absolute paths that might escape sandbox
#ifdef _WIN32
    // Check for drive letters or UNC paths
    if ((len >= 3 && filename[1] == ':') ||
        (len >= 2 && filename[0] == '\\' && filename[1] == '\\')) {
        // For now, reject absolute paths - in a real app, you'd validate they're in allowed
        // directories
        return false;
    }
#else
    // Check for absolute paths starting with /
    if (filename[0] == '/') {
        return false;
    }
#endif

    // Check for null bytes (could indicate injection)
    for (size_t i = 0; i < len; i++) {
        if (filename[i] == '\0') {
            return false;
        }
    }

    return true;
}

char *file_read_text(const char *filename) {
    if (!filename) {
        NPAD_ERROR_INVALID_PARAM("filename");
        set_error("Invalid filename: NULL pointer");
        return NULL;
    }

    if (!is_safe_path(filename)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, 0, filename, "Invalid or unsafe file path: %s",
                         filename);
        set_error("Invalid or unsafe file path");
        return NULL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "Failed to open file for reading: %s",
                         strerror(errno));
        set_errno_error("Failed to open file", filename);
        return NULL;
    }

    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "Failed to seek to end of file: %s",
                         strerror(errno));
        set_errno_error("Failed to seek in file", filename);
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "Failed to get file size: %s",
                         strerror(errno));
        set_errno_error("Failed to get file size", filename);
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "Failed to seek to start of file: %s",
                         strerror(errno));
        set_errno_error("Failed to seek in file", filename);
        fclose(file);
        return NULL;
    }

    // Check for reasonable file size limits (prevent DoS)
    const long MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB limit
    if (size > MAX_FILE_SIZE) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, 0, filename, "File too large: %ld bytes (max %ld)",
                         size, MAX_FILE_SIZE);
        set_error("File too large");
        fclose(file);
        return NULL;
    }

    // Allocate buffer
    char *content = malloc((size_t) size + 1);
    if (!content) {
        NPAD_ERROR_MEMORY_ALLOC(filename);
        set_error("Out of memory");
        fclose(file);
        return NULL;
    }

    // Read file
    size_t read_size = fread(content, 1, (size_t) size, file);
    fclose(file);

    if (read_size != (size_t) size) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename,
                         "Failed to read complete file: expected %ld bytes, got %zu bytes", size,
                         read_size);
        set_errno_error("Failed to read file", filename);
        free(content);
        return NULL;
    }

    content[size] = '\0';
    return content;
}

bool file_read_binary(const char *filename, void **data, size_t *size) {
    if (!filename || !data || !size) {
        set_error("Invalid parameters");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        set_errno_error("Failed to open file", filename);
        return false;
    }

    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        set_errno_error("Failed to seek in file", filename);
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        set_errno_error("Failed to get file size", filename);
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        set_errno_error("Failed to seek in file", filename);
        fclose(file);
        return false;
    }

    // Check file size limits
    const long MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB limit
    if (file_size > MAX_FILE_SIZE) {
        set_error("File too large");
        fclose(file);
        return false;
    }

    // Allocate buffer
    void *buffer = malloc((size_t) file_size);
    if (!buffer) {
        set_error("Out of memory");
        fclose(file);
        return false;
    }

    // Read file
    size_t read_size = fread(buffer, 1, (size_t) file_size, file);
    fclose(file);

    if (read_size != (size_t) file_size) {
        set_errno_error("Failed to read file", filename);
        free(buffer);
        return false;
    }

    *data = buffer;
    *size = (size_t) file_size;
    return true;
}

bool file_write_text(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }

    size_t content_length = strlen(content);
    size_t written = fwrite(content, 1, content_length, file);

    // Check for write errors before closing
    bool write_error = (written != content_length);
    int close_result = fclose(file);

    if (write_error) {
        set_errno_error("Failed to write file", filename);
        return false;
    }

    if (close_result != 0) {
        set_errno_error("Failed to close file after writing", filename);
        return false;
    }

    return true;
}

bool file_write_binary(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }

    size_t written = fwrite(data, 1, size, file);

    // Check for write errors before closing
    bool write_error = (written != size);
    int close_result = fclose(file);

    if (write_error) {
        set_errno_error("Failed to write file", filename);
        return false;
    }

    if (close_result != 0) {
        set_errno_error("Failed to close file after writing", filename);
        return false;
    }

    return true;
}

bool file_exists(const char *filename) {
    if (!filename || !is_safe_path(filename))
        return false;
    return access(filename, F_OK) == 0;
}

bool file_is_readable(const char *filename) {
    if (!filename || !is_safe_path(filename))
        return false;
    return access(filename, R_OK) == 0;
}

bool file_is_writable(const char *filename) {
    if (!filename || !is_safe_path(filename))
        return false;
    return access(filename, W_OK) == 0;
}

size_t file_get_size(const char *filename) {
    if (!filename || !is_safe_path(filename))
        return 0;

    FILE *file = fopen(filename, "rb");
    if (!file)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long size = ftell(file);
    fclose(file);

    return (size >= 0) ? (size_t) size : 0;
}

bool file_delete(const char *filename) {
    if (!filename) {
        set_error("Invalid filename");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    if (remove(filename) != 0) {
        set_errno_error("Failed to delete file", filename);
        return false;
    }

    return true;
}

bool file_copy(const char *source, const char *destination) {
    if (!source || !destination) {
        set_error("Invalid parameters");
        return false;
    }

    if (!is_safe_path(source) || !is_safe_path(destination)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    void *data;
    size_t size;

    if (!file_read_binary(source, &data, &size)) {
        return false;
    }

    bool result = file_write_binary(destination, data, size);
    free(data);

    return result;
}

char *file_get_directory(const char *filepath) {
    if (!filepath)
        return NULL;

    const char *last_slash = strrchr(filepath, '/');
    const char *last_backslash = strrchr(filepath, '\\');
    const char *separator = (last_slash > last_backslash) ? last_slash : last_backslash;

    if (!separator) {
        // No directory separator found, return current directory
        char *result = malloc(2);
        if (!result) {
            return NULL;
        }
        strcpy(result, ".");
        return result;
    }

    size_t dir_length = (size_t) (separator - filepath);
    if (dir_length == 0) {
        // Root directory case
        char *result = malloc(2);
        if (!result) {
            return NULL;
        }
        strcpy(result, separator == last_slash ? "/" : "\\");
        return result;
    }

    char *directory = malloc(dir_length + 1);
    if (!directory) {
        return NULL;
    }
    strncpy(directory, filepath, dir_length);
    directory[dir_length] = '\0';

    return directory;
}

char *file_get_filename(const char *filepath) {
    if (!filepath)
        return NULL;

    const char *last_slash = strrchr(filepath, '/');
    const char *last_backslash = strrchr(filepath, '\\');
    const char *separator = (last_slash > last_backslash) ? last_slash : last_backslash;

    const char *filename = separator ? (separator + 1) : filepath;

    char *result = malloc(strlen(filename) + 1);
    if (!result) {
        return NULL;
    }
    strcpy(result, filename);
    return result;
}

char *file_get_extension(const char *filepath) {
    if (!filepath)
        return NULL;

    const char *filename = strrchr(filepath, '/');
    const char *filename_backslash = strrchr(filepath, '\\');

    if (filename_backslash > filename) {
        filename = filename_backslash;
    }

    if (!filename) {
        filename = filepath;
    } else {
        filename++; // Skip the separator
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        // No extension or hidden file
        char *result = malloc(1);
        if (!result) {
            return NULL;
        }
        result[0] = '\0';
        return result;
    }

    char *result = malloc(strlen(dot + 1) + 1);
    if (!result) {
        return NULL;
    }
    strcpy(result, dot + 1);
    return result;
}

char *file_join_paths(const char *dir, const char *filename) {
    if (!dir || !filename)
        return NULL;

    size_t dir_len = strlen(dir);
    size_t filename_len = strlen(filename);

    // Check for empty strings
    if (dir_len == 0) {
        char *result = malloc(filename_len + 1);
        if (!result)
            return NULL;
        strcpy(result, filename);
        return result;
    }

    if (filename_len == 0) {
        char *result = malloc(dir_len + 1);
        if (!result)
            return NULL;
        strcpy(result, dir);
        return result;
    }

    bool needs_separator = (dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\');

    size_t total_len = dir_len + filename_len + (needs_separator ? 1 : 0) + 1;
    char *result = malloc(total_len);
    if (!result) {
        return NULL;
    }

    strcpy(result, dir);
    if (needs_separator) {
#ifdef _WIN32
        strcat(result, "\\");
#else
        strcat(result, "/");
#endif
    }
    strcat(result, filename);

    return result;
}

bool file_is_absolute_path(const char *path) {
    if (!path || strlen(path) == 0)
        return false;

#ifdef _WIN32
    // Windows: C:\ or \\server\share
    return (strlen(path) >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
           (strlen(path) >= 2 && path[0] == '\\' && path[1] == '\\');
#else
    // Unix-like: starts with /
    return path[0] == '/';
#endif
}

const char *file_get_last_error(void) {
    return g_last_error;
}