/*
 * npad - File Operations Implementation
 * Cross-platform file I/O operations
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef _WIN32
// POSIX.1-2008 under -std=c99: fsync, pread, lstat, readlink, O_NOFOLLOW,
// O_CLOEXEC, posix_fallocate and st_mtim. 64-bit off_t on 32-bit targets.
// (Not on Windows: MinGW turns _POSIX_C_SOURCE into its own printf.)
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#ifdef __APPLE__
// _POSIX_C_SOURCE alone hides the Darwin extensions this file uses there:
// st_mtimespec, O_NOFOLLOW and O_DIRECTORY
#define _DARWIN_C_SOURCE
#endif
#endif

#include "file_ops.h"
#include "error.h"
#include <errno.h>
#include <limits.h>
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif
#endif

// Maximum path length accepted (sanity bound only - the OS enforces real limits)
#define NPAD_MAX_PATH_LEN 4096

// Global error message
static char g_last_error[512] = { 0 };

static void set_error(const char *message) {
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

// The reason is passed in rather than read from errno here: anything between
// the failing call and this one (logging to a missing stderr in the Windows
// GUI, for one) can overwrite errno, and the message then names the wrong
// cause ("Bad file descriptor" for a file that simply does not exist).
static void set_errno_error_code(const char *operation, const char *filename, int errnum) {
    snprintf(g_last_error, sizeof(g_last_error), "%.100s '%.200s': %.150s",
             operation ? operation : "Unknown", filename ? filename : "Unknown", strerror(errnum));
}

static void set_errno_error(const char *operation, const char *filename) {
    set_errno_error_code(operation, filename, errno);
}

#ifdef _WIN32
// As set_errno_error_code, for a Win32 GetLastError() code, so a failed save
// says why ("Access is denied", "The process cannot access the file because it
// is being used by another process") instead of only that it failed
static void set_win32_error(const char *operation, const char *filename, DWORD code) {
    wchar_t wmsg[256];
    char reason[200];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
                             0, wmsg, (DWORD) (sizeof(wmsg) / sizeof(wmsg[0])), NULL);
    while (n > 0 && (wmsg[n - 1] == L'\r' || wmsg[n - 1] == L'\n' || wmsg[n - 1] == L' ' ||
                     wmsg[n - 1] == L'.')) {
        n--;
    }
    wmsg[n] = L'\0';
    if (n == 0 ||
        WideCharToMultiByte(CP_UTF8, 0, wmsg, -1, reason, (int) sizeof(reason), NULL, NULL) <= 0) {
        snprintf(reason, sizeof(reason), "Windows error %lu", (unsigned long) code);
    }
    snprintf(g_last_error, sizeof(g_last_error), "%.100s '%.200s': %.150s",
             operation ? operation : "Unknown", filename ? filename : "Unknown", reason);
}

// Paths at or beyond this many characters get the \\?\ prefix, which lifts
// MAX_PATH (260) for the wide APIs without needing the process to be
// long-path aware. Kept below MAX_PATH so a sibling temp name fits too.
#define NPAD_LONG_PATH_AT 240

// Give an over-long absolute wide path the \\?\ (or \\?\UNC\) prefix. Takes
// ownership of path and returns the path to use (possibly path itself).
static wchar_t *long_path_w(wchar_t *path) {
    if (!path || wcslen(path) < NPAD_LONG_PATH_AT || wcsncmp(path, L"\\\\?\\", 4) == 0 ||
        wcsncmp(path, L"\\\\.\\", 4) == 0) {
        return path;
    }
    // \\?\ turns off Win32 normalisation, so canonicalise first (separators,
    // "." and "..", relative to the working directory)
    DWORD need = GetFullPathNameW(path, 0, NULL, NULL);
    if (need == 0)
        return path;
    wchar_t *full = malloc(((size_t) need + 1) * sizeof(wchar_t));
    if (!full)
        return path;
    DWORD got = GetFullPathNameW(path, need + 1, full, NULL);
    if (got == 0 || got > need) {
        free(full);
        return path;
    }
    const wchar_t *prefix = NULL;
    const wchar_t *rest = full;
    if (full[0] == L'\\' && full[1] == L'\\') {
        prefix = L"\\\\?\\UNC\\";
        rest = full + 2;
    } else if (full[0] != L'\0' && full[1] == L':') {
        prefix = L"\\\\?\\";
    }
    if (!prefix) {
        free(path);
        return full;
    }
    size_t len = wcslen(prefix) + wcslen(rest) + 1;
    wchar_t *out = malloc(len * sizeof(wchar_t));
    if (!out) {
        free(full);
        return path;
    }
    wcscpy(out, prefix);
    wcscat(out, rest);
    free(full);
    free(path);
    return out;
}

// UTF-8 path to a malloc'd wide path for the W APIs (NULL on failure), with
// the long-path prefix where it is needed
static wchar_t *widen_path(const char *utf8) {
    if (!utf8)
        return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t *wide = malloc((size_t) n * sizeof(wchar_t));
    if (!wide)
        return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, n) <= 0) {
        free(wide);
        return NULL;
    }
    return long_path_w(wide);
}
#endif

// Minimal path sanity check. npad is a desktop editor: it must be able to
// open any path the user can access (including relative paths containing
// ".."), so no traversal filtering is done here.
static bool is_valid_path(const char *filename) {
    return filename && filename[0] != '\0' && strlen(filename) <= NPAD_MAX_PATH_LEN;
}

// Open a file honoring UTF-8 paths on Windows (fopen uses the ANSI code
// page there, which mangles non-ASCII filenames)
static FILE *open_file_utf8(const char *filename, const char *mode) {
#ifdef _WIN32
    wchar_t wpath[NPAD_MAX_PATH_LEN];
    wchar_t wmode[8];
    // MB_ERR_INVALID_CHARS: a path that is not UTF-8 (ANSI bytes from some
    // caller) must fail here, visibly, rather than become U+FFFD and fail as
    // "not found" - or, through a narrow fopen, name some other file
    int path_len =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1, wpath, NPAD_MAX_PATH_LEN);
    int mode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
    if (path_len > 0 && mode_len > 0) {
        return _wfopen(wpath, wmode);
    }
    errno = (GetLastError() == ERROR_NO_UNICODE_TRANSLATION) ? EILSEQ : ENAMETOOLONG;
    return NULL;
#else
    return fopen(filename, mode);
#endif
}

// ---------------------------------------------------------------------------
// UTF conversion helpers (portable, no OS dependencies, unit-testable)
// ---------------------------------------------------------------------------

// Length (1-4) of the well-formed UTF-8 sequence at p, or 0 if there is none
// there; then *bad_len is the length of its maximal ill-formed subpart (the
// lead byte plus any continuation bytes that were still acceptable), which is
// what one U+FFFD replaces - the Unicode-recommended practice, and what GTK
// and Windows do. Rejects overlongs, surrogates and code points above U+10FFFF
// via the second-byte ranges of Unicode's table 3-7.
static size_t utf8_sequence_length(const uint8_t *p, size_t remaining, size_t *bad_len) {
    uint8_t byte = p[0];
    if (byte < 0x80)
        return 1;

    size_t need;
    uint8_t second_lo = 0x80, second_hi = 0xBF;
    if (byte >= 0xC2 && byte <= 0xDF) {
        need = 2;
    } else if (byte >= 0xE0 && byte <= 0xEF) {
        need = 3;
        if (byte == 0xE0)
            second_lo = 0xA0; // Overlong below U+0800
        else if (byte == 0xED)
            second_hi = 0x9F; // Surrogates U+D800-U+DFFF
    } else if (byte >= 0xF0 && byte <= 0xF4) {
        need = 4;
        if (byte == 0xF0)
            second_lo = 0x90; // Overlong below U+10000
        else if (byte == 0xF4)
            second_hi = 0x8F; // Above U+10FFFF
    } else {
        *bad_len = 1; // Stray continuation byte, C0/C1 or F5-FF
        return 0;
    }

    size_t k = 1;
    while (k < need && k < remaining) {
        uint8_t lo = (k == 1) ? second_lo : 0x80;
        uint8_t hi = (k == 1) ? second_hi : 0xBF;
        if (p[k] < lo || p[k] > hi)
            break;
        k++;
    }
    if (k == need)
        return need;
    *bad_len = k;
    return 0;
}

static bool utf8_is_valid(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t bad_len = 0;
        size_t n = utf8_sequence_length(data + i, len - i, &bad_len);
        if (n == 0)
            return false;
        i += n;
    }
    return true;
}

// Copy UTF-8 bytes into a NUL-terminated string that is guaranteed valid,
// replacing each ill-formed subpart with U+FFFD. *lossy is set if anything was
// replaced, since the original bytes then cannot be written back.
static char *utf8_sanitize(const uint8_t *data, size_t len, bool *lossy) {
    // Worst case: every byte is a lone bad byte and becomes 3 (EF BF BD)
    if (len > (SIZE_MAX - 1) / 3)
        return NULL;
    char *out = malloc(len * 3 + 1);
    if (!out)
        return NULL;

    size_t out_pos = 0;
    size_t i = 0;
    while (i < len) {
        size_t bad_len = 0;
        size_t n = utf8_sequence_length(data + i, len - i, &bad_len);
        if (n > 0) {
            memcpy(out + out_pos, data + i, n);
            out_pos += n;
            i += n;
        } else {
            out[out_pos++] = (char) 0xEF;
            out[out_pos++] = (char) 0xBF;
            out[out_pos++] = (char) 0xBD;
            i += bad_len;
            *lossy = true;
        }
    }
    out[out_pos] = '\0';
    return out;
}

// Append a code point to a UTF-8 buffer, returns bytes written
static size_t utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char) (0xF0 | (cp >> 18));
        out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char) (0x80 | (cp & 0x3F));
        return 4;
    }
}

// Decode one code point from UTF-8, advancing *i. Invalid input yields U+FFFD.
static uint32_t utf8_decode(const uint8_t *data, size_t len, size_t *i) {
    uint8_t byte = data[*i];
    if (byte < 0x80) {
        (*i)++;
        return byte;
    }
    if ((byte & 0xE0) == 0xC0 && *i + 1 < len) {
        uint32_t cp = ((uint32_t) (byte & 0x1F) << 6) | (data[*i + 1] & 0x3F);
        *i += 2;
        return cp;
    }
    if ((byte & 0xF0) == 0xE0 && *i + 2 < len) {
        uint32_t cp = ((uint32_t) (byte & 0x0F) << 12) | ((uint32_t) (data[*i + 1] & 0x3F) << 6) |
                      (data[*i + 2] & 0x3F);
        *i += 3;
        return cp;
    }
    if ((byte & 0xF8) == 0xF0 && *i + 3 < len) {
        uint32_t cp = ((uint32_t) (byte & 0x07) << 18) | ((uint32_t) (data[*i + 1] & 0x3F) << 12) |
                      ((uint32_t) (data[*i + 2] & 0x3F) << 6) | (data[*i + 3] & 0x3F);
        *i += 4;
        return cp;
    }
    (*i)++;
    return 0xFFFD;
}

static uint16_t read_u16(const uint8_t *p, bool big_endian) {
    return big_endian ? (uint16_t) ((p[0] << 8) | p[1]) : (uint16_t) ((p[1] << 8) | p[0]);
}

static void write_u16(uint8_t *p, uint16_t value, bool big_endian) {
    if (big_endian) {
        p[0] = (uint8_t) (value >> 8);
        p[1] = (uint8_t) (value & 0xFF);
    } else {
        p[0] = (uint8_t) (value & 0xFF);
        p[1] = (uint8_t) (value >> 8);
    }
}

// Convert UTF-16 bytes (without BOM) to a NUL-terminated UTF-8 string.
// An unpaired surrogate, or a final byte left over from an odd byte count,
// becomes U+FFFD and sets *lossy: UTF-8 cannot carry either, so a save could
// not write the original bytes back.
static char *utf16_to_utf8(const uint8_t *data, size_t byte_len, bool big_endian, bool *lossy) {
    size_t unit_count = byte_len / 2;
    // Worst case: every UTF-16 unit becomes 3 UTF-8 bytes (surrogate pairs:
    // 2 units -> 4 bytes), plus 3 for a dangling odd byte
    char *out = malloc(unit_count * 3 + 3 + 1);
    if (!out)
        return NULL;

    size_t out_pos = 0;
    size_t i = 0;
    while (i < unit_count) {
        uint16_t unit = read_u16(data + i * 2, big_endian);
        uint32_t cp;

        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < unit_count) {
            uint16_t low = read_u16(data + (i + 1) * 2, big_endian);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + (((uint32_t) (unit - 0xD800) << 10) | (low - 0xDC00));
                i += 2;
            } else {
                cp = 0xFFFD; // Unpaired high surrogate
                *lossy = true;
                i++;
            }
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            cp = 0xFFFD; // Unpaired surrogate
            *lossy = true;
            i++;
        } else {
            cp = unit;
            i++;
        }

        out_pos += utf8_encode(cp, out + out_pos);
    }

    if (byte_len % 2 != 0) {
        out_pos += utf8_encode(0xFFFD, out + out_pos); // Half a code unit
        *lossy = true;
    }

    out[out_pos] = '\0';
    return out;
}

// Convert a NUL-terminated UTF-8 string to UTF-16 bytes, with a BOM first
// when with_bom. Returns malloc'd buffer, sets *out_len to the byte length.
static uint8_t *utf8_to_utf16(const char *utf8, bool big_endian, bool with_bom, size_t *out_len) {
    size_t len = strlen(utf8);
    // Worst case: every UTF-8 byte becomes one UTF-16 unit, plus BOM
    uint8_t *out = malloc(len * 2 + 4);
    if (!out)
        return NULL;

    size_t out_pos = 0;
    if (with_bom) {
        write_u16(out, 0xFEFF, big_endian);
        out_pos = 2;
    }

    size_t i = 0;
    while (i < len) {
        uint32_t cp = utf8_decode((const uint8_t *) utf8, len, &i);
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            write_u16(out + out_pos, (uint16_t) (0xD800 | (v >> 10)), big_endian);
            write_u16(out + out_pos + 2, (uint16_t) (0xDC00 | (v & 0x3FF)), big_endian);
            out_pos += 4;
        } else {
            write_u16(out + out_pos, (uint16_t) cp, big_endian);
            out_pos += 2;
        }
    }

    *out_len = out_pos;
    return out;
}

// ---------------------------------------------------------------------------
// ANSI conversion
// ---------------------------------------------------------------------------

#ifdef _WIN32
// The code page npad reads and writes "ANSI" files in: the system's ANSI code
// page, except when that is UTF-8 (Windows' "Beta: Use Unicode UTF-8 for
// worldwide language support"). A file only reaches the ANSI branch because it
// is NOT valid UTF-8, so decoding it as UTF-8 would turn every such byte into
// U+FFFD and the save would write EF BF BD over each one. The system locale's
// legacy ANSI code page is what such files were written in (1252 if it has
// none).
static UINT ansi_code_page(void) {
    UINT acp = GetACP();
    if (acp != CP_UTF8)
        return acp;
    DWORD legacy = 0;
    if (GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                       (LPWSTR) &legacy, (int) (sizeof(legacy) / sizeof(WCHAR))) > 0 &&
        legacy != 0 && legacy != CP_UTF8 && IsValidCodePage((UINT) legacy)) {
        return (UINT) legacy;
    }
    return 1252;
}

// Would saving this text in code page cp (as utf8_to_ansi does) write back
// exactly these bytes? False on any doubt, including running out of memory.
static bool ansi_roundtrips(UINT cp, const wchar_t *wide, int wide_len, const uint8_t *data,
                            size_t len) {
    BOOL used_default = FALSE;
    int n =
        WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide, wide_len, NULL, 0, NULL, &used_default);
    if (n <= 0 || (size_t) n != len || used_default)
        return false;
    char *back = malloc((size_t) n);
    if (!back)
        return false;
    bool same =
        WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide, wide_len, back, n, NULL, NULL) == n &&
        memcmp(back, data, len) == 0;
    free(back);
    return same;
}

// Decode bytes in a double-byte code page one line at a time, so an invalid
// lead byte at the end of a line cannot swallow the CR or LF after it and
// merge two lines. In the DBCS code pages (932, 936, 949, 950) a trail byte
// is always 0x40 or above, so a CR or LF byte is never part of a valid
// character, and each byte yields at most one UTF-16 unit. NULL on failure.
static wchar_t *ansi_decode_by_line(UINT cp, const uint8_t *data, size_t len, int *out_units) {
    wchar_t *wide = malloc(len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    size_t pos = 0;
    size_t i = 0;
    while (i < len) {
        if (data[i] == '\r' || data[i] == '\n') {
            wide[pos++] = (wchar_t) data[i++];
            continue;
        }
        size_t start = i;
        while (i < len && data[i] != '\r' && data[i] != '\n')
            i++;
        int n = MultiByteToWideChar(cp, 0, (const char *) data + start, (int) (i - start),
                                    wide + pos, (int) (len - pos));
        if (n <= 0) {
            free(wide);
            return NULL;
        }
        pos += (size_t) n;
    }
    *out_units = (int) pos;
    return wide;
}
#else
// Windows-1252 bytes 0x80-0x9F. Elsewhere it matches Latin-1 (U+00A0-U+00FF
// are the same bytes), but these hold the curly quotes, dashes and euro sign
// that Windows "ANSI" text is full of; read as Latin-1 they would become
// invisible C1 controls. The five bytes 1252 leaves undefined map to the C1
// control of the same value, as Windows does, so every byte round-trips.
static const uint16_t cp1252_80_9f[32] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020,
                                           0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D,
                                           0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D,
                                           0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
                                           0x0153, 0x009D, 0x017E, 0x0178 };

// The Windows-1252 byte for a code point, or false if 1252 has none
static bool cp1252_from_unicode(uint32_t cp, uint8_t *byte) {
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
        *byte = (uint8_t) cp;
        return true;
    }
    for (size_t k = 0; k < sizeof(cp1252_80_9f) / sizeof(cp1252_80_9f[0]); k++) {
        if (cp1252_80_9f[k] == cp) {
            *byte = (uint8_t) (0x80 + k);
            return true;
        }
    }
    return false;
}
#endif

// Convert "ANSI" bytes (see TextEncoding) to UTF-8, for files that are not
// valid UTF-8. Sets *lossy when the decoded text would not save back to the
// same bytes (an invalid sequence in a multi-byte code page). Returns NULL and
// sets the error on failure - never an empty string for a non-empty file,
// which the editor would then save over the original.
static char *ansi_to_utf8(const uint8_t *data, size_t len, bool *lossy) {
#ifdef _WIN32
    if (len == 0) {
        char *empty = malloc(1);
        if (empty)
            empty[0] = '\0';
        return empty;
    }
    // Windows' conversions count in int, so beyond INT_MAX they fail outright
    if (len > (size_t) INT_MAX) {
        set_error("The file is too large to convert from ANSI");
        return NULL;
    }

    UINT cp = ansi_code_page();
    int wide_len = MultiByteToWideChar(cp, 0, (const char *) data, (int) len, NULL, 0);
    if (wide_len <= 0) {
        set_error("Failed to convert file contents from ANSI");
        return NULL;
    }
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide) {
        set_error("Out of memory");
        return NULL;
    }
    if (MultiByteToWideChar(cp, 0, (const char *) data, (int) len, wide, wide_len) != wide_len) {
        free(wide);
        set_error("Failed to convert file contents from ANSI");
        return NULL;
    }

    if (!ansi_roundtrips(cp, wide, wide_len, data, len)) {
        *lossy = true;
        CPINFO cp_info;
        if (GetCPInfo(cp, &cp_info) && cp_info.MaxCharSize == 2) {
            int line_units = 0;
            wchar_t *by_line = ansi_decode_by_line(cp, data, len, &line_units);
            if (by_line) {
                free(wide);
                wide = by_line;
                wide_len = line_units;
            }
        }
    }

    // wide_len is at least 1 here: the input is non-empty and every decode
    // above yields a unit per character
    char *out = NULL;
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
        // The UTF-8 form exceeds INT_MAX (a 1.2-2 GB file of high bytes)
        set_error("The file is too large to convert from ANSI");
    } else {
        out = malloc((size_t) utf8_len + 1);
        if (!out) {
            set_error("Out of memory");
        } else if (WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, out, utf8_len, NULL, NULL) !=
                   utf8_len) {
            set_error("Failed to convert file contents from ANSI");
            free(out);
            out = NULL;
        } else {
            out[utf8_len] = '\0';
        }
    }
    free(wide);
    return out;
#else
    (void) lossy; // Every Windows-1252 byte decodes to a character that encodes back to it
    if (len > (SIZE_MAX - 1) / 3) {
        set_error("The file is too large to convert from ANSI");
        return NULL;
    }
    // Worst case 3 UTF-8 bytes per byte (0x80 is U+20AC)
    char *out = malloc(len * 3 + 1);
    if (!out) {
        set_error("Out of memory");
        return NULL;
    }
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        uint32_t cp = (b >= 0x80 && b <= 0x9F) ? cp1252_80_9f[b - 0x80] : b;
        out_pos += utf8_encode(cp, out + out_pos);
    }
    out[out_pos] = '\0';
    return out;
#endif
}

// Convert UTF-8 to "ANSI" bytes for saving ANSI files. Characters the code
// page lacks become '?' - never a "best fit" look-alike (Windows would
// otherwise write U+221E INFINITY as '8' and U+0142 as 'l', silently changing
// the meaning), so this writes exactly what file_ansi_is_lossy warned about.
static uint8_t *utf8_to_ansi(const char *utf8, size_t *out_len) {
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0)
        return NULL;
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_len);

    UINT cp = ansi_code_page();
    int ansi_len = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (ansi_len <= 0) {
        free(wide);
        return NULL;
    }
    uint8_t *out = malloc((size_t) ansi_len);
    if (!out) {
        free(wide);
        return NULL;
    }
    int written =
        WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, wide, -1, (char *) out, ansi_len, NULL, NULL);
    free(wide);
    if (written != ansi_len) {
        free(out);
        return NULL;
    }
    *out_len = (size_t) ansi_len - 1; // Exclude NUL
    return out;
#else
    size_t len = strlen(utf8);
    uint8_t *out = malloc(len + 1);
    if (!out)
        return NULL;
    size_t out_pos = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp = utf8_decode((const uint8_t *) utf8, len, &i);
        uint8_t byte;
        out[out_pos++] = cp1252_from_unicode(cp, &byte) ? byte : (uint8_t) '?';
    }
    out[out_pos] = '\0';
    *out_len = out_pos;
    return out;
#endif
}

bool file_ansi_is_lossy(const char *utf8) {
    if (!utf8)
        return false;
#ifdef _WIN32
    // Convert exactly as utf8_to_ansi does and see whether any character had
    // to fall back to the default character
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0)
        return false;
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide)
        return false;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_len);

    BOOL used_default = FALSE;
    WideCharToMultiByte(ansi_code_page(), WC_NO_BEST_FIT_CHARS, wide, -1, NULL, 0, NULL,
                        &used_default);
    free(wide);
    return used_default != FALSE;
#else
    // Windows-1252 model (matches utf8_to_ansi)
    size_t len = strlen(utf8);
    size_t i = 0;
    while (i < len) {
        uint8_t byte;
        if (!cp1252_from_unicode(utf8_decode((const uint8_t *) utf8, len, &i), &byte)) {
            return true;
        }
    }
    return false;
#endif
}

// Heuristic for UTF-16 files without a BOM. Text that is mostly ASCII or
// Latin-1 has a zero high byte in most code units: the odd bytes for little
// endian, the even ones for big endian: over 40% of the units must have one.
// Units whose LOW byte is zero instead (U+0100, U+4E00, ...) are fine while
// they are fewer, and only decide the byte order. A zero code unit never
// appears in text but is common in binary data of this shape, so more than a
// few of those rule UTF-16 out. An odd final byte is left to the decoder,
// which flags it.
static bool looks_like_utf16(const uint8_t *data, size_t len, bool *big_endian) {
    if (len < 4)
        return false;

    size_t sample = len < 512 ? len : 512;
    size_t pairs = sample / 2;
    size_t odd_zero = 0, even_zero = 0, zero_units = 0;
    for (size_t p = 0; p < pairs; p++) {
        bool even_is_zero = data[p * 2] == 0;
        bool odd_is_zero = data[p * 2 + 1] == 0;
        if (even_is_zero && odd_is_zero)
            zero_units++;
        else if (odd_is_zero)
            odd_zero++; // U+0001-U+00FF in little endian
        else if (even_is_zero)
            even_zero++; // U+0001-U+00FF in big endian
    }
    if (zero_units * 10 >= pairs)
        return false;
    if (odd_zero * 10 > pairs * 4 && even_zero < odd_zero) {
        *big_endian = false;
        return true;
    }
    if (even_zero * 10 > pairs * 4 && odd_zero < even_zero) {
        *big_endian = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Line ending helpers
// ---------------------------------------------------------------------------

LineEnding file_detect_line_ending(const char *text) {
    size_t crlf = 0, lf = 0, cr = 0;

    for (const char *p = text; *p; p++) {
        if (*p == '\r') {
            if (*(p + 1) == '\n') {
                crlf++;
                p++;
            } else {
                cr++;
            }
        } else if (*p == '\n') {
            lf++;
        }
    }

    if (crlf + lf + cr == 0)
        return NPAD_DEFAULT_LINE_ENDING; // Nothing to detect: the platform's own

    // The majority style wins; the editor holds one kind of line break, so a
    // file with mixed endings is saved in that style throughout. A tie goes
    // to a style the file actually uses: the platform's own if it is one of
    // the tied, else CRLF, LF, CR in that order. Never to one that is absent
    // ("a\nb\r" must not become CRLF).
    size_t max = crlf > lf ? crlf : lf;
    if (cr > max)
        max = cr;
    const size_t counts[3] = { crlf, lf, cr }; // Indexed by LineEnding
    if (counts[NPAD_DEFAULT_LINE_ENDING] == max)
        return NPAD_DEFAULT_LINE_ENDING;
    if (crlf == max)
        return NPAD_EOL_CRLF;
    if (lf == max)
        return NPAD_EOL_LF;
    return NPAD_EOL_CR;
}

char *file_convert_line_endings(const char *text, LineEnding target) {
    if (!text)
        return NULL;

    const char *eol = (target == NPAD_EOL_CRLF) ? "\r\n" : (target == NPAD_EOL_LF) ? "\n" : "\r";
    size_t eol_len = strlen(eol);

    // Count line breaks of any style
    size_t breaks = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\r') {
            breaks++;
            if (*(p + 1) == '\n')
                p++;
        } else if (*p == '\n') {
            breaks++;
        }
    }

    char *out = malloc(strlen(text) + breaks * eol_len + 1);
    if (!out)
        return NULL;

    char *dst = out;
    for (const char *p = text; *p; p++) {
        if (*p == '\r') {
            memcpy(dst, eol, eol_len);
            dst += eol_len;
            if (*(p + 1) == '\n')
                p++;
        } else if (*p == '\n') {
            memcpy(dst, eol, eol_len);
            dst += eol_len;
        } else {
            *dst++ = *p;
        }
    }
    *dst = '\0';

    return out;
}

const char *file_encoding_name(TextEncoding encoding) {
    switch (encoding) {
        case NPAD_ENC_UTF8:
            return "UTF-8";
        case NPAD_ENC_UTF8_BOM:
            return "UTF-8 with BOM";
        case NPAD_ENC_UTF16_LE:
            return "UTF-16 LE";
        case NPAD_ENC_UTF16_BE:
            return "UTF-16 BE";
        case NPAD_ENC_ANSI:
            return "ANSI";
        default:
            return "Unknown";
    }
}

const char *file_line_ending_name(LineEnding line_ending) {
    switch (line_ending) {
        case NPAD_EOL_CRLF:
            return "Windows (CRLF)";
        case NPAD_EOL_LF:
            return "Unix (LF)";
        case NPAD_EOL_CR:
            return "Mac (CR)";
        default:
            return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// Is the path an existing folder? (Following symlinks.)
static bool path_is_directory(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    wchar_t *wpath = widen_path(filename);
    if (!wpath)
        return false;
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(filename, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void set_folder_error(const char *filename) {
    snprintf(g_last_error, sizeof(g_last_error), "'%.300s' is a folder, not a file",
             filename ? filename : "Unknown");
}

// The size of an open stream's file, 64-bit on every platform: a 32-bit
// ftell fails or misleads past 2 GB on Windows. Fails with EISDIR for a
// folder, which fopen opens without complaint on Linux (ftell then says
// LONG_MAX, and the read tried to allocate 8 EB).
static bool stream_size(FILE *file, unsigned long long *out) {
#ifdef _WIN32
    intptr_t os_handle = _get_osfhandle(_fileno(file));
    LARGE_INTEGER li;
    if (os_handle == -1 || !GetFileSizeEx((HANDLE) os_handle, &li) || li.QuadPart < 0) {
        errno = EIO;
        return false;
    }
    *out = (unsigned long long) li.QuadPart;
    return true;
#else
    struct stat st;
    if (fstat(fileno(file), &st) != 0)
        return false;
    if (S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        return false;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 0) {
        errno = EINVAL; // A pipe or device has no size to read up to
        return false;
    }
    *out = (unsigned long long) st.st_size;
    return true;
#endif
}

// Read entire file into a malloc'd buffer with one extra NUL byte appended.
// Returns NULL on failure and sets the error message.
static uint8_t *read_all_bytes(const char *filename, size_t *out_size) {
    if (!is_valid_path(filename)) {
        NPAD_ERROR_ERROR(NPAD_ERROR_INVALID_PARAM, 0, filename ? filename : "(null)",
                         "Invalid file path");
        set_error("Invalid file path");
        return NULL;
    }

    FILE *file = open_file_utf8(filename, "rb");
    if (!file) {
        int err = errno; // Before logging, which can itself change errno
        if (path_is_directory(filename)) {
            set_folder_error(filename); // Windows refuses a folder with EACCES
        } else {
            set_errno_error_code("Failed to open file", filename, err);
        }
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, err, filename, "Failed to open file for reading: %s",
                         strerror(err));
        return NULL;
    }

    unsigned long long size = 0;
    if (!stream_size(file, &size)) {
        int err = errno;
        if (err == EISDIR) {
            set_folder_error(filename);
        } else {
            set_errno_error_code("Failed to get file size", filename, err);
        }
        fclose(file);
        return NULL;
    }

    // npad holds a document as one string and its text views count in int,
    // so a file past 2 GB cannot be loaded - say so rather than fail later
    if (size > FILE_MAX_LOAD_BYTES || size >= (unsigned long long) SIZE_MAX) {
        snprintf(g_last_error, sizeof(g_last_error),
                 "'%.300s' is too large to open (%.1f GB; npad opens files up to 2 GB)", filename,
                 (double) size / (1024.0 * 1024.0 * 1024.0));
        fclose(file);
        return NULL;
    }

    uint8_t *content = malloc((size_t) size + 1);
    if (!content) {
        NPAD_ERROR_MEMORY_ALLOC(filename);
        set_error("Out of memory");
        fclose(file);
        return NULL;
    }

    size_t total_read = 0;
    const size_t CHUNK_SIZE = 65536;

    while (total_read < (size_t) size) {
        size_t to_read = (size_t) size - total_read;
        if (to_read > CHUNK_SIZE)
            to_read = CHUNK_SIZE;

        size_t chunk_read = fread(content + total_read, 1, to_read, file);
        if (chunk_read == 0) {
            if (feof(file))
                break;
            set_errno_error("I/O error while reading", filename);
            free(content);
            fclose(file);
            return NULL;
        }
        total_read += chunk_read;
    }

    fclose(file);

    if (total_read != (size_t) size) {
        // Shrank while being read: not an errno condition, so say what happened
        snprintf(g_last_error, sizeof(g_last_error),
                 "Failed to read '%.300s' completely: it changed while being read", filename);
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, 0, filename,
                         "Failed to read complete file: expected %llu bytes, got %zu bytes", size,
                         total_read);
        free(content);
        return NULL;
    }

    content[total_read] = '\0';
    *out_size = total_read;
    return content;
}

char *file_read_text(const char *filename) {
    size_t size;
    return (char *) read_all_bytes(filename, &size);
}

char *file_read_text_limited(const char *filename, size_t max_bytes) {
    if (!is_valid_path(filename) || max_bytes == 0)
        return NULL;

    FILE *file = open_file_utf8(filename, "rb");
    if (!file) {
        set_errno_error("Failed to open file", filename);
        return NULL;
    }
    unsigned long long size = 0;
    if (!stream_size(file, &size) && errno == EISDIR) {
        set_folder_error(filename); // A folder reads as nothing, not as an empty file
        fclose(file);
        return NULL;
    }

    char *buf = malloc(max_bytes + 1);
    if (!buf) {
        fclose(file);
        return NULL;
    }

    size_t got = fread(buf, 1, max_bytes, file);
    fclose(file);
    buf[got] = 0; // NUL-terminate the prefix
    return buf;
}

bool file_get_stamp(const char *filename, FileStamp *out) {
    if (!out)
        return false;
    out->size = -1;
    out->write_time = 0;
    out->device = 0;
    out->inode = 0;
    if (!is_valid_path(filename))
        return false;

#ifdef _WIN32
    wchar_t *wpath = widen_path(filename);
    if (!wpath)
        return false;
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL got = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
    free(wpath);
    if (!got)
        return false;
    out->size = (long long) (((unsigned long long) data.nFileSizeHigh << 32) | data.nFileSizeLow);
    out->write_time =
        (long long) (((unsigned long long) data.ftLastWriteTime.dwHighDateTime << 32) |
                     data.ftLastWriteTime.dwLowDateTime);
    return true;
#else
    struct stat st;
    if (stat(filename, &st) != 0)
        return false;
    out->size = (long long) st.st_size;
    // Nanoseconds, not whole seconds: a same-size rewrite within the second
    // (sed -i, a formatter running just after a save) must still register
#ifdef __APPLE__
    out->write_time =
        (long long) st.st_mtimespec.tv_sec * 1000000000LL + (long long) st.st_mtimespec.tv_nsec;
#else
    out->write_time = (long long) st.st_mtim.tv_sec * 1000000000LL + (long long) st.st_mtim.tv_nsec;
#endif
    // A file replaced by rename is a different inode even when size and time
    // happen to match
    out->device = (unsigned long long) st.st_dev;
    out->inode = (unsigned long long) st.st_ino;
    return true;
#endif
}

bool file_stamp_equal(const FileStamp *a, const FileStamp *b) {
    if (!a || !b)
        return false;
    return a->size == b->size && a->write_time == b->write_time && a->device == b->device &&
           a->inode == b->inode;
}

#ifdef _WIN32
// The volume and file index identify a file however its path is spelled
// (case, 8.3 short names, a different drive mapping, a hard link).
static bool file_identity(const char *filename, BY_HANDLE_FILE_INFORMATION *out) {
    wchar_t *wpath = widen_path(filename); // Long-path aware, like every other open
    if (!wpath)
        return false;
    // No access rights requested: only the metadata is read, so this works on
    // a file another program holds open and never blocks it
    HANDLE h = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wpath);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool ok = GetFileInformationByHandle(h, out) != 0;
    CloseHandle(h);
    return ok;
}
#endif

bool file_same(const char *a, const char *b) {
    if (!is_valid_path(a) || !is_valid_path(b))
        return false;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION ia, ib;
    if (!file_identity(a, &ia) || !file_identity(b, &ib))
        return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
           ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
#else
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

bool file_rename(const char *from, const char *to) {
    if (!is_valid_path(from) || !is_valid_path(to))
        return false;
#ifdef _WIN32
    wchar_t *wfrom = widen_path(from);
    wchar_t *wto = widen_path(to);
    bool ok = wfrom && wto && MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok)
        set_win32_error("Failed to rename", from,
                        wfrom && wto ? GetLastError() : ERROR_OUTOFMEMORY);
    free(wfrom);
    free(wto);
    return ok;
#else
    if (rename(from, to) != 0) {
        set_errno_error("Failed to rename", from);
        return false;
    }
    return true;
#endif
}

// Does the decoded text stop short of the file's real content? npad carries
// text as NUL-terminated strings, so a NUL anywhere in a file silently cuts
// everything after it - a 120 KB PNG loads as nine bytes. Detected here, at
// decode time, rather than from the binary-looking heuristic: a file can hold
// a NUL without looking binary at all.
static bool raw_has_embedded_nul(const uint8_t *raw, size_t size, TextEncoding enc) {
    if (!raw || size == 0)
        return false;
    if (enc == NPAD_ENC_UTF16_LE || enc == NPAD_ENC_UTF16_BE) {
        // A zero CODE UNIT, not a zero byte - every ASCII character in UTF-16
        // contains one of those legitimately.
        bool be = (enc == NPAD_ENC_UTF16_BE);
        for (size_t i = 0; i + 1 < size; i += 2) {
            uint16_t unit = be ? (uint16_t) ((raw[i] << 8) | raw[i + 1])
                               : (uint16_t) ((raw[i + 1] << 8) | raw[i]);
            if (unit == 0)
                return true;
        }
        return false;
    }
    for (size_t i = 0; i < size; i++) {
        if (raw[i] == 0)
            return true;
    }
    return false;
}

char *file_read_text_ex(const char *filename, TextFileInfo *info) {
    size_t size;
    uint8_t *raw = read_all_bytes(filename, &size);
    if (!raw)
        return NULL;

    TextEncoding encoding;
    char *utf8 = NULL;
    bool truncated = false;
    bool lossy = false;
    bool no_bom = false;
    g_last_error[0] = '\0'; // So a converter's own reason is not overwritten below

    if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        // The BOM is taken at its word, as notepad.exe does, but the bytes
        // after it are not: an invalid one is shown as U+FFFD and flagged,
        // since a save would write EF BF BD in its place
        encoding = NPAD_ENC_UTF8_BOM;
        if (utf8_is_valid(raw + 3, size - 3)) {
            utf8 = malloc(size - 3 + 1);
            if (utf8) {
                memcpy(utf8, raw + 3, size - 3);
                utf8[size - 3] = '\0';
            }
        } else {
            utf8 = utf8_sanitize(raw + 3, size - 3, &lossy);
        }
    } else if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        encoding = NPAD_ENC_UTF16_LE;
        utf8 = utf16_to_utf8(raw + 2, size - 2, false, &lossy);
    } else if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        encoding = NPAD_ENC_UTF16_BE;
        utf8 = utf16_to_utf8(raw + 2, size - 2, true, &lossy);
    } else {
        bool big_endian;
        if (looks_like_utf16(raw, size, &big_endian)) {
            encoding = big_endian ? NPAD_ENC_UTF16_BE : NPAD_ENC_UTF16_LE;
            no_bom = true; // Saved back without one, so the bytes round-trip
            utf8 = utf16_to_utf8(raw, size, big_endian, &lossy);
        } else if (utf8_is_valid(raw, size)) {
            // No length weighting, as in notepad.exe: a short GBK, UHC or Big5
            // file whose bytes happen to form valid UTF-8 opens as UTF-8.
            // Preferring the ANSI code page instead would misdetect genuine
            // short UTF-8 such as "café" on CJK systems.
            encoding = NPAD_ENC_UTF8;
            truncated = raw_has_embedded_nul(raw, size, encoding);
            utf8 = (char *) raw;
            raw = NULL; // Ownership transferred (check it BEFORE handing it over)
        } else {
            encoding = NPAD_ENC_ANSI;
            utf8 = ansi_to_utf8(raw, size, &lossy);
        }
    }

    if (raw) { // NULL only on the UTF-8 branch above, which already checked
        truncated = raw_has_embedded_nul(raw, size, encoding);
    }
    free(raw);

    if (!utf8) {
        if (g_last_error[0] == '\0')
            set_error("Failed to convert file contents");
        return NULL;
    }

    if (info) {
        memset(info, 0, sizeof(*info));
        info->encoding = encoding;
        info->line_ending = file_detect_line_ending(utf8);
        info->truncated = truncated;
        info->decode_lossy = lossy;
        info->utf16_no_bom = no_bom;
    }

    return utf8;
}

bool file_read_binary(const char *filename, void **data, size_t *size) {
    if (!filename || !data || !size) {
        set_error("Invalid parameters");
        return false;
    }

    size_t read_size;
    uint8_t *buffer = read_all_bytes(filename, &read_size);
    if (!buffer)
        return false;

    *data = buffer;
    *size = read_size;
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

// There is no plain truncating write any more: fopen("wb") empties the
// destination before a byte of the new content exists, so a full disk or a
// failed write left an empty or partial file. Every write goes through the
// atomic writer (file_write_binary_atomic, below).

bool file_write_text(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters");
        return false;
    }
    return file_write_binary_atomic(filename, content, strlen(content));
}

bool file_write_binary(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters");
        return false;
    }
    return file_write_binary_atomic(filename, data, size);
}

// Convert UTF-8 content to the target encoding/line endings as raw bytes.
// Returns malloc'd buffer, sets *out_len.
static uint8_t *encode_text(const char *utf8_content, const TextFileInfo *info, size_t *out_len) {
    TextFileInfo defaults = { .encoding = NPAD_ENC_UTF8, .line_ending = NPAD_DEFAULT_LINE_ENDING };
    if (!info)
        info = &defaults;

    char *converted = file_convert_line_endings(utf8_content, info->line_ending);
    if (!converted)
        return NULL;

    uint8_t *bytes = NULL;
    size_t len = 0;

    switch (info->encoding) {
        case NPAD_ENC_UTF8:
            bytes = (uint8_t *) converted;
            len = strlen(converted);
            converted = NULL; // Ownership transferred
            break;

        case NPAD_ENC_UTF8_BOM: {
            size_t content_len = strlen(converted);
            bytes = malloc(content_len + 3);
            if (bytes) {
                bytes[0] = 0xEF;
                bytes[1] = 0xBB;
                bytes[2] = 0xBF;
                memcpy(bytes + 3, converted, content_len);
                len = content_len + 3;
            }
            break;
        }

        case NPAD_ENC_UTF16_LE:
            bytes = utf8_to_utf16(converted, false, !info->utf16_no_bom, &len);
            break;

        case NPAD_ENC_UTF16_BE:
            bytes = utf8_to_utf16(converted, true, !info->utf16_no_bom, &len);
            break;

        case NPAD_ENC_ANSI:
            bytes = utf8_to_ansi(converted, &len);
            break;
    }

    free(converted);

    if (!bytes) {
        set_error("Failed to encode file contents");
        return NULL;
    }

    *out_len = len;
    return bytes;
}

bool file_write_text_ex(const char *filename, const char *utf8_content, const TextFileInfo *info) {
    if (!filename || !utf8_content) {
        set_error("Invalid parameters");
        return false;
    }

    size_t len;
    uint8_t *bytes = encode_text(utf8_content, info, &len);
    if (!bytes)
        return false;

    bool result = file_write_binary_atomic(filename, bytes, len);
    free(bytes);
    return result;
}

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

// Windows: what _waccess reports (any existing path is readable; writable
// unless it has the read-only attribute, which folders' is not), through
// the wide API so long paths work. mode is F_OK, R_OK or W_OK.
#ifdef _WIN32
static bool win_access(const char *filename, int mode) {
    wchar_t *wpath = widen_path(filename);
    if (!wpath)
        return false;
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    if (mode == W_OK)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) || !(attrs & FILE_ATTRIBUTE_READONLY);
    return true;
}
#endif

bool file_exists(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    return win_access(filename, F_OK);
#else
    return access(filename, F_OK) == 0;
#endif
}

bool file_is_readable(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    return win_access(filename, R_OK);
#else
    return access(filename, R_OK) == 0;
#endif
}

bool file_is_writable(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    return win_access(filename, W_OK);
#else
    return access(filename, W_OK) == 0;
#endif
}

bool file_is_directory(const char *path) {
    return path_is_directory(path);
}

size_t file_get_size(const char *filename) {
    if (!is_valid_path(filename))
        return 0;

    // From the file's metadata, 64-bit: a 32-bit ftell failed past 2 GB on
    // Windows, so the large-file prompt never appeared for the files it is
    // for. A folder or device has no size (Linux's ftell said 8 EB).
    unsigned long long size = 0;
#ifdef _WIN32
    wchar_t *wpath = widen_path(filename);
    if (!wpath)
        return 0;
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL got = GetFileAttributesExW(wpath, GetFileExInfoStandard, &data);
    free(wpath);
    if (!got || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    size = ((unsigned long long) data.nFileSizeHigh << 32) | data.nFileSizeLow;
#else
    struct stat st;
    if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
        return 0;
    size = (unsigned long long) st.st_size;
#endif
    return size > (unsigned long long) SIZE_MAX ? SIZE_MAX : (size_t) size;
}

bool file_looks_binary(const char *filename) {
    if (!is_valid_path(filename))
        return false;

    FILE *file = open_file_utf8(filename, "rb");
    if (!file)
        return false;

    unsigned char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf), file);
    fclose(file);
    if (n == 0)
        return false;

    // BOM-marked files are text: UTF-16 in particular is full of NUL bytes
    if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        return false; // UTF-8 BOM
    if (n >= 2 && ((buf[0] == 0xFF && buf[1] == 0xFE) || (buf[0] == 0xFE && buf[1] == 0xFF)))
        return false; // UTF-16 LE/BE BOM
    bool big_endian;
    if (looks_like_utf16(buf, n, &big_endian))
        return false; // UTF-16 without a BOM: the same test the decoder applies

    size_t control = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (c == 0)
            return true; // A NUL byte outside UTF-16 never appears in text
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1B)
            control++;
    }
    // More than 10% unusual control bytes: treat as binary
    return control * 10 > n;
}

void file_count_text_stats(const char *utf8, size_t *words, size_t *chars, size_t *lines) {
    size_t w = 0, c = 0, l = 1;
    bool in_word = false;

    for (const char *p = utf8 ? utf8 : ""; *p; p++) {
        unsigned char b = (unsigned char) *p;
        if (b == '\n') {
            l++;
            in_word = false;
            continue;
        }
        if (b == '\r') {
            // CR or the CR of a CRLF pair: count the pair once
            if (p[1] != '\n')
                l++;
            in_word = false;
            continue;
        }
        // Count code points: every byte that is not a UTF-8 continuation
        if ((b & 0xC0) != 0x80)
            c++;
        if (b == ' ' || b == '\t' || b == '\f' || b == '\v') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            w++;
        }
    }

    if (words)
        *words = w;
    if (chars)
        *chars = c;
    if (lines)
        *lines = l;
}

bool file_delete(const char *filename) {
    if (!is_valid_path(filename)) {
        set_error("Invalid filename");
        return false;
    }

#ifdef _WIN32
    // Wide, not the CRT's remove(): that takes the ANSI code page, so a path
    // under "Café" was never deleted (session slots, failed-save temp files)
    wchar_t *wpath = widen_path(filename);
    if (!wpath) {
        set_error("Invalid filename");
        return false;
    }
    BOOL ok = DeleteFileW(wpath);
    DWORD err = ok ? 0 : GetLastError();
    free(wpath);
    if (!ok) {
        set_win32_error("Failed to delete file", filename, err);
        return false;
    }
#else
    // unlink, not remove(): a file delete must never take an empty folder
    if (unlink(filename) != 0) {
        set_errno_error("Failed to delete file", filename);
        return false;
    }
#endif

    return true;
}

bool file_copy(const char *source, const char *destination) {
    if (!source || !destination) {
        set_error("Invalid parameters");
        return false;
    }

    void *data;
    size_t size;

    if (!file_read_binary(source, &data, &size)) {
        return false;
    }

    // Atomic, like every write: the destination keeps its old content if the
    // copy cannot be completed
    bool result = file_write_binary_atomic(destination, data, size);
    free(data);

    return result;
}

char *file_get_directory(const char *filepath) {
    if (!filepath)
        return NULL;

    const char *last_slash = strrchr(filepath, '/');
    const char *last_backslash = strrchr(filepath, '\\');
    const char *separator = last_slash;
    if (!separator || (last_backslash && last_backslash > separator)) {
        separator = last_backslash;
    }

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
    const char *separator = last_slash;
    if (!separator || (last_backslash && last_backslash > separator)) {
        separator = last_backslash;
    }

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

    char *filename = file_get_filename(filepath);
    if (!filename)
        return NULL;

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        // No extension or hidden file
        free(filename);
        char *result = malloc(1);
        if (!result) {
            return NULL;
        }
        result[0] = '\0';
        return result;
    }

    char *result = malloc(strlen(dot + 1) + 1);
    if (result) {
        strcpy(result, dot + 1);
    }
    free(filename);
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

char *file_get_absolute_path(const char *path) {
    if (!is_valid_path(path))
        return NULL;

#ifdef _WIN32
    wchar_t wide[NPAD_MAX_PATH_LEN];
    wchar_t full[NPAD_MAX_PATH_LEN];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, NPAD_MAX_PATH_LEN) <= 0)
        return NULL;
    DWORD len = GetFullPathNameW(wide, NPAD_MAX_PATH_LEN, full, NULL);
    if (len == 0 || len >= NPAD_MAX_PATH_LEN)
        return NULL;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, full, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return NULL;
    char *result = malloc((size_t) bytes);
    if (result)
        WideCharToMultiByte(CP_UTF8, 0, full, -1, result, bytes, NULL, NULL);
    return result;
#else
    if (path[0] == '/') {
        char *copy = malloc(strlen(path) + 1);
        if (copy)
            strcpy(copy, path);
        return copy;
    }
    // Joined to the working directory rather than realpath()'d: the file need
    // not exist yet, and a symlinked path should stay the path the user gave
    char cwd[NPAD_MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd)))
        return NULL;
    const char *rest = path;
    while (rest[0] == '.' && rest[1] == '/') // "./notes.txt" -> "notes.txt"
        rest += 2;
    return file_join_paths(cwd, rest);
#endif
}

const char *file_get_last_error(void) {
    return g_last_error;
}

// ---------------------------------------------------------------------------
// Atomic writes and validation
// ---------------------------------------------------------------------------
//
// How a save replaces a file (file_write_binary_atomic):
//
//  1. Resolve the real file. A symlink is followed to its target, so the link
//     stays a link (dotfile managers) and the target gets the edit.
//  2. Write the new content to a temporary file beside the target. The name
//     is short and fixed-length (".npad-<8 hex>.tmp") so a long file name
//     still fits, random, and created exclusively (O_EXCL + O_NOFOLLOW /
//     CREATE_NEW): a planted symlink or another instance's temp file is never
//     written through or truncated.
//  3. Give it the original's identity (POSIX: owner, mode including set-id
//     bits, ACLs and other extended attributes - after the data, since a
//     write clears set-id bits and file capabilities; Windows: ReplaceFileW
//     carries over ACLs, attributes, streams such as Mark-of-the-Web, and
//     the creation time).
//  4. fsync / FlushFileBuffers so the data (and on POSIX the identity) is on
//     disk before the name points at it, then read it back through the same
//     handle and compare.
//  5. Rename it over the target (ReplaceFileW on Windows), then fsync the
//     folder on POSIX so the rename itself survives a power cut.
//
// When the file cannot be replaced - it has other hard links, its folder is
// not writable, it is a bind mount or held open without delete sharing, or
// its owner or ACL cannot be reproduced on a new file - it is overwritten in
// place instead (write_in_place), guarded so that a failure still leaves the
// original content. Either way, a failed save never destroys or truncates the
// existing file.

#if defined(FILE_OPS_FAULT_INJECTION) && !defined(_WIN32)
static unsigned g_faults = 0; // One bit per FileOpsFault stage

void file_ops_inject_fault(FileOpsFault fault) {
    if (fault == FILE_OPS_FAULT_NONE)
        g_faults = 0;
    else
        g_faults |= 1u << fault;
}

// True once, at the stage a test asked to fail, with the errno a real
// failure there would leave
static bool fault_hit(FileOpsFault stage) {
    if (!(g_faults & (1u << stage)))
        return false;
    g_faults &= ~(1u << stage);
    switch (stage) {
        case FILE_OPS_FAULT_TEMP_CREATE:
            errno = EACCES;
            break;
        case FILE_OPS_FAULT_WRITE:
        case FILE_OPS_FAULT_INPLACE_WRITE:
            errno = ENOSPC;
            break;
        case FILE_OPS_FAULT_RENAME_BUSY:
            errno = EBUSY;
            break;
        default:
            errno = EIO;
            break;
    }
    return true;
}
#define FAULT(stage) fault_hit(stage)
#else
#define FAULT(stage) false
#endif

static uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// A random value for temp names. The exclusive create is what makes a name
// safe; the randomness keeps other accounts from claiming the names in
// advance and keeps instances that save in the same second apart.
static uint32_t temp_name_random(void) {
    static uint64_t counter = 0;
    counter++;
    uint64_t seed = (uint64_t) time(NULL) ^ ((uint64_t) (uintptr_t) &counter << 16);
#ifdef _WIN32
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    seed ^= (uint64_t) qpc.QuadPart ^ ((uint64_t) GetCurrentProcessId() << 32);
#else
    seed ^= (uint64_t) getpid() << 32;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        uint64_t r = 0;
        if (read(fd, &r, sizeof(r)) == (ssize_t) sizeof(r))
            seed ^= r;
        close(fd);
    }
#endif
    return (uint32_t) mix64(seed + counter * 0x9E3779B97F4A7C15ULL);
}

#define IO_CHUNK ((size_t) 1 << 20)

// Where write_in_place keeps its safety copy of the original. It has to
// outlive a power cut, so npad points this at its (persistent, per-user)
// recovery folder; /tmp is tmpfs or cleaned at boot on many systems. Empty:
// the temp folder.
static char g_backup_dir[NPAD_MAX_PATH_LEN + 1] = { 0 };

void file_ops_set_backup_dir(const char *dir) {
    if (dir && strlen(dir) < sizeof(g_backup_dir))
        memcpy(g_backup_dir, dir, strlen(dir) + 1);
    else
        g_backup_dir[0] = '\0';
}

// Check available disk space before writing. Advisory: if the space query
// itself fails (network shares, unusual filesystems), the write proceeds.
bool file_check_disk_space(const char *path, size_t required_bytes) {
    if (!path) {
        set_error("Invalid path for disk space check");
        return false;
    }

    char *dir_path = file_get_directory(path);
    if (!dir_path) {
        return true; // Cannot determine directory - do not block the save
    }

    bool result = true;

#ifdef _WIN32
    ULARGE_INTEGER free_bytes_available;
    wchar_t *wdir = widen_path(dir_path);
    if (wdir && GetDiskFreeSpaceExW(wdir, &free_bytes_available, NULL, NULL)) {
        result = (free_bytes_available.QuadPart >= required_bytes);
        if (!result) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Insufficient disk space: %llu bytes available, %zu bytes required",
                     (unsigned long long) free_bytes_available.QuadPart, required_bytes);
        }
    }
    free(wdir);
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
    }
#endif

    free(dir_path);
    return result;
}

bool file_validate_permissions(const char *filename, bool for_writing) {
    if (!is_valid_path(filename)) {
        set_error("Invalid filename for permission validation");
        return false;
    }

    if (!file_exists(filename)) {
        if (!for_writing)
            return true; // Nothing to validate for reading a new file

        // File doesn't exist - check if we can create it in the directory
        char *dir_path = file_get_directory(filename);
        if (!dir_path) {
            set_error("Failed to get directory for permission check");
            return false;
        }

        if (!file_exists(dir_path)) {
            snprintf(g_last_error, sizeof(g_last_error), "The folder '%.300s' does not exist",
                     dir_path);
            free(dir_path);
            return false;
        }

        bool can_create = file_is_writable(dir_path);
        if (!can_create) {
            set_error("Cannot create file in directory: permission denied");
            free(dir_path);
            return false;
        }

        free(dir_path);
        return true;
    }

    if (path_is_directory(filename)) {
        set_folder_error(filename);
        return false;
    }

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
    wchar_t *wpath = widen_path(filename);
    DWORD attributes = wpath ? GetFileAttributesW(wpath) : INVALID_FILE_ATTRIBUTES;
    free(wpath);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (for_writing && (attributes & FILE_ATTRIBUTE_READONLY)) {
            set_error("File is read-only");
            return false;
        }
    }
#else
    struct stat file_stat;
    if (stat(filename, &file_stat) == 0) {
        if (!S_ISREG(file_stat.st_mode)) {
            set_error("Path is not a regular file");
            return false;
        }
    }
#endif

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

#ifndef _WIN32
// ---- POSIX ------------------------------------------------------------------

static char *dup_string(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy)
        memcpy(copy, s, len + 1);
    return copy;
}

// The folder part of a path ("." when there is none). '/' only: a backslash
// is an ordinary character in a Linux file name.
static char *posix_dirname(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash)
        return dup_string(".");
    if (slash == path)
        return dup_string("/");
    size_t len = (size_t) (slash - path);
    char *dir = malloc(len + 1);
    if (dir) {
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    return dir;
}

static char *posix_join(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    bool slash = dlen > 0 && dir[dlen - 1] == '/';
    size_t len = dlen + (slash ? 0 : 1) + strlen(name) + 1;
    char *out = malloc(len);
    if (out)
        snprintf(out, len, "%s%s%s", dir, slash ? "" : "/", name);
    return out;
}

// Follow a chain of symlinks to the file they name (which need not exist yet:
// saving through a dangling link creates its target). Only the final
// component is followed; links in the folders above resolve by themselves.
static char *resolve_symlinks(const char *path) {
    char *cur = dup_string(path);
    for (int depth = 0; cur && depth < 40; depth++) {
        struct stat st;
        if (lstat(cur, &st) != 0 || !S_ISLNK(st.st_mode))
            return cur;
        char link[NPAD_MAX_PATH_LEN];
        ssize_t n = readlink(cur, link, sizeof(link) - 1);
        if (n < 0)
            return cur; // Unreadable link: let the save report what it finds
        link[n] = '\0';
        char *next;
        if (link[0] == '/') {
            next = dup_string(link);
        } else {
            char *dir = posix_dirname(cur);
            next = dir ? posix_join(dir, link) : NULL;
            free(dir);
        }
        free(cur);
        cur = next;
    }
    if (cur) {
        free(cur);
        errno = ELOOP;
    } else {
        errno = ENOMEM;
    }
    return NULL;
}

// Create "<dir>/<prefix><8 hex><suffix>" exclusively, never through a
// symlink, retrying on a name that is taken. Returns the descriptor (read and
// write) and the malloc'd path, or -1 with errno set.
static int create_temp_posix(const char *dir, const char *prefix, const char *suffix, mode_t mode,
                             char **out_path) {
    for (int attempt = 0; attempt < 100; attempt++) {
        char name[64];
        snprintf(name, sizeof(name), "%s%08x%s", prefix, (unsigned) temp_name_random(), suffix);
        char *path = posix_join(dir, name);
        if (!path) {
            errno = ENOMEM;
            return -1;
        }
        int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
        if (fd >= 0) {
            *out_path = path;
            return fd;
        }
        int err = errno;
        free(path);
        if (err != EEXIST) {
            errno = err;
            return -1;
        }
    }
    errno = EEXIST;
    return -1;
}

static bool pwrite_all(int fd, const uint8_t *data, size_t size) {
    size_t done = 0;
    while (done < size) {
        size_t n = size - done < IO_CHUNK ? size - done : IO_CHUNK;
        ssize_t w = pwrite(fd, data + done, n, (off_t) done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (w == 0) {
            errno = EIO;
            return false;
        }
        done += (size_t) w;
    }
    return true;
}

static bool pread_all(int fd, uint8_t *out, size_t size) {
    size_t done = 0;
    while (done < size) {
        size_t n = size - done < IO_CHUNK ? size - done : IO_CHUNK;
        ssize_t r = pread(fd, out + done, n, (off_t) done);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (r == 0) {
            errno = EIO; // Shorter than it said
            return false;
        }
        done += (size_t) r;
    }
    return true;
}

// Read the file back through its descriptor, in chunks, and compare it with
// what was meant to be written (no second whole-file buffer)
static bool verify_fd(int fd, const uint8_t *data, size_t size) {
    struct stat st;
    if (fstat(fd, &st) != 0)
        return false;
    if ((unsigned long long) st.st_size != (unsigned long long) size) {
        errno = EIO;
        return false;
    }
    uint8_t *buf = malloc(size < IO_CHUNK ? (size ? size : 1) : IO_CHUNK);
    if (!buf) {
        errno = ENOMEM;
        return false;
    }
    bool ok = true;
    size_t done = 0;
    while (done < size) {
        size_t n = size - done < IO_CHUNK ? size - done : IO_CHUNK;
        ssize_t r = pread(fd, buf, n, (off_t) done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0 || memcmp(buf, data + done, (size_t) r) != 0) {
            if (r >= 0)
                errno = EIO;
            ok = false;
            break;
        }
        done += (size_t) r;
    }
    free(buf);
    return ok;
}

// Make the rename durable: the new name lives in the folder's own data
static void fsync_dir(const char *dir) {
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        if (fsync(fd) != 0) {
            // Some filesystems cannot sync a folder (EINVAL); the data is
            // already on disk, so this is not a failed save
        }
        close(fd);
    }
}

#ifdef __linux__
static bool is_acl_xattr(const char *name) {
    return strncmp(name, "system.posix_acl_", 17) == 0 || strcmp(name, "system.nfs4_acl") == 0;
}

// Copy the original's extended attributes - POSIX ACLs live here - onto the
// temp file. An ACL that cannot be reproduced would change who can read the
// file, so that is a failure (the caller then writes in place); other
// attributes (SELinux labels, user.*) are best effort. An ACL the new file
// inherited from the folder but the original lacks is removed.
static bool copy_xattrs(const char *src, int dst_fd) {
    bool ok = true;
    bool src_has_acl = false;
    ssize_t len = listxattr(src, NULL, 0);
    if (len > 0) {
        char *names = malloc((size_t) len);
        if (!names)
            return false;
        len = listxattr(src, names, (size_t) len);
        if (len < 0)
            ok = false;
        for (ssize_t i = 0; ok && i < len; i += (ssize_t) strlen(names + i) + 1) {
            const char *name = names + i;
            bool acl = is_acl_xattr(name);
            if (strcmp(name, "system.posix_acl_access") == 0)
                src_has_acl = true;
            ssize_t vlen = getxattr(src, name, NULL, 0);
            char *value = vlen >= 0 ? malloc(vlen > 0 ? (size_t) vlen : 1) : NULL;
            if (value)
                vlen = getxattr(src, name, value, (size_t) vlen);
            if (!value || vlen < 0 || fsetxattr(dst_fd, name, value, (size_t) vlen, 0) != 0) {
                if (acl)
                    ok = false;
            }
            free(value);
        }
        free(names);
    }
    if (ok && !src_has_acl && fremovexattr(dst_fd, "system.posix_acl_access") != 0) {
        // ENODATA (none inherited) or ENOTSUP: nothing to remove
    }
    return ok;
}
#endif

// Give the temp file the original's owner, group, mode and ACLs. Called once
// the data is written: a write clears set-id bits, and nothing may be written
// after this. Returns whether the result matches - it cannot when, say, the
// original belongs to another account (only root may give a file away) or
// has set-id bits this user may not set; the caller then keeps the original
// inode instead.
static bool copy_identity(int fd, const char *target, const struct stat *orig) {
    // Owner first: a chown clears set-id bits, which the chmod then restores
    if (fchown(fd, orig->st_uid, orig->st_gid) != 0 && fchown(fd, (uid_t) -1, orig->st_gid) != 0) {
        // Checked below, by comparing what the file ended up with
    }
    bool acl_ok = true;
#ifdef __linux__
    acl_ok = copy_xattrs(target, fd);
#else
    (void) target;
#endif
    if (fchmod(fd, orig->st_mode & 07777) != 0) {
        // Checked below
    }
    struct stat now;
    if (fstat(fd, &now) != 0)
        return false;
    return acl_ok && now.st_uid == orig->st_uid && now.st_gid == orig->st_gid &&
           (now.st_mode & 07777) == (orig->st_mode & 07777);
}

// Copy the original to a private backup file before it is overwritten in
// place. It is what remains if the machine dies mid-write, so it goes to the
// backup folder (file_ops_set_backup_dir) when there is one, and to the temp
// folder only when that is unset or unusable.
static bool write_backup(const uint8_t *data, size_t size, char **out_path) {
    int fd = -1;
    if (g_backup_dir[0]) {
        if (mkdir(g_backup_dir, 0700) != 0) {
            // Usually EEXIST; any other problem shows in the create below
        }
        fd = create_temp_posix(g_backup_dir, "npad-backup-", ".bak", 0600, out_path);
    }
    if (fd < 0) {
        const char *tmpdir = getenv("TMPDIR");
        if (!tmpdir || tmpdir[0] != '/')
            tmpdir = "/tmp";
        fd = create_temp_posix(tmpdir, "npad-backup-", ".bak", 0600, out_path);
    }
    if (fd < 0)
        return false;
    bool ok = pwrite_all(fd, data, size) && fsync(fd) == 0;
    int err = errno;
    if (close(fd) != 0 && ok) {
        ok = false;
        err = errno;
    }
    if (!ok) {
        unlink(*out_path);
        free(*out_path);
        *out_path = NULL;
        errno = err;
    }
    return ok;
}

// Allocate the bytes a file is about to grow by, so a full disk is found
// before anything is overwritten. Returns 0 when reserved, or when this
// filesystem or platform cannot reserve (the restore still guards that case);
// otherwise the error (ENOSPC, EFBIG, ...).
static int reserve_space(int fd, size_t from, size_t len) {
#ifdef __APPLE__
    (void) fd;
    (void) from;
    (void) len;
    return 0; // No posix_fallocate
#else
    int r = posix_fallocate(fd, (off_t) from, (off_t) len);
    return (r == EOPNOTSUPP || r == ENOSYS || r == EINVAL) ? 0 : r;
#endif
}

// Writing to or truncating a file clears its set-id bits unless the writer
// has CAP_FSETID; put back the ones it had. Best effort (only the owner may):
// the content is what the save is for.
static void restore_setid_bits(int fd, const struct stat *orig) {
    mode_t want = orig->st_mode & 07777;
    struct stat now;
    if (!(want & 06000) || fstat(fd, &now) != 0 || (now.st_mode & 07777) == want)
        return;
    if (fchmod(fd, want) != 0) {
        // Not this file's owner: it keeps the mode the write left
    }
}

// Overwrite the file where it is, for when it cannot be replaced (see the
// overview above). Not atomic, so guarded: the original is read into memory
// and copied to a backup file first; growth is reserved before anything is
// overwritten, so a full disk fails with the file untouched; and any later
// failure writes the original back. Only a crash mid-write can leave the
// file mixed, and the backup then holds the original.
static bool write_in_place(const char *display, const char *target, const uint8_t *data,
                           size_t size) {
    int fd = open(target, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        set_errno_error("Failed to open file for writing", display);
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (unsigned long long) st.st_size >= (unsigned long long) SIZE_MAX) {
        set_error("Path is not a regular file");
        close(fd);
        return false;
    }
    size_t orig_size = (size_t) st.st_size;
    uint8_t *orig = malloc(orig_size ? orig_size : 1);
    if (!orig || !pread_all(fd, orig, orig_size)) {
        set_errno_error_code("Failed to read the original before overwriting", display,
                             orig ? errno : ENOMEM);
        free(orig);
        close(fd);
        return false;
    }
    char *backup = NULL;
    if (!write_backup(orig, orig_size, &backup)) {
        set_errno_error_code("Failed to make a safety copy before overwriting", display, errno);
        free(orig);
        close(fd);
        return false;
    }

    const char *what = NULL;
    int err = 0;
    int reserve = size > orig_size ? reserve_space(fd, orig_size, size - orig_size) : 0;
    if (reserve != 0) {
        // Not even reserved (a full disk): the original is intact, once any
        // partial growth is dropped
        if (ftruncate(fd, (off_t) orig_size) == 0) {
            set_errno_error_code("Failed to save", display, reserve);
            close(fd);
            unlink(backup);
            free(backup);
            free(orig);
            return false;
        }
        what = "Failed to save";
        errno = reserve;
    } else if (!pwrite_all(fd, data, size) || FAULT(FILE_OPS_FAULT_INPLACE_WRITE)) {
        what = "Failed to write";
    } else if (ftruncate(fd, (off_t) size) != 0) {
        what = "Failed to set the length of";
    } else {
        restore_setid_bits(fd, &st);
        if (fsync(fd) != 0)
            what = "Failed to flush to disk";
        else if (!verify_fd(fd, data, size))
            what = "Failed to verify the written data for";
    }
    if (!what) {
        bool closed = close(fd) == 0;
        if (closed) {
            unlink(backup);
            free(backup);
            free(orig);
            return true;
        }
        // A close error can mean deferred write-back failed (NFS); the data
        // was verified and synced, but re-open and put the original back
        what = "Failed to close";
        err = errno;
        fd = open(target, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    } else {
        err = errno;
    }

    // Put the original back
    bool restored = fd >= 0 && !FAULT(FILE_OPS_FAULT_INPLACE_RESTORE) &&
                    pwrite_all(fd, orig, orig_size) && ftruncate(fd, (off_t) orig_size) == 0;
    if (restored) {
        restore_setid_bits(fd, &st);
        restored = fsync(fd) == 0 && verify_fd(fd, orig, orig_size);
    }
    if (fd >= 0)
        close(fd);
    if (restored) {
        unlink(backup);
        set_errno_error_code(what, display, err);
    } else {
        snprintf(g_last_error, sizeof(g_last_error),
                 "Saving failed (%.80s) and the original could not be put back; a copy of it is "
                 "at '%.200s'",
                 strerror(err), backup);
    }
    free(backup);
    free(orig);
    return false;
}

static bool atomic_write_impl(const char *filename, const uint8_t *data, size_t size) {
    char *target = resolve_symlinks(filename);
    if (!target) {
        set_errno_error("Failed to follow the link", filename);
        return false;
    }

    bool ok = false;
    char *dir = NULL;
    char *temp = NULL;
    int fd = -1;

    struct stat orig;
    bool exists = stat(target, &orig) == 0;
    if (!exists && errno != ENOENT) {
        set_errno_error("Cannot save", filename);
        goto out;
    }
    if (!file_check_disk_space(target, size + 1024) || !file_validate_permissions(target, true))
        goto out; // Message already set

    if (exists && orig.st_nlink > 1) {
        // Hard links: a rename would split them, leaving the other names with
        // the old content. Write in place so every name sees the edit.
        ok = write_in_place(filename, target, data, size);
        goto out;
    }

    dir = posix_dirname(target);
    if (!dir) {
        set_error("Out of memory");
        goto out;
    }
    // A new file gets the usual 0666 & ~umask. Replacing one starts private
    // (0600) so nobody can open it before it has the original's permissions;
    // copy_identity then gives it those.
    if (FAULT(FILE_OPS_FAULT_TEMP_CREATE)) {
        fd = -1;
    } else {
        fd = create_temp_posix(dir, ".npad-", ".tmp", exists ? 0600 : 0666, &temp);
    }
    if (fd < 0) {
        int err = errno;
        if (exists && (err == EACCES || err == EPERM)) {
            // A writable file in a folder this user may not create files in
            ok = write_in_place(filename, target, data, size);
        } else {
            set_errno_error_code("Failed to create a temporary file in", dir, err);
        }
        goto out;
    }

    // The data goes in first, while the file is still private: the first
    // write by a process without CAP_FSETID clears set-id bits and file
    // capabilities, so identity copied before it would not survive it
    if (!pwrite_all(fd, data, size) || FAULT(FILE_OPS_FAULT_WRITE)) {
        set_errno_error("Failed to write", filename);
        goto out;
    }

    if (exists && !copy_identity(fd, target, &orig)) {
        // Owner, mode or ACL cannot be reproduced on a new file: keep the
        // original inode instead of handing back a file with other rights
        close(fd);
        fd = -1;
        unlink(temp);
        free(temp);
        temp = NULL;
        ok = write_in_place(filename, target, data, size);
        goto out;
    }

    // The sync now covers the metadata just set as well as the data
    const char *what = NULL;
    if (fsync(fd) != 0 || FAULT(FILE_OPS_FAULT_SYNC)) {
        what = "Failed to flush to disk";
    } else if (!verify_fd(fd, data, size) || FAULT(FILE_OPS_FAULT_VERIFY)) {
        what = "Failed to verify the written data for";
    }
    if (what) {
        set_errno_error(what, filename);
        goto out;
    }
    int close_rc = close(fd);
    fd = -1;
    if (close_rc != 0) {
        set_errno_error("Failed to close", filename);
        goto out;
    }

    if (FAULT(FILE_OPS_FAULT_RENAME) || FAULT(FILE_OPS_FAULT_RENAME_BUSY) ||
        rename(temp, target) != 0) {
        int err = errno;
        unlink(temp);
        free(temp);
        temp = NULL;
        if (exists &&
            (err == EBUSY || err == EXDEV || err == EACCES || err == EPERM || err == ETXTBSY)) {
            // A bind-mounted single file (EBUSY), a sticky folder, and the like
            ok = write_in_place(filename, target, data, size);
        } else {
            set_errno_error_code("Failed to replace", filename, err);
        }
        goto out;
    }
    free(temp);
    temp = NULL;
    fsync_dir(dir);
    ok = true;

out:
    if (fd >= 0)
        close(fd);
    if (temp) {
        unlink(temp); // Never leave a stray temp file beside the user's file
        free(temp);
    }
    free(dir);
    free(target);
    return ok;
}

#else
// ---- Windows ------------------------------------------------------------------

static char *narrow_path(const wchar_t *wide) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (n <= 0)
        return NULL;
    char *out = malloc((size_t) n);
    if (out && WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, n, NULL, NULL) <= 0) {
        free(out);
        out = NULL;
    }
    return out;
}

// "<folder of beside><prefix><8 hex><suffix>": a name in the same folder as
// beside (or inside it, when it ends with a separator), long-path prefixed
// where needed. (Not "near": windows.h defines that as a macro.)
static wchar_t *sibling_name(const wchar_t *beside, const wchar_t *prefix, const wchar_t *suffix) {
    const wchar_t *sep = NULL;
    for (const wchar_t *p = beside; *p; p++) {
        if (*p == L'\\' || *p == L'/')
            sep = p;
    }
    size_t dir_len = sep ? (size_t) (sep - beside) + 1 : 0;
    size_t len = dir_len + wcslen(prefix) + 8 + wcslen(suffix) + 1;
    wchar_t *out = malloc(len * sizeof(wchar_t));
    if (!out)
        return NULL;
    memcpy(out, beside, dir_len * sizeof(wchar_t));
    wcscpy(out + dir_len, prefix);
    wchar_t *hex = out + dir_len + wcslen(prefix);
    uint32_t v = temp_name_random();
    for (int i = 7; i >= 0; i--) {
        hex[i] = L"0123456789abcdef"[v & 0xF];
        v >>= 4;
    }
    wcscpy(hex + 8, suffix);
    return long_path_w(out);
}

// Create a new file exclusively (CREATE_NEW never opens an existing name, a
// symlink included), retrying on a name that is taken
static HANDLE win_create_temp(const wchar_t *beside, const wchar_t *prefix, const wchar_t *suffix,
                              wchar_t **out_path) {
    for (int attempt = 0; attempt < 100; attempt++) {
        wchar_t *path = sibling_name(beside, prefix, suffix);
        if (!path) {
            SetLastError(ERROR_OUTOFMEMORY);
            return INVALID_HANDLE_VALUE;
        }
        HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            *out_path = path;
            return h;
        }
        DWORD err = GetLastError();
        free(path);
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            SetLastError(err);
            return INVALID_HANDLE_VALUE;
        }
    }
    SetLastError(ERROR_FILE_EXISTS);
    return INVALID_HANDLE_VALUE;
}

static bool win_seek(HANDLE h, unsigned long long pos) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG) pos;
    return SetFilePointerEx(h, li, NULL, FILE_BEGIN) != 0;
}

static bool win_set_size(HANDLE h, unsigned long long size) {
    return win_seek(h, size) && SetEndOfFile(h) != 0;
}

// Write from the current position, in chunks (WriteFile takes a DWORD)
static bool win_write_all(HANDLE h, const uint8_t *data, size_t size) {
    size_t done = 0;
    while (done < size) {
        DWORD n = (DWORD) (size - done < IO_CHUNK ? size - done : IO_CHUNK);
        DWORD written = 0;
        if (!WriteFile(h, data + done, n, &written, NULL))
            return false;
        if (written == 0) {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        done += written;
    }
    return true;
}

static bool win_read_all(HANDLE h, uint8_t *out, size_t size) {
    if (!win_seek(h, 0))
        return false;
    size_t done = 0;
    while (done < size) {
        DWORD n = (DWORD) (size - done < IO_CHUNK ? size - done : IO_CHUNK);
        DWORD got = 0;
        if (!ReadFile(h, out + done, n, &got, NULL))
            return false;
        if (got == 0) {
            SetLastError(ERROR_HANDLE_EOF);
            return false;
        }
        done += got;
    }
    return true;
}

// Read the file back through the same handle, in chunks, and compare: works
// past 2 GB, where re-reading the whole file through a 32-bit ftell failed
static bool win_verify(HANDLE h, const uint8_t *data, size_t size) {
    LARGE_INTEGER cur;
    if (!GetFileSizeEx(h, &cur))
        return false;
    if ((unsigned long long) cur.QuadPart != (unsigned long long) size) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    if (!win_seek(h, 0))
        return false;
    uint8_t *buf = malloc(size < IO_CHUNK ? (size ? size : 1) : IO_CHUNK);
    if (!buf) {
        SetLastError(ERROR_OUTOFMEMORY);
        return false;
    }
    bool ok = true;
    size_t done = 0;
    while (done < size) {
        DWORD n = (DWORD) (size - done < IO_CHUNK ? size - done : IO_CHUNK);
        DWORD got = 0;
        if (!ReadFile(h, buf, n, &got, NULL)) {
            ok = false;
            break;
        }
        if (got == 0 || memcmp(buf, data + done, got) != 0) {
            SetLastError(ERROR_INVALID_DATA);
            ok = false;
            break;
        }
        done += got;
    }
    free(buf);
    return ok;
}

// The path a handle really refers to (following symlinks), \\?\-prefixed
static wchar_t *final_path_of(HANDLE h) {
    DWORD need = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (need == 0)
        return NULL;
    wchar_t *buf = malloc(((size_t) need + 1) * sizeof(wchar_t));
    if (!buf)
        return NULL;
    DWORD got = GetFinalPathNameByHandleW(h, buf, need + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (got == 0 || got > need) {
        free(buf);
        return NULL;
    }
    return buf;
}

// The original, copied to a private file before it is overwritten in place:
// in the backup folder (file_ops_set_backup_dir) when there is one, so it
// survives a reboot, else in the temp folder
static bool win_write_backup(const uint8_t *data, size_t size, wchar_t **out_path) {
    HANDLE h = INVALID_HANDLE_VALUE;
    if (g_backup_dir[0]) {
        size_t len = strlen(g_backup_dir);
        char *dir = malloc(len + 2);
        if (dir) {
            memcpy(dir, g_backup_dir, len + 1);
            if (len > 0 && dir[len - 1] != '\\' && dir[len - 1] != '/')
                memcpy(dir + len, "\\", 2); // So the name goes inside it
            wchar_t *wdir = widen_path(dir);
            if (wdir) {
                CreateDirectoryW(wdir, NULL); // Usually there already
                h = win_create_temp(wdir, L"npad-backup-", L".bak", out_path);
                free(wdir);
            }
            free(dir);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        wchar_t tmpdir[MAX_PATH + 2];
        DWORD n = GetTempPathW(MAX_PATH + 1, tmpdir);
        if (n == 0 || n > MAX_PATH)
            return false;
        h = win_create_temp(tmpdir, L"npad-backup-", L".bak", out_path);
    }
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool ok = win_write_all(h, data, size) && FlushFileBuffers(h);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(*out_path);
        free(*out_path);
        *out_path = NULL;
        SetLastError(err);
    }
    return ok;
}

// Overwrite the file where it is, for when it cannot be replaced; guarded as
// the POSIX write_in_place is (backup, growth reserved first, original
// written back on any failure)
static bool win_write_in_place(const char *display, const wchar_t *target, const uint8_t *data,
                               size_t size) {
    HANDLE h = CreateFileW(target, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        set_win32_error("Failed to open file for writing", display, GetLastError());
        return false;
    }
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 ||
        (unsigned long long) li.QuadPart >= (unsigned long long) SIZE_MAX) {
        set_win32_error("Failed to get the size of", display, GetLastError());
        CloseHandle(h);
        return false;
    }
    size_t orig_size = (size_t) li.QuadPart;
    uint8_t *orig = malloc(orig_size ? orig_size : 1);
    if (!orig || !win_read_all(h, orig, orig_size)) {
        set_win32_error("Failed to read the original before overwriting", display,
                        orig ? GetLastError() : ERROR_OUTOFMEMORY);
        free(orig);
        CloseHandle(h);
        return false;
    }
    wchar_t *backup = NULL;
    if (!win_write_backup(orig, orig_size, &backup)) {
        set_win32_error("Failed to make a safety copy before overwriting", display, GetLastError());
        free(orig);
        CloseHandle(h);
        return false;
    }

    const char *what = NULL;
    DWORD err = 0;
    if (size > orig_size && !win_set_size(h, size)) {
        err = GetLastError();
        if (win_set_size(h, orig_size)) {
            // Could not even reserve the space: the original is intact
            CloseHandle(h);
            DeleteFileW(backup);
            free(backup);
            free(orig);
            set_win32_error("Failed to save", display, err);
            return false;
        }
        what = "Failed to save";
    } else {
        if (!win_seek(h, 0) || !win_write_all(h, data, size)) {
            what = "Failed to write";
        } else if (!win_set_size(h, size)) {
            what = "Failed to set the length of";
        } else if (!FlushFileBuffers(h)) {
            what = "Failed to flush to disk";
        } else if (!win_verify(h, data, size)) {
            what = "Failed to verify the written data for";
        }
        if (what)
            err = GetLastError();
    }
    if (!what) {
        CloseHandle(h);
        DeleteFileW(backup);
        free(backup);
        free(orig);
        return true;
    }

    bool restored = win_seek(h, 0) && win_write_all(h, orig, orig_size) &&
                    win_set_size(h, orig_size) && FlushFileBuffers(h) &&
                    win_verify(h, orig, orig_size);
    CloseHandle(h);
    if (restored) {
        DeleteFileW(backup);
        set_win32_error(what, display, err);
    } else {
        char *where = narrow_path(backup);
        set_win32_error("Saving failed", display, err);
        size_t used = strlen(g_last_error);
        snprintf(g_last_error + used, sizeof(g_last_error) - used,
                 ", and the original could not be put back; a copy of it is at '%.200s'",
                 where ? where : "(unknown)");
        free(where);
    }
    free(backup);
    free(orig);
    return false;
}

static bool atomic_write_impl(const char *filename, const uint8_t *data, size_t size) {
    wchar_t *target = widen_path(filename);
    if (!target) {
        set_error("Invalid file path");
        return false;
    }
    if (!file_check_disk_space(filename, size + 1024) ||
        !file_validate_permissions(filename, true)) {
        free(target);
        return false; // Message already set
    }

    bool ok = false;
    wchar_t *temp = NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD attrs = GetFileAttributesW(target);
    bool exists = attrs != INVALID_FILE_ATTRIBUTES;
    bool hard_linked = false;

    if (exists) {
        HANDLE probe = CreateFileW(target, FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (probe != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(probe, &info) && info.nNumberOfLinks > 1)
                hard_linked = true;
            if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
                // A symbolic link: save to the file it points at, so the link
                // stays a link (ReplaceFileW would put a file in its place)
                wchar_t *real = final_path_of(probe);
                if (real) {
                    free(target);
                    target = real;
                }
            }
            CloseHandle(probe);
        }
    }

    if (hard_linked) {
        // Replacing would split the links; every name must see the edit
        ok = win_write_in_place(filename, target, data, size);
        goto out;
    }

    h = win_create_temp(target, L".npad-", L".tmp", &temp);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (exists && err == ERROR_ACCESS_DENIED) {
            // Modify on the file but not create in its folder
            // (C:\ProgramData\<app>): write in place, as Notepad does
            ok = win_write_in_place(filename, target, data, size);
        } else {
            set_win32_error("Failed to create a temporary file beside", filename, err);
        }
        goto out;
    }

    const char *what = NULL;
    if (!win_write_all(h, data, size)) {
        what = "Failed to write";
    } else if (!FlushFileBuffers(h)) {
        what = "Failed to flush to disk";
    } else if (!win_verify(h, data, size)) {
        what = "Failed to verify the written data for";
    }
    if (what) {
        set_win32_error(what, filename, GetLastError());
        goto out;
    }
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;

    if (!exists) {
        if (!MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            set_win32_error("Failed to save", filename, GetLastError());
            goto out;
        }
        free(temp);
        temp = NULL;
        ok = true;
        goto out;
    }

    // ReplaceFileW keeps what a rename would lose: the original's ACL, owner,
    // attributes (Hidden, System), creation time and alternate streams, such
    // as the Zone.Identifier that makes SmartScreen warn about a download.
    // It is given a backup name because without one, one of its documented
    // failures leaves the original under no name at all.
    wchar_t *backup = sibling_name(target, L".npad-", L".bak");
    if (backup && ReplaceFileW(target, temp, backup, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
        DeleteFileW(backup); // The original's old content, no longer needed
        free(backup);
        free(temp);
        temp = NULL;
        ok = true;
        goto out;
    }
    DWORD err = backup ? GetLastError() : ERROR_OUTOFMEMORY;
    if (backup && GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES) {
        // The original was moved to the backup name and the new file did not
        // take its place (ERROR_UNABLE_TO_MOVE_REPLACEMENT_2): put it back
        if (!MoveFileExW(backup, target, MOVEFILE_WRITE_THROUGH)) {
            char *where = narrow_path(backup);
            set_win32_error("Failed to replace", filename, err);
            size_t used = strlen(g_last_error);
            snprintf(g_last_error + used, sizeof(g_last_error) - used,
                     "; the original is now named '%.200s'", where ? where : "(unknown)");
            free(where);
            free(backup);
            goto out;
        }
    }
    free(backup);
    switch (err) {
        case ERROR_UNABLE_TO_MOVE_REPLACEMENT:
        case ERROR_UNABLE_TO_MOVE_REPLACEMENT_2:
        case ERROR_UNABLE_TO_REMOVE_REPLACED:
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            // Held open without delete sharing (a log a service writes), or
            // the folder refuses the rename: write in place instead
            DeleteFileW(temp);
            free(temp);
            temp = NULL;
            ok = win_write_in_place(filename, target, data, size);
            break;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_FUNCTION:
        case ERROR_NOT_SUPPORTED:
        case ERROR_CALL_NOT_IMPLEMENTED:
        case ERROR_FILE_NOT_FOUND:
            // A filesystem or redirector without ReplaceFile (or the original
            // vanished meanwhile): a plain replace, which keeps no ACL or
            // streams - as every save did before
            if (MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                free(temp);
                temp = NULL;
                ok = true;
            } else {
                set_win32_error("Failed to replace", filename, GetLastError());
            }
            break;
        default:
            set_win32_error("Failed to replace", filename, err);
            break;
    }

out:
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (temp) {
        DeleteFileW(temp); // Wide: an ANSI remove() missed non-ASCII folders
        free(temp);
    }
    free(target);
    return ok;
}
#endif

// Atomic write: see the overview at the top of this section. The original
// file is never truncated or removed before its replacement is known good.
bool file_write_binary_atomic(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters for atomic binary write");
        return false;
    }

    if (!is_valid_path(filename)) {
        set_error("Invalid file path");
        return false;
    }

    return atomic_write_impl(filename, (const uint8_t *) data, size);
}

bool file_write_text_atomic(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters for atomic write");
        return false;
    }
    return file_write_binary_atomic(filename, content, strlen(content));
}
