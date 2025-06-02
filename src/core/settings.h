/*
 * npad - Settings Management
 * Cross-platform settings storage and retrieval
 * 
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

// Settings initialization and cleanup
bool settings_init(void);
void settings_cleanup(void);

// Basic setting operations
bool settings_set_string(const char* key, const char* value);
char* settings_get_string(const char* key, const char* default_value);

bool settings_set_int(const char* key, int value);
int settings_get_int(const char* key, int default_value);

bool settings_set_bool(const char* key, bool value);
bool settings_get_bool(const char* key, bool default_value);

bool settings_set_double(const char* key, double value);
double settings_get_double(const char* key, double default_value);

// Settings management
bool settings_has_key(const char* key);
bool settings_remove_key(const char* key);
bool settings_clear_all(void);

// File operations
bool settings_save(void);
bool settings_load(void);
const char* settings_get_file_path(void);

// Window state helpers
bool settings_save_window_state(int x, int y, int width, int height, bool maximized);
bool settings_load_window_state(int* x, int* y, int* width, int* height, bool* maximized);

// Recent files management
bool settings_add_recent_file(const char* filepath);
char** settings_get_recent_files(int* count);
void settings_free_recent_files(char** files, int count);
bool settings_clear_recent_files(void);

#endif // SETTINGS_H