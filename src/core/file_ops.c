/*
 * npad - File Operations Implementation
 * Cross-platform file I/O operations
 * 
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define access _access
#define F_OK 0
#define R_OK 4
#define W_OK 2
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// Global error message
static char g_last_error[512] = {0};

static void set_error(const char* message)
{
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static void set_errno_error(const char* operation, const char* filename)
{
    snprintf(g_last_error, sizeof(g_last_error), 
             "%s '%s': %s", operation, filename, strerror(errno));
}

char* file_read_text(const char* filename)
{
    if (!filename) {
        set_error("Invalid filename");
        return NULL;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        set_errno_error("Failed to open file", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        set_errno_error("Failed to get file size", filename);
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer
    char* content = malloc(size + 1);
    if (!content) {
        set_error("Out of memory");
        fclose(file);
        return NULL;
    }
    
    // Read file
    size_t read_size = fread(content, 1, size, file);
    fclose(file);
    
    if (read_size != (size_t)size) {
        set_errno_error("Failed to read file", filename);
        free(content);
        return NULL;
    }
    
    content[size] = '\0';
    return content;
}

bool file_read_binary(const char* filename, void** data, size_t* size)
{
    if (!filename || !data || !size) {
        set_error("Invalid parameters");
        return false;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        set_errno_error("Failed to open file", filename);
        return false;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        set_errno_error("Failed to get file size", filename);
        fclose(file);
        return false;
    }
    
    // Allocate buffer
    void* buffer = malloc(file_size);
    if (!buffer) {
        set_error("Out of memory");
        fclose(file);
        return false;
    }
    
    // Read file
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != (size_t)file_size) {
        set_errno_error("Failed to read file", filename);
        free(buffer);
        return false;
    }
    
    *data = buffer;
    *size = file_size;
    return true;
}

bool file_write_text(const char* filename, const char* content)
{
    if (!filename || !content) {
        set_error("Invalid parameters");
        return false;
    }
    
    FILE* file = fopen(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }
    
    size_t content_length = strlen(content);
    size_t written = fwrite(content, 1, content_length, file);
    fclose(file);
    
    if (written != content_length) {
        set_errno_error("Failed to write file", filename);
        return false;
    }
    
    return true;
}

bool file_write_binary(const char* filename, const void* data, size_t size)
{
    if (!filename || !data) {
        set_error("Invalid parameters");
        return false;
    }
    
    FILE* file = fopen(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (written != size) {
        set_errno_error("Failed to write file", filename);
        return false;
    }
    
    return true;
}

bool file_exists(const char* filename)
{
    if (!filename) return false;
    return access(filename, F_OK) == 0;
}

bool file_is_readable(const char* filename)
{
    if (!filename) return false;
    return access(filename, R_OK) == 0;
}

bool file_is_writable(const char* filename)
{
    if (!filename) return false;
    return access(filename, W_OK) == 0;
}

size_t file_get_size(const char* filename)
{
    if (!filename) return 0;
    
    FILE* file = fopen(filename, "rb");
    if (!file) return 0;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return (size >= 0) ? (size_t)size : 0;
}

bool file_delete(const char* filename)
{
    if (!filename) {
        set_error("Invalid filename");
        return false;
    }
    
    if (remove(filename) != 0) {
        set_errno_error("Failed to delete file", filename);
        return false;
    }
    
    return true;
}

bool file_copy(const char* source, const char* destination)
{
    if (!source || !destination) {
        set_error("Invalid parameters");
        return false;
    }
    
    void* data;
    size_t size;
    
    if (!file_read_binary(source, &data, &size)) {
        return false;
    }
    
    bool result = file_write_binary(destination, data, size);
    free(data);
    
    return result;
}

char* file_get_directory(const char* filepath)
{
    if (!filepath) return NULL;
    
    const char* last_slash = strrchr(filepath, '/');
    const char* last_backslash = strrchr(filepath, '\\');
    const char* separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    
    if (!separator) {
        // No directory separator found, return current directory
        char* result = malloc(2);
        strcpy(result, ".");
        return result;
    }
    
    size_t dir_length = separator - filepath;
    char* directory = malloc(dir_length + 1);
    strncpy(directory, filepath, dir_length);
    directory[dir_length] = '\0';
    
    return directory;
}

char* file_get_filename(const char* filepath)
{
    if (!filepath) return NULL;
    
    const char* last_slash = strrchr(filepath, '/');
    const char* last_backslash = strrchr(filepath, '\\');
    const char* separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    
    const char* filename = separator ? (separator + 1) : filepath;
    
    char* result = malloc(strlen(filename) + 1);
    strcpy(result, filename);
    return result;
}

char* file_get_extension(const char* filepath)
{
    if (!filepath) return NULL;
    
    const char* filename = strrchr(filepath, '/');
    const char* filename_backslash = strrchr(filepath, '\\');
    
    if (filename_backslash > filename) {
        filename = filename_backslash;
    }
    
    if (!filename) {
        filename = filepath;
    } else {
        filename++; // Skip the separator
    }
    
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        // No extension or hidden file
        char* result = malloc(1);
        result[0] = '\0';
        return result;
    }
    
    char* result = malloc(strlen(dot + 1) + 1);
    strcpy(result, dot + 1);
    return result;
}

char* file_join_paths(const char* dir, const char* filename)
{
    if (!dir || !filename) return NULL;
    
    size_t dir_len = strlen(dir);
    size_t filename_len = strlen(filename);
    bool needs_separator = (dir_len > 0 && 
                           dir[dir_len - 1] != '/' && 
                           dir[dir_len - 1] != '\\');
    
    size_t total_len = dir_len + filename_len + (needs_separator ? 1 : 0) + 1;
    char* result = malloc(total_len);
    
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

bool file_is_absolute_path(const char* path)
{
    if (!path || strlen(path) == 0) return false;
    
#ifdef _WIN32
    // Windows: C:\ or \\server\share
    return (strlen(path) >= 3 && path[1] == ':' && path[2] == '\\') ||
           (strlen(path) >= 2 && path[0] == '\\' && path[1] == '\\');
#else
    // Unix-like: starts with /
    return path[0] == '/';
#endif
}

const char* file_get_last_error(void)
{
    return g_last_error;
}