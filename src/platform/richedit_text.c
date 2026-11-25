/*
 * npad - RichEdit text boundary (Win32)
 * See richedit_text.h for why this is more than a UTF-8 <-> UTF-16 copy.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>
#include <limits.h>
#include <stdint.h>
#endif

#include "richedit_text.h"

#include <stdlib.h>
#include <string.h>

size_t richedit_normalize_breaks(wchar_t *text, size_t len) {
    if (!text)
        return 0;
    // Same break rules as file_convert_line_endings: a CR LF pair is one
    // break, a lone CR or a lone LF is one break each. Never grows, so in place.
    size_t out = 0;
    for (size_t in = 0; in < len; in++) {
        wchar_t c = text[in];
        if (c == L'\r') {
            text[out++] = L'\r';
            if (in + 1 < len && text[in + 1] == L'\n')
                in++;
        } else if (c == L'\n') {
            text[out++] = L'\r';
        } else {
            text[out++] = c;
        }
    }
    text[out] = L'\0';
    return out;
}

#ifdef _WIN32

// Units compared per EM_GETTEXTRANGE call when verifying a load
#define RICHEDIT_VERIFY_CHUNK 65536

void richedit_configure_plain_text(HWND edit) {
    SendMessageW(edit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessageW(edit, EM_EXLIMITTEXT, 0, RICHEDIT_TEXT_LIMIT);
    SendMessageW(edit, EM_SETUNDOLIMIT, 100000, 0);
}

wchar_t *richedit_text_from_utf8(const char *utf8, size_t *out_len, bool *replaced) {
    if (replaced)
        *replaced = false;
    if (out_len)
        *out_len = 0;
    if (!utf8)
        return NULL;

    // MultiByteToWideChar counts in int: a longer string cannot be converted
    // in one call, and silently converting part of it would load a fragment
    size_t bytes = strlen(utf8);
    if (bytes > (size_t) INT_MAX)
        return NULL;

    int units = 0;
    if (bytes > 0) {
        units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, (int) bytes, NULL, 0);
        if (units <= 0) {
            if (GetLastError() != ERROR_NO_UNICODE_TRANSLATION)
                return NULL;
            // Not valid UTF-8: load it with U+FFFD for the bad bytes, as before,
            // but tell the caller the control will not hold the input exactly
            if (replaced)
                *replaced = true;
            units = MultiByteToWideChar(CP_UTF8, 0, utf8, (int) bytes, NULL, 0);
            if (units <= 0)
                return NULL;
        }
    }

    wchar_t *wide = malloc(((size_t) units + 1) * sizeof(wchar_t));
    if (!wide)
        return NULL;
    if (units > 0) {
        DWORD flags = (replaced && *replaced) ? 0 : MB_ERR_INVALID_CHARS;
        if (MultiByteToWideChar(CP_UTF8, flags, utf8, (int) bytes, wide, units) != units) {
            free(wide);
            return NULL;
        }
    }
    wide[units] = L'\0';

    size_t len = richedit_normalize_breaks(wide, (size_t) units);
    if (out_len)
        *out_len = len;
    return wide;
}

char *richedit_text_to_utf8(const wchar_t *text, size_t len) {
    if (!text || len > (size_t) INT_MAX)
        return NULL;
    int bytes = 0;
    if (len > 0) {
        bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int) len, NULL, 0, NULL, NULL);
        if (bytes <= 0)
            return NULL;
    }
    char *utf8 = malloc((size_t) bytes + 1);
    if (!utf8)
        return NULL;
    if (len > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, text, (int) len, utf8, bytes, NULL, NULL) != bytes) {
        free(utf8);
        return NULL;
    }
    utf8[bytes] = '\0';
    return utf8;
}

bool richedit_load_text(HWND edit, const wchar_t *text, size_t len) {
    if (!edit || !text)
        return false;
    SETTEXTEX st;
    st.flags = ST_DEFAULT;
    st.codepage = 1200; // UTF-16
    SendMessageW(edit, EM_SETTEXTEX, (WPARAM) &st, (LPARAM) text);
    // The return value says only that something was set; whether it is the
    // text that was sent is what the caller needs to know
    return richedit_holds_text(edit, text, len);
}

bool richedit_holds_text(HWND edit, const wchar_t *text, size_t len) {
    if (!edit || !text || len > (size_t) RICHEDIT_TEXT_LIMIT)
        return false;

    // Without GTL_USECRLF each break counts once, as the control stores it
    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200;
    LRESULT have = SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM) &gtl, 0);
    if (have < 0 || (size_t) have != len)
        return false;
    if (len == 0)
        return true;

    wchar_t *chunk = malloc(((size_t) RICHEDIT_VERIFY_CHUNK + 1) * sizeof(wchar_t));
    if (!chunk)
        return false; // Cannot prove it, so do not claim it

    bool same = true;
    size_t pos = 0;
    while (pos < len) {
        size_t want = len - pos;
        if (want > RICHEDIT_VERIFY_CHUNK)
            want = RICHEDIT_VERIFY_CHUNK;
        // Never end a range between the halves of a surrogate pair
        if (want > 1 && pos + want < len && IS_HIGH_SURROGATE(text[pos + want - 1]))
            want--;
        TEXTRANGEW range;
        range.chrg.cpMin = (LONG) pos;
        range.chrg.cpMax = (LONG) (pos + want);
        range.lpstrText = chunk;
        LRESULT got = SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM) &range);
        if (got <= 0 || (size_t) got > want ||
            memcmp(chunk, text + pos, (size_t) got * sizeof(wchar_t)) != 0) {
            same = false;
            break;
        }
        pos += (size_t) got;
    }
    free(chunk);
    return same;
}

wchar_t *richedit_get_text(HWND edit, size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!edit)
        return NULL;

    // The stored form (GT_DEFAULT: one CR per break), not GT_USECRLF: that
    // flag also turns a stored VT into CR LF on the way out, so a vertical tab
    // the control holds intact would still be saved as a line break. The CR
    // breaks are widened to CR LF here instead, and nothing else is touched.
    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_NUMCHARS | GTL_PRECISE;
    gtl.codepage = 1200; // UTF-16
    LRESULT length = SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM) &gtl, 0);
    if (length < 0)
        return NULL;

    // GETTEXTEX.cb is a DWORD byte count including the terminator: a length
    // whose buffer does not fit would wrap to a small size
    size_t units = (size_t) length + 1;
    if (units > (size_t) MAXDWORD / sizeof(wchar_t))
        return NULL;

    wchar_t *wide = malloc(units * sizeof(wchar_t));
    if (!wide)
        return NULL;

    GETTEXTEX gt;
    memset(&gt, 0, sizeof(gt));
    gt.cb = (DWORD) (units * sizeof(wchar_t));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    LRESULT copied = SendMessageW(edit, EM_GETTEXTEX, (WPARAM) &gt, (LPARAM) wide);
    // Anything but the whole text is a failure: saving a short copy would
    // replace the file with the part that happened to be copied
    if (copied != length) {
        free(wide);
        return NULL;
    }
    size_t n = (size_t) copied;
    wide[n] = L'\0';

    size_t breaks = 0;
    for (size_t i = 0; i < n; i++)
        if (wide[i] == L'\r')
            breaks++;
    if (breaks > 0) {
        if (breaks > SIZE_MAX / sizeof(wchar_t) - units) {
            free(wide);
            return NULL;
        }
        wchar_t *grown = realloc(wide, (units + breaks) * sizeof(wchar_t));
        if (!grown) {
            free(wide);
            return NULL;
        }
        wide = grown;
        // Widen from the end, so every unit is read before it is overwritten
        size_t w = n + breaks;
        wide[w] = L'\0';
        for (size_t i = n; i-- > 0;) {
            if (wide[i] == L'\r') {
                wide[--w] = L'\n';
                wide[--w] = L'\r';
            } else {
                wide[--w] = wide[i];
            }
        }
        n += breaks;
    }
    if (out_len)
        *out_len = n;
    return wide;
}

char *richedit_get_text_utf8(HWND edit) {
    size_t len = 0;
    wchar_t *wide = richedit_get_text(edit, &len);
    if (!wide)
        return NULL;
    char *utf8 = richedit_text_to_utf8(wide, len);
    free(wide);
    return utf8;
}

#endif // _WIN32
