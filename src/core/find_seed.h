/*
 * npad - Find seeding and the other platform-independent halves of Find
 * The platform-independent half of "Find is pre-filled with the selected
 * text", which Windows 10 notepad.exe has done since 1809. Every UI layer
 * (Win32, GTK, and later ncurses and Cocoa) asks the same two questions here,
 * so the rule and the preference are identical on every platform:
 *
 *   - find_seed_enabled(): is the preference on? (key find_seed_selection,
 *     default ON because notepad.exe does it)
 *   - find_seed_text(): may this selection seed the box, and as what string?
 *
 * Also here, because every platform needs the same answer:
 *
 *   - find_replace_ranges(): Replace All's result, built in one pass so the
 *     UI can apply it as ONE edit (one undo step)
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef FIND_SEED_H
#define FIND_SEED_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

// Settings key, shared by every platform and its Preferences UI
#define FIND_SEED_KEY "find_seed_selection"

// Wrap-around: set from the Find/Replace dialog AND offered on the Preferences
// Editor page as "Wrap around by default", so it is a preference (default off,
// like notepad.exe) even though it lives under the kept find_ prefix
#define FIND_WRAP_KEY "find_wrap_around"

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

// One match for find_replace_ranges: offsets in code units into the text,
// end exclusive
typedef struct {
    long start;
    long end;
} FindRange;

// Replace All's text in one pass: text[0..len) with every range replaced by
// replacement. The ranges must be in ascending order, non-overlapping and
// within [0, len]; anything else returns NULL (as does running out of
// memory), and the caller then leaves the document alone.
//
// Building the whole result first lets the UI apply it as a single edit, which
// the edit control records as one undo step. Code units are wchar_t: UTF-16 on
// Windows, where this is used, so offsets are the edit control's positions.
//
// Returns a malloc'd, NUL-terminated buffer the caller frees; *out_len (when
// given) receives its length in code units, not counting the NUL.
wchar_t *find_replace_ranges(const wchar_t *text, size_t len, const FindRange *ranges, size_t count,
                             const wchar_t *replacement, size_t *out_len);

#endif // FIND_SEED_H
