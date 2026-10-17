/*
 * npad - Find seeding
 * The platform-independent half of "Find is pre-filled with the selected
 * text", which Windows 10 notepad.exe has done since 1809. Every UI layer
 * (Win32, GTK, and later ncurses and Cocoa) asks the same two questions here,
 * so the rule and the preference are identical on every platform:
 *
 *   - find_seed_enabled(): is the preference on? (key find_seed_selection,
 *     default ON because notepad.exe does it)
 *   - find_seed_text(): may this selection seed the box, and as what string?
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef FIND_SEED_H
#define FIND_SEED_H

#include <stdbool.h>

// Settings key, shared by every platform and its Preferences UI
#define FIND_SEED_KEY "find_seed_selection"

// Longest term that can seed the box, in UTF-16 code units (a character above
// U+FFFF counts as two). It is the capacity of the Win32 search buffer, and
// every platform applies the same cap so a selection seeds the same way
// everywhere: past it the box keeps the last search term rather than holding
// a silently truncated one.
#define FIND_SEED_MAX_UNITS 255

// Is "fill Find with the selected text" on? Default true.
bool find_seed_enabled(void);

// The text a UTF-8 selection should seed Find with, or NULL when it should
// not seed (the caller then keeps the last search term): NULL/empty
// selection, any line break (LF, CR, VT, FF, NEL, LS, PS), invalid UTF-8, or
// longer than FIND_SEED_MAX_UNITS once escaped.
//
// escape_backslashes doubles each '\' so a term that will be run through
// escape interpretation ("Interpret escapes") still matches the selection
// literally - a selected C:\new would otherwise search for C:<newline>ew.
//
// Returns a malloc'd string the caller frees.
char *find_seed_text(const char *selection, bool escape_backslashes);

#endif // FIND_SEED_H
