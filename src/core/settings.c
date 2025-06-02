/*
 * npad - Settings Management Implementation
 * Cross-platform settings storage using JSON format
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "settings.h"
#include "file_ops.h"
#include <ctype.h>
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
static char *trim_whitespace(char *str);
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
    free_settings_list();

    if (g_settings_file_path) {
        free(g_settings_file_path);
        g_settings_file_path = NULL;
    }
}

bool settings_set_string(const char *key, const char *value) {
    if (!key || !value)
        return false;

    SettingEntry *entry = find_setting(key);
    if (entry) {
        // Update existing entry
        free(entry->value);
        entry->value = malloc(strlen(value) + 1);
        strcpy(entry->value, value);
    } else {
        // Create new entry
        entry = create_setting(key, value);
        if (!entry)
            return false;

        // Add to front of list
        entry->next = g_settings_head;
        g_settings_head = entry;
    }

    return true;
}

char *settings_get_string(const char *key, const char *default_value) {
    if (!key)
        return NULL;

    SettingEntry *entry = find_setting(key);
    if (entry && entry->value) {
        char *result = malloc(strlen(entry->value) + 1);
        strcpy(result, entry->value);
        return result;
    }

    if (default_value) {
        char *result = malloc(strlen(default_value) + 1);
        strcpy(result, default_value);
        return result;
    }

    return NULL;
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
    return find_setting(key) != NULL;
}

bool settings_remove_key(const char *key) {
    if (!key)
        return false;

    SettingEntry *prev = NULL;
    SettingEntry *current = g_settings_head;

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
            return true;
        }

        prev = current;
        current = current->next;
    }

    return false;
}

bool settings_clear_all(void) {
    free_settings_list();
    return true;
}

bool settings_save(void) {
    if (!g_settings_file_path)
        return false;

    char *content = serialize_settings();
    if (!content)
        return false;

    // Ensure directory exists
    char *dir = file_get_directory(g_settings_file_path);
    if (dir) {
#ifdef _WIN32
        CreateDirectoryA(dir, NULL);
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
    int new_count = 0;

    // Add the new file first
    new_list[new_count++] = malloc(strlen(filepath) + 1);
    strcpy(new_list[new_count - 1], filepath);

    // Add existing files (except the one we're moving to top)
    for (int i = 0; i < count && new_count < MAX_RECENT_FILES; i++) {
        if (i != existing_index) {
            new_list[new_count++] = malloc(strlen(recent_files[i]) + 1);
            strcpy(new_list[new_count - 1], recent_files[i]);
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

    if (recent_files) {
        settings_free_recent_files(recent_files, count);
    }

    return true;
}

char **settings_get_recent_files(int *count) {
    if (!count)
        return NULL;

    char **files = malloc(MAX_RECENT_FILES * sizeof(char *));
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
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {
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

static char *trim_whitespace(char *str) {
    if (!str)
        return NULL;

    // Trim leading whitespace
    while (isspace(*str))
        str++;

    if (*str == 0)
        return str;

    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end))
        end--;

    end[1] = '\0';
    return str;
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
    if (!key || !value)
        return NULL;

    SettingEntry *entry = malloc(sizeof(SettingEntry));
    if (!entry)
        return NULL;

    entry->key = malloc(strlen(key) + 1);
    entry->value = malloc(strlen(value) + 1);

    if (!entry->key || !entry->value) {
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

    // Simple JSON parser for our basic key-value needs
    // This is a minimal implementation - a full JSON parser would be more robust

    char *content_copy = malloc(strlen(content) + 1);
    strcpy(content_copy, content);

    // Remove whitespace and find the opening brace
    char *json_start = strchr(content_copy, '{');
    if (!json_start) {
        free(content_copy);
        return false;
    }

    char *current = json_start + 1;

    while (*current && *current != '}') {
        // Skip whitespace
        while (*current && isspace(*current))
            current++;
        if (*current == '}' || *current == '\0')
            break;

        // Parse key
        if (*current != '"') {
            current++;
            continue;
        }

        current++; // Skip opening quote
        char *key_start = current;

        // Find end of key
        while (*current && *current != '"')
            current++;
        if (!*current)
            break;

        *current = '\0'; // Null-terminate key
        current++;       // Skip closing quote

        // Skip whitespace and colon
        while (*current && (isspace(*current) || *current == ':'))
            current++;

        // Parse value
        if (*current == '"') {
            // String value
            current++; // Skip opening quote
            char *value_start = current;

            // Find end of value, handling escaped quotes
            while (*current && *current != '"') {
                if (*current == '\\' && *(current + 1) == '"') {
                    current += 2;
                } else {
                    current++;
                }
            }

            if (*current == '"') {
                *current = '\0'; // Null-terminate value
                settings_set_string(key_start, value_start);
                current++; // Skip closing quote
            }
        } else {
            // Non-string value (number, boolean, etc.)
            char *value_start = current;

            // Find end of value
            while (*current && *current != ',' && *current != '}' && !isspace(*current)) {
                current++;
            }

            char old_char = *current;
            *current = '\0'; // Null-terminate value
            settings_set_string(key_start, value_start);
            *current = old_char;
        }

        // Skip to next key-value pair
        while (*current && *current != ',' && *current != '}')
            current++;
        if (*current == ',')
            current++;
    }

    free(content_copy);
    return true;
}

static char *serialize_settings(void) {
    // Calculate required buffer size
    size_t total_size = 100; // Base size for JSON structure
    SettingEntry *current = g_settings_head;

    while (current) {
        // "key": "value", (with quotes and escaping)
        total_size += strlen(current->key) + strlen(current->value) + 10;
        current = current->next;
    }

    char *content = malloc(total_size);
    if (!content)
        return NULL;

    strcpy(content, "{\n");

    current = g_settings_head;
    bool first = true;

    while (current) {
        if (!first) {
            strcat(content, ",\n");
        }
        first = false;

        strcat(content, "  \"");
        strcat(content, current->key);
        strcat(content, "\": \"");
        strcat(content, current->value);
        strcat(content, "\"");

        current = current->next;
    }

    strcat(content, "\n}\n");

    return content;
}