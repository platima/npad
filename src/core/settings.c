/*
 * npad - Settings Management Implementation
 * Cross-platform settings storage using JSON format
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "settings.h"
#include "error.h"
#include "file_ops.h"
#include "thread_safety.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define MAX_RECENT_FILES 10
#define MAX_LINE_LENGTH 1024
#define MAX_KEY_LENGTH 128
#define MAX_VALUE_LENGTH 512

// Setting entry structure
typedef struct SettingEntry {
    char *key;
    char *value;
    struct SettingEntry *next;
} SettingEntry;

// Global settings state
static SettingEntry *g_settings_head = NULL;
static char *g_settings_file_path = NULL;

// Helper functions
static char *get_settings_directory(void);
static SettingEntry *find_setting(const char *key);
static SettingEntry *create_setting(const char *key, const char *value);
static void free_settings_list(void);
static bool parse_settings_file(const char *content);
static char *serialize_settings(void);

bool settings_init(void) {
    // Get settings directory
    char *settings_dir = get_settings_directory();
    if (!settings_dir)
        return false;

    // Create settings file path
    g_settings_file_path = file_join_paths(settings_dir, "settings.json");
    free(settings_dir);

    if (!g_settings_file_path)
        return false;

    // Load existing settings
    settings_load();

    return true;
}

void settings_cleanup(void) {
    npad_mutex_lock(&g_settings_mutex);
    free_settings_list();
    npad_mutex_unlock(&g_settings_mutex);

    if (g_settings_file_path) {
        free(g_settings_file_path);
        g_settings_file_path = NULL;
    }
}

bool settings_set_string(const char *key, const char *value) {
    if (!key || !value)
        return false;

    npad_mutex_lock(&g_settings_mutex);

    SettingEntry *entry = find_setting(key);
    if (entry) {
        // Update existing entry
        char *new_value = malloc(strlen(value) + 1);
        if (!new_value) {
            NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, key,
                             "Failed to allocate memory for setting value");
            npad_mutex_unlock(&g_settings_mutex);
            return false;
        }
        strcpy(new_value, value);
        free(entry->value);
        entry->value = new_value;
    } else {
        // Create new entry
        entry = create_setting(key, value);
        if (!entry) {
            npad_mutex_unlock(&g_settings_mutex);
            return false;
        }

        // Add to front of list
        entry->next = g_settings_head;
        g_settings_head = entry;
    }

    npad_mutex_unlock(&g_settings_mutex);
    return true;
}

char *settings_get_string(const char *key, const char *default_value) {
    if (!key)
        return NULL;

    npad_mutex_lock(&g_settings_mutex);
    SettingEntry *entry = find_setting(key);
    char *result = NULL;

    if (entry && entry->value) {
        result = malloc(strlen(entry->value) + 1);
        if (result) {
            strcpy(result, entry->value);
        }
    } else if (default_value) {
        result = malloc(strlen(default_value) + 1);
        if (result) {
            strcpy(result, default_value);
        }
    }

    npad_mutex_unlock(&g_settings_mutex);
    return result;
}

bool settings_set_int(const char *key, int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return settings_set_string(key, buffer);
}

int settings_get_int(const char *key, int default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    int result = atoi(str_value);
    free(str_value);
    return result;
}

bool settings_set_bool(const char *key, bool value) {
    return settings_set_string(key, value ? "true" : "false");
}

bool settings_get_bool(const char *key, bool default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    bool result = (strcmp(str_value, "true") == 0 || strcmp(str_value, "1") == 0);
    free(str_value);
    return result;
}

bool settings_set_double(const char *key, double value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.6f", value);
    return settings_set_string(key, buffer);
}

double settings_get_double(const char *key, double default_value) {
    char *str_value = settings_get_string(key, NULL);
    if (!str_value)
        return default_value;

    double result = atof(str_value);
    free(str_value);
    return result;
}

bool settings_has_key(const char *key) {
    if (!key)
        return false;

    npad_mutex_lock(&g_settings_mutex);
    SettingEntry *entry = find_setting(key);
    npad_mutex_unlock(&g_settings_mutex);
    return entry != NULL;
}

bool settings_remove_key(const char *key) {
    if (!key)
        return false;

    npad_mutex_lock(&g_settings_mutex);

    SettingEntry *prev = NULL;
    SettingEntry *current = g_settings_head;
    bool found = false;

    while (current) {
        if (strcmp(current->key, key) == 0) {
            // Found the entry to remove
            if (prev) {
                prev->next = current->next;
            } else {
                g_settings_head = current->next;
            }

            free(current->key);
            free(current->value);
            free(current);
            found = true;
            break;
        }

        prev = current;
        current = current->next;
    }

    npad_mutex_unlock(&g_settings_mutex);
    return found;
}

bool settings_clear_all(void) {
    npad_mutex_lock(&g_settings_mutex);
    free_settings_list();
    npad_mutex_unlock(&g_settings_mutex);
    return true;
}

bool settings_save(void) {
    if (!g_settings_file_path)
        return false;

    char *content = serialize_settings();
    if (!content)
        return false; // Ensure directory exists
    char *dir = file_get_directory(g_settings_file_path);
    if (dir) {
#ifdef _WIN32
        CreateDirectory(dir, NULL);
#else
        mkdir(dir, 0755);
#endif
        free(dir);
    }

    bool result = file_write_text(g_settings_file_path, content);
    free(content);

    return result;
}

bool settings_load(void) {
    if (!g_settings_file_path || !file_exists(g_settings_file_path)) {
        return true; // No file to load, that's OK
    }

    char *content = file_read_text(g_settings_file_path);
    if (!content)
        return false;

    bool result = parse_settings_file(content);
    free(content);

    return result;
}

const char *settings_get_file_path(void) {
    return g_settings_file_path;
}

bool settings_save_window_state(int x, int y, int width, int height, bool maximized) {
    return settings_set_int("window_x", x) && settings_set_int("window_y", y) &&
           settings_set_int("window_width", width) && settings_set_int("window_height", height) &&
           settings_set_bool("window_maximized", maximized);
}

bool settings_load_window_state(int *x, int *y, int *width, int *height, bool *maximized) {
    if (!x || !y || !width || !height || !maximized)
        return false;

    *x = settings_get_int("window_x", 100);
    *y = settings_get_int("window_y", 100);
    *width = settings_get_int("window_width", 800);
    *height = settings_get_int("window_height", 600);
    *maximized = settings_get_bool("window_maximized", false);

    return true;
}

bool settings_add_recent_file(const char *filepath) {
    if (!filepath)
        return false;

    // Get current recent files
    int count;
    char **recent_files = settings_get_recent_files(&count);

    // Check if file is already in the list
    int existing_index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(recent_files[i], filepath) == 0) {
            existing_index = i;
            break;
        }
    }

    // Create new list with the file at the top
    char **new_list = malloc(MAX_RECENT_FILES * sizeof(char *));
    if (!new_list) {
        if (recent_files) {
            settings_free_recent_files(recent_files, count);
        }
        return false;
    }
    int new_count = 0;

    // Add the new file first
    new_list[new_count] = malloc(strlen(filepath) + 1);
    if (!new_list[new_count]) {
        free(new_list);
        if (recent_files) {
            settings_free_recent_files(recent_files, count);
        }
        return false;
    }
    strcpy(new_list[new_count], filepath);
    new_count++;

    // Add existing files (except the one we're moving to top)
    for (int i = 0; i < count && new_count < MAX_RECENT_FILES; i++) {
        if (i != existing_index && recent_files && recent_files[i]) {
            new_list[new_count] = malloc(strlen(recent_files[i]) + 1);
            if (!new_list[new_count]) {
                // Cleanup on failure
                for (int j = 0; j < new_count; j++) {
                    free(new_list[j]);
                }
                free(new_list);
                settings_free_recent_files(recent_files, count);
                return false;
            }
            strcpy(new_list[new_count], recent_files[i]);
            new_count++;
        }
    }

    // Save the new list
    for (int i = 0; i < new_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent_file_%d", i);
        settings_set_string(key, new_list[i]);
    }

    // Remove any old entries
    for (int i = new_count; i < MAX_RECENT_FILES; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent_file_%d", i);
        settings_remove_key(key);
    }

    // Cleanup
    for (int i = 0; i < new_count; i++) {
        free(new_list[i]);
    }
    free(new_list);

    settings_free_recent_files(recent_files, count);

    return true;
}

char **settings_get_recent_files(int *count) {
    if (!count)
        return NULL;

    char **files = malloc(MAX_RECENT_FILES * sizeof(char *));
    if (!files) {
        *count = 0;
        return NULL;
    }
    *count = 0;

    for (int i = 0; i < MAX_RECENT_FILES; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent_file_%d", i);

        char *filepath = settings_get_string(key, NULL);
        if (filepath && file_exists(filepath)) {
            files[*count] = filepath;
            (*count)++;
        } else if (filepath) {
            free(filepath);
        }
    }

    if (*count == 0) {
        free(files);
        return NULL;
    }

    return files;
}

void settings_free_recent_files(char **files, int count) {
    if (!files)
        return;

    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
}

bool settings_clear_recent_files(void) {
    for (int i = 0; i < MAX_RECENT_FILES; i++) {
        char key[32];
        snprintf(key, sizeof(key), "recent_file_%d", i);
        settings_remove_key(key);
    }
    return true;
}

// Helper function implementations

static char *get_settings_directory(void) {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
        char *platima_dir = file_join_paths(path, "Platima");
        char *npad_dir = file_join_paths(platima_dir, "npad");
        free(platima_dir);
        return npad_dir;
    }
    return NULL;
#else
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }

    if (home) {
        char *config_dir = file_join_paths(home, ".config");
        char *npad_dir = file_join_paths(config_dir, "npad");
        free(config_dir);
        return npad_dir;
    }

    return NULL;
#endif
}

static SettingEntry *find_setting(const char *key) {
    if (!key)
        return NULL;

    SettingEntry *current = g_settings_head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

static SettingEntry *create_setting(const char *key, const char *value) {
    if (!key) {
        NPAD_ERROR_INVALID_PARAM("key");
        return NULL;
    }

    if (!value) {
        NPAD_ERROR_INVALID_PARAM("value");
        return NULL;
    }

    SettingEntry *entry = malloc(sizeof(SettingEntry));
    if (!entry) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, key,
                         "Failed to allocate memory for setting entry");
        return NULL;
    }

    entry->key = malloc(strlen(key) + 1);
    entry->value = malloc(strlen(value) + 1);

    if (!entry->key || !entry->value) {
        NPAD_ERROR_ERROR(NPAD_ERROR_MEMORY, errno, key,
                         "Failed to allocate memory for setting key/value strings");
        free(entry->key);
        free(entry->value);
        free(entry);
        return NULL;
    }

    strcpy(entry->key, key);
    strcpy(entry->value, value);
    entry->next = NULL;

    return entry;
}

static void free_settings_list(void) {
    SettingEntry *current = g_settings_head;
    while (current) {
        SettingEntry *next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
    g_settings_head = NULL;
}

static bool parse_settings_file(const char *content) {
    if (!content)
        return false;

    size_t content_len = strlen(content);
    char *content_copy = malloc(content_len + 1);
    if (!content_copy)
        return false;

    strcpy(content_copy, content);

    // Remove whitespace and find the opening brace
    char *json_start = strchr(content_copy, '{');
    if (!json_start) {
        free(content_copy);
        return false;
    }

    char *current = json_start + 1;
    char *content_end = content_copy + content_len;

    while (current < content_end && *current && *current != '}') {
        // Skip whitespace
        while (current < content_end && *current && isspace(*current))
            current++;
        if (current >= content_end || *current == '}' || *current == '\0')
            break;

        // Parse key
        if (*current != '"') {
            current++;
            continue;
        }

        current++; // Skip opening quote
        char *key_start = current;

        // Find end of key
        while (current < content_end && *current && *current != '"') {
            if (*current == '\\' && current + 1 < content_end) {
                current += 2; // Skip escaped character
            } else {
                current++;
            }
        }
        if (current >= content_end || !*current)
            break;

        *current = '\0'; // Null-terminate key
        current++;       // Skip closing quote

        // Skip whitespace and colon
        while (current < content_end && *current && (isspace(*current) || *current == ':'))
            current++;

        // Parse value
        if (current < content_end && *current == '"') {
            // String value
            current++; // Skip opening quote
            char *value_start = current;

            // Find end of value, handling escaped quotes
            while (current < content_end && *current && *current != '"') {
                if (*current == '\\' && current + 1 < content_end) {
                    current += 2; // Skip escaped character
                } else {
                    current++;
                }
            }

            if (current < content_end && *current == '"') {
                *current = '\0'; // Null-terminate value
                settings_set_string(key_start, value_start);
                current++; // Skip closing quote
            }
        } else if (current < content_end) {
            // Non-string value (number, boolean, etc.)
            char *value_start = current;

            // Find end of value
            while (current < content_end && *current && *current != ',' && *current != '}' &&
                   !isspace(*current)) {
                current++;
            }

            if (current < content_end) {
                char old_char = *current;
                *current = '\0'; // Null-terminate value
                settings_set_string(key_start, value_start);
                *current = old_char;
            }
        }

        // Skip to next key-value pair
        while (current < content_end && *current && *current != ',' && *current != '}')
            current++;
        if (current < content_end && *current == ',')
            current++;
    }

    free(content_copy);
    return true;
}

static char *serialize_settings(void) {
    npad_mutex_lock(&g_settings_mutex);

    // Calculate required buffer size more accurately
    size_t total_size = 100; // Base size for JSON structure
    SettingEntry *current = g_settings_head;
    int entry_count = 0;

    while (current) {
        // Calculate exact size needed for this entry
        size_t key_len = current->key ? strlen(current->key) : 0;
        size_t value_len = current->value ? strlen(current->value) : 0;

        // Count characters that need escaping
        size_t key_escaped_len = key_len;
        size_t value_escaped_len = value_len;

        if (current->key) {
            for (size_t i = 0; i < key_len; i++) {
                if (current->key[i] == '"' || current->key[i] == '\\') {
                    key_escaped_len++;
                }
            }
        }

        if (current->value) {
            for (size_t i = 0; i < value_len; i++) {
                if (current->value[i] == '"' || current->value[i] == '\\') {
                    value_escaped_len++;
                }
            }
        }

        // "key": "value", plus quotes, colon, space, comma, newline
        total_size += key_escaped_len + value_escaped_len + 20;
        entry_count++;
        current = current->next;
    }

    // Add margin for structure and final newlines
    total_size += 50;

    char *content = malloc(total_size);
    if (!content) {
        npad_mutex_unlock(&g_settings_mutex);
        return NULL;
    }

    size_t written = 0;
    int ret = snprintf(content, total_size, "{\n");
    if (ret < 0 || (size_t) ret >= total_size - written) {
        free(content);
        npad_mutex_unlock(&g_settings_mutex);
        return NULL;
    }
    written += ret;

    current = g_settings_head;
    bool first = true;

    while (current && written < total_size - 50) { // Leave margin for closing
        if (!first) {
            ret = snprintf(content + written, total_size - written, ",\n");
            if (ret < 0 || (size_t) ret >= total_size - written) {
                free(content);
                npad_mutex_unlock(&g_settings_mutex);
                return NULL;
            }
            written += ret;
        }
        first = false;

        // Escape quotes in key and value - calculate exact size needed
        size_t key_len = strlen(current->key);
        size_t value_len = strlen(current->value);

        char *escaped_key = malloc(key_len * 2 + 1);
        char *escaped_value = malloc(value_len * 2 + 1);
        if (!escaped_key || !escaped_value) {
            free(escaped_key);
            free(escaped_value);
            free(content);
            npad_mutex_unlock(&g_settings_mutex);
            return NULL;
        }

        // Escape key
        const char *src = current->key;
        char *dst = escaped_key;
        while (*src) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            }
            *dst++ = *src++;
        }
        *dst = '\0';

        // Escape value
        src = current->value;
        dst = escaped_value;
        while (*src) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            }
            *dst++ = *src++;
        }
        *dst = '\0';

        ret = snprintf(content + written, total_size - written, "  \"%s\": \"%s\"", escaped_key,
                       escaped_value);

        free(escaped_key);
        free(escaped_value);

        if (ret < 0 || (size_t) ret >= total_size - written) {
            free(content);
            npad_mutex_unlock(&g_settings_mutex);
            return NULL;
        }
        written += ret;

        current = current->next;
    }

    ret = snprintf(content + written, total_size - written, "\n}\n");
    if (ret < 0 || (size_t) ret >= total_size - written) {
        free(content);
        npad_mutex_unlock(&g_settings_mutex);
        return NULL;
    }

    npad_mutex_unlock(&g_settings_mutex);
    return content;
}