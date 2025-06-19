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
#include <stdint.h>
#include <time.h>

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
#include <sys/statvfs.h>
#include <unistd.h>
#endif

// CRC32 lookup table for corruption detection
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// Global error message
static char g_last_error[512] = { 0 };

static void set_error(const char *message) {
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static void set_errno_error(const char *operation, const char *filename) {
    snprintf(g_last_error, sizeof(g_last_error), "%.100s '%.200s': %.150s",
             operation ? operation : "Unknown", filename ? filename : "Unknown", strerror(errno));
}

static bool is_safe_path(const char *filename) {
    if (!filename || strlen(filename) == 0) {
        return false;
    }

    size_t len = strlen(filename);
    if (len > 1024) {
        return false; // Path too long
    }

    // Check for various path traversal patterns but allow legitimate paths
    const char *pos = filename;
    while ((pos = strstr(pos, "..")) != NULL) {
        // Check if ".." is part of a legitimate filename (not a directory traversal)
        bool is_traversal = false;

        // Check if ".." is at start of path or preceded by path separator
        if (pos == filename || pos[-1] == '/' || pos[-1] == '\\') {
            // Check if ".." is followed by path separator or end of string
            if (pos[2] == '\0' || pos[2] == '/' || pos[2] == '\\') {
                is_traversal = true;
            }
        }

        if (is_traversal) {
            return false; // This is a directory traversal attempt
        }

        pos += 2; // Move past this ".." to continue checking
    }

#ifdef _WIN32
    // Allow Windows absolute paths (C:\, D:\, etc.) and UNC paths (\\server\share)
    // These are legitimate for file dialogs
    if ((len >= 3 && filename[1] == ':' && (filename[2] == '\\' || filename[2] == '/')) ||
        (len >= 2 && filename[0] == '\\' && filename[1] == '\\')) {
        // This is a legitimate absolute Windows path
    }
#else
    // Allow Unix absolute paths starting with /
    if (filename[0] == '/') {
        // This is a legitimate absolute Unix path
    }
#endif

    // Check for null bytes (could indicate injection)
    for (size_t i = 0; i < len; i++) {
        if (filename[i] == '\0') {
            return false;
        }
    }

    // which we've already checked above. Other restrictions were too aggressive.
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

    // Validate read permissions
    if (!file_validate_permissions(filename, false)) {
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

    // Read file in chunks for better error detection
    size_t total_read = 0;
    const size_t CHUNK_SIZE = 8192;

    while (total_read < (size_t) size) {
        size_t to_read = (size_t) size - total_read;
        if (to_read > CHUNK_SIZE) {
            to_read = CHUNK_SIZE;
        }

        size_t chunk_read = fread(content + total_read, 1, to_read, file);
        if (chunk_read == 0) {
            if (feof(file)) {
                break; // End of file reached
            } else if (ferror(file)) {
                set_error("I/O error during file read");
                free(content);
                fclose(file);
                return NULL;
            }
        }
        total_read += chunk_read;
    }

    fclose(file);

    if (total_read != (size_t) size) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename,
                         "Failed to read complete file: expected %ld bytes, got %zu bytes", size,
                         total_read);
        set_errno_error("Failed to read file completely", filename);
        free(content);
        return NULL;
    }

    content[size] = '\0';

    // Basic corruption check - verify the file still exists and has expected size
    size_t current_size = file_get_size(filename);
    if (current_size != (size_t) size) {
        set_error("File size changed during read operation");
        free(content);
        return NULL;
    }

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

    size_t content_length = strlen(content);

    // Basic disk space check (lightweight)
    if (!file_check_disk_space(filename, content_length + 512)) {
        return false;
    }

    // Basic permission validation
    if (!file_validate_permissions(filename, true)) {
        return false;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }

    size_t written = fwrite(content, 1, content_length, file);

    // Check for write errors before closing
    bool write_error = (written != content_length);
    if (write_error) {
        int error_code = ferror(file);
        fclose(file);

        // Provide more specific error messages
        if (error_code != 0) {
            set_error("Write operation failed: insufficient disk space or I/O error");
        } else {
            set_errno_error("Failed to write complete file", filename);
        }

        // Clean up incomplete file
        remove(filename);
        return false;
    }

    // Ensure data is flushed to disk
    if (fflush(file) != 0) {
        set_errno_error("Failed to flush data to disk", filename);
        fclose(file);
        remove(filename);
        return false;
    }

    int close_result = fclose(file);
    if (close_result != 0) {
        set_errno_error("Failed to close file after writing", filename);
        remove(filename);
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

    // Basic disk space check (lightweight)
    if (!file_check_disk_space(filename, size + 512)) {
        return false;
    }

    // Basic permission validation
    if (!file_validate_permissions(filename, true)) {
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
    if (write_error) {
        int error_code = ferror(file);
        fclose(file);

        // Provide more specific error messages
        if (error_code != 0) {
            set_error("Write operation failed: insufficient disk space or I/O error");
        } else {
            set_errno_error("Failed to write complete file", filename);
        }

        // Clean up incomplete file
        remove(filename);
        return false;
    }

    // Ensure data is flushed to disk
    if (fflush(file) != 0) {
        set_errno_error("Failed to flush data to disk", filename);
        fclose(file);
        remove(filename);
        return false;
    }

    int close_result = fclose(file);
    if (close_result != 0) {
        set_errno_error("Failed to close file after writing", filename);
        remove(filename);
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

// Calculate CRC32 checksum for data integrity verification
static uint32_t calculate_crc32(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// Generate a temporary filename for atomic operations
static char *generate_temp_filename(const char *original_filename) {
    if (!original_filename) {
        return NULL;
    }

    size_t len = strlen(original_filename);
    char *temp_filename = malloc(len + 16); // Extra space for suffix
    if (!temp_filename) {
        return NULL;
    }

    snprintf(temp_filename, len + 16, "%s.tmp.%d", original_filename, (int) time(NULL));
    return temp_filename;
}

// Check available disk space before writing
bool file_check_disk_space(const char *path, size_t required_bytes) {
    if (!path) {
        set_error("Invalid path for disk space check");
        return false;
    }

    // Get directory path for the file
    char *dir_path = file_get_directory(path);
    if (!dir_path) {
        set_error("Failed to get directory for disk space check");
        return false;
    }

    bool result = false;

#ifdef _WIN32
    ULARGE_INTEGER free_bytes_available;
    if (GetDiskFreeSpaceEx(dir_path, &free_bytes_available, NULL, NULL)) {
        result = (free_bytes_available.QuadPart >= required_bytes);
        if (!result) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Insufficient disk space: %llu bytes available, %zu bytes required",
                     (unsigned long long) free_bytes_available.QuadPart, required_bytes);
        }
    } else {
        set_error("Failed to get disk space information");
    }
#else
    struct statvfs stat;
    if (statvfs(dir_path, &stat) == 0) {
        uint64_t available_bytes = (uint64_t) stat.f_bavail * stat.f_frsize;
        result = (available_bytes >= required_bytes);
        if (!result) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Insufficient disk space: %llu bytes available, %zu bytes required",
                     (unsigned long long) available_bytes, required_bytes);
        }
    } else {
        set_errno_error("Failed to get disk space information", dir_path);
    }
#endif

    free(dir_path);
    return result;
}

// Enhanced permission validation
bool file_validate_permissions(const char *filename, bool for_writing) {
    if (!filename) {
        set_error("Invalid filename for permission validation");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    // Check if file exists
    bool file_exists_flag = file_exists(filename);

    if (file_exists_flag) {
        // File exists - check read/write permissions
        if (!file_is_readable(filename)) {
            set_error("File is not readable");
            return false;
        }

        if (for_writing && !file_is_writable(filename)) {
            set_error("File is not writable");
            return false;
        }

#ifdef _WIN32
        // Check Windows file attributes
        DWORD attributes = GetFileAttributes(filename);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (for_writing && (attributes & FILE_ATTRIBUTE_READONLY)) {
                set_error("File is read-only");
                return false;
            }

            if (attributes & FILE_ATTRIBUTE_SYSTEM) {
                set_error("Cannot modify system file");
                return false;
            }
        }
#else
        // Check Unix permissions more thoroughly
        struct stat file_stat;
        if (stat(filename, &file_stat) == 0) {
            // Check if it's a regular file
            if (!S_ISREG(file_stat.st_mode)) {
                set_error("Path is not a regular file");
                return false;
            }

            // Check owner permissions if we're the owner
            if (geteuid() == file_stat.st_uid) {
                if (for_writing && !(file_stat.st_mode & S_IWUSR)) {
                    set_error("File is not writable by owner");
                    return false;
                }
            }
        }
#endif
    } else if (for_writing) {
        // File doesn't exist - check if we can create it in the directory
        char *dir_path = file_get_directory(filename);
        if (!dir_path) {
            set_error("Failed to get directory for permission check");
            return false;
        }

        bool can_create = file_is_writable(dir_path);
        if (!can_create) {
            set_error("Cannot create file in directory: permission denied");
            free(dir_path);
            return false;
        }

        free(dir_path);
    }

    return true;
}

// Verify file integrity using content comparison
bool file_verify_integrity(const char *filename, const char *expected_content) {
    if (!filename || !expected_content) {
        set_error("Invalid parameters for integrity verification");
        return false;
    }

    char *actual_content = file_read_text(filename);
    if (!actual_content) {
        // Error already set by file_read_text
        return false;
    }

    bool integrity_ok = (strcmp(actual_content, expected_content) == 0);

    if (!integrity_ok) {
        set_error("File content integrity check failed");
    }

    free(actual_content);
    return integrity_ok;
}

// Atomic write operation with rollback capability
bool file_write_text_atomic(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters for atomic write");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    size_t content_length = strlen(content);

    // Check disk space before proceeding
    if (!file_check_disk_space(filename, content_length + 1024)) { // Extra buffer for metadata
        return false;
    }

    // Validate permissions
    if (!file_validate_permissions(filename, true)) {
        return false;
    }

    // Generate temporary filename
    char *temp_filename = generate_temp_filename(filename);
    if (!temp_filename) {
        set_error("Failed to generate temporary filename");
        return false;
    }

    // Write to temporary file first
    bool write_success = file_write_text(temp_filename, content);
    if (!write_success) {
        // Error already set by file_write_text
        remove(temp_filename); // Clean up temp file
        free(temp_filename);
        return false;
    }

    // Verify the written content
    if (!file_verify_integrity(temp_filename, content)) {
        set_error("Integrity verification failed after write");
        remove(temp_filename); // Clean up temp file
        free(temp_filename);
        return false;
    }

    // Atomically replace the original file
#ifdef _WIN32
    // Windows: Use MoveFileEx for atomic replacement
    if (!MoveFileEx(temp_filename, filename, MOVEFILE_REPLACE_EXISTING)) {
        set_error("Failed to atomically replace file");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#else
    // Unix: Use rename() which is atomic
    if (rename(temp_filename, filename) != 0) {
        set_errno_error("Failed to atomically replace file", filename);
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#endif

    free(temp_filename);
    return true;
}

// Atomic binary write operation
bool file_write_binary_atomic(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters for atomic binary write");
        return false;
    }

    if (!is_safe_path(filename)) {
        set_error("Invalid or unsafe file path");
        return false;
    }

    // Check disk space before proceeding
    if (!file_check_disk_space(filename, size + 1024)) { // Extra buffer for metadata
        return false;
    }

    // Validate permissions
    if (!file_validate_permissions(filename, true)) {
        return false;
    }

    // Generate temporary filename
    char *temp_filename = generate_temp_filename(filename);
    if (!temp_filename) {
        set_error("Failed to generate temporary filename");
        return false;
    }

    // Write to temporary file first
    bool write_success = file_write_binary(temp_filename, data, size);
    if (!write_success) {
        // Error already set by file_write_binary
        remove(temp_filename); // Clean up temp file
        free(temp_filename);
        return false;
    }

    // Verify the written data by reading it back and comparing checksums
    void *read_data;
    size_t read_size;
    if (!file_read_binary(temp_filename, &read_data, &read_size)) {
        set_error("Failed to verify written data");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }

    bool data_matches =
        (read_size == size && calculate_crc32(data, size) == calculate_crc32(read_data, read_size));
    free(read_data);

    if (!data_matches) {
        set_error("Data integrity verification failed after write");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }

    // Atomically replace the original file
#ifdef _WIN32
    if (!MoveFileEx(temp_filename, filename, MOVEFILE_REPLACE_EXISTING)) {
        set_error("Failed to atomically replace file");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#else
    if (rename(temp_filename, filename) != 0) {
        set_errno_error("Failed to atomically replace file", filename);
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#endif

    free(temp_filename);
    return true;
}