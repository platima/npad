/*
 * npad - RichEdit text boundary (Win32)
 *
 * The one place the document's text crosses between the core's UTF-8 and the
 * RichEdit control, kept apart from ui_win32.c so tests/test_richedit_boundary.c
 * can drive the very same code against a hidden control in its own process.
 *
 * Four things about RichEdit make this more than a UTF-8 <-> UTF-16 copy:
 *
 * - It stores every line break as one bare CR, and its loader reads some break
 *   sequences differently from the core: CR CR LF is msftedit's EM_FMTLINES
 *   soft-break marker and is DELETED, joining the two lines. So the text is
 *   normalised before it goes in: every CRLF, lone CR and lone LF becomes one
 *   CR, exactly the breaks file_convert_line_endings counts. Reading back
 *   (richedit_get_text: the stored CRs widened to CR LF, see below) then gives
 *   the core's own CRLF conversion of the input.
 *
 * - It rewrites some valid characters on the way in: U+2028 and U+2029 become
 *   paragraph breaks; BEL, U+FDD0-FDEF and U+FFF9-FFFF (U+FFFC and U+FFFD
 *   among them) become spaces. Nothing reports it, so after loading the
 *   control is compared with what was sent, and the caller learns whether the
 *   control holds the text exactly. A document that does not must never be
 *   saved over its file without the user being told.
 *
 * - Reading with GT_USECRLF translates more than breaks: a VT the control
 *   holds intact comes out as CR LF. So the text is read in its stored form
 *   (one CR per break) and only the CRs are widened to CR LF here. Typing
 *   never stores a VT in plain-text mode (Shift+Enter stores CR; the boundary
 *   test pins that), so nothing a user types changes meaning.
 *
 * - EM_GETTEXTEX takes its buffer size as a DWORD byte count and reports how
 *   much it copied; both are checked, so a short copy is a failure rather than
 *   a truncated save.
 *
 * richedit_normalize_breaks is plain C and builds on any platform (the unit
 * tests check it against the core's line ending conversion); the rest needs
 * Win32.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef RICHEDIT_TEXT_H
#define RICHEDIT_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

// The control's text limit (EM_EXLIMITTEXT): effectively unlimited
#define RICHEDIT_TEXT_LIMIT 0x7FFFFFFE

// Rewrites text[0..len) in place so every line break - CRLF, lone CR or lone
// LF - is one CR, and returns the new length. The buffer must have room for
// len + 1 units: the result is always NUL-terminated.
size_t richedit_normalize_breaks(wchar_t *text, size_t len);

#ifdef _WIN32
#include <windows.h>

// Plain-text mode, the length limit and undo depth npad's control uses
void richedit_configure_plain_text(HWND edit);

// UTF-8 to the UTF-16 form the control stores (breaks normalised to CR).
// Returns malloc'd, NUL-terminated text and its length in *out_len, or NULL if
// the text is too large to convert or memory runs out. *replaced (optional) is
// set when the input was not valid UTF-8 and bad bytes became U+FFFD.
wchar_t *richedit_text_from_utf8(const char *utf8, size_t *out_len, bool *replaced);

// UTF-16 (len units) to malloc'd UTF-8, or NULL on failure
char *richedit_text_to_utf8(const wchar_t *text, size_t len);

// Replaces the whole content with text (len units, breaks already normalised)
// via EM_SETTEXTEX. Returns true if the control now holds exactly that text,
// false if RichEdit changed anything on the way in (or refused it).
bool richedit_load_text(HWND edit, const wchar_t *text, size_t len);

// True if the control's content is exactly text (len units, CR breaks).
// Compares in chunks, so it needs no second copy of a large document.
bool richedit_holds_text(HWND edit, const wchar_t *text, size_t len);

// The whole content with CRLF line breaks, malloc'd and NUL-terminated, its
// length in *out_len (optional). NULL on failure, including a copy shorter
// than the control's own count.
wchar_t *richedit_get_text(HWND edit, size_t *out_len);

// As richedit_get_text, converted to UTF-8
char *richedit_get_text_utf8(HWND edit);
#endif

#endif // RICHEDIT_TEXT_H
