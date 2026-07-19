/*
 * npad - Resource Header
 * Resource ID definitions for Windows resources
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef RESOURCE_H
#define RESOURCE_H

// Application Icon
#define IDI_NPAD 100

// Dialog IDs
#define IDD_FIND 200
#define IDD_REPLACE 201
#define IDD_GOTO 202
#define IDD_CONVERT_DELIM 203
#define IDD_CUSTOM_INDENT 204
#define IDD_PREFS_GENERAL 210
#define IDD_PREFS_APPEARANCE 211
#define IDD_PREFS_BACKUP 212
#define IDD_PREFS_LISTS 213
#define IDD_PREFS_DEFAULTS 214
#define IDD_PREFS_DEBUG 215

// Control IDs for Find Dialog
#define ID_FIND_TEXT 3001
#define ID_FIND_NEXT 3002
#define ID_FIND_CANCEL 3003
#define ID_FIND_CASE 3004
#define ID_FIND_WHOLE_WORD 3005
#define ID_FIND_WRAP 3007
#define ID_FIND_ESCAPES 3139 // Shared by Find and Replace, like the options above

// Control IDs for Replace Dialog
#define ID_REPLACE_WITH 3006
#define ID_REPLACE_NEXT 3008
#define ID_REPLACE_ALL 3009

// Direction radio buttons
#define IDC_RADIO_UP 3010
#define IDC_RADIO_DOWN 3011

// Go To Line dialog
#define ID_GOTO_EDIT 3012

// Preferences: General page
#define ID_PREF_AUTOSAVE_ENABLED 3100
#define ID_PREF_AUTOSAVE_INTERVAL 3101
#define ID_PREF_LARGE_FILE_MB 3102
#define ID_PREF_RECENT_MAX 3103
#define ID_PREF_RECENT_CLEAR 3104
#define ID_PREF_SESSION_ENABLED 3105
#define ID_PREF_SESSION_INTERVAL 3106
#define ID_PREF_CTRL_N_WINDOW 3107

// Preferences: Appearance page
#define ID_PREF_STATUSBAR 3113
#define ID_PREF_FONT 3114
#define ID_PREF_SCHEME 3115
#define ID_PREF_FONT_MONO 3116
#define ID_PREF_FONT_PROP 3117
#define ID_PREF_OPENDYSLEXIC 3118
#define ID_PREF_SYNC_VIEW 3119

// Preferences: Defaults page
#define ID_PREF_DEFAULT_ENCODING 3120
#define ID_PREF_DEFAULT_EOL 3121
#define ID_PREF_DEFAULT_FONT_TYPE 3124
#define ID_PREF_DEFAULT_ZOOM 3125
#define ID_PREF_USE_CURRENT 3126
#define ID_PREF_AUTO_DEFAULTS 3127

// Preferences: Backup page
#define ID_PREF_EXPORT 3122
#define ID_PREF_IMPORT 3123
#define ID_PREF_RESET_DEFAULTS 3128

// Preferences: Debug page (hidden; Ctrl+Shift+. or Shift+click Preferences)
#define ID_PREF_DEBUG_TEXT 3129
#define ID_PREF_COPY_DIAG 3130

// Preferences: Markdown page
#define ID_PREF_LIST_ENABLED 3131
#define ID_PREF_LIST_INDENT_FORMAT 3132
#define ID_PREF_LIST_CUSTOM_TEXT 3137
#define ID_PREF_LIST_TAB_BRACKETS 3138

// Convert Delimiters dialog
#define ID_DELIM_FROM 3133
#define ID_DELIM_TO 3134
#define ID_DELIM_SEL_ONLY 3135
#define ID_DELIM_HINT 3136

// Custom Indent dialog
#define ID_CUSTOM_INDENT_EDIT 3140
#define ID_CUSTOM_INDENT_HINT 3141

// Static text controls
#define IDC_STATIC -1

#endif // RESOURCE_H