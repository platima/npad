/*
 * npad - File Operations Implementation
 * Cross-platform file I/O operations
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "file_ops.h"
#include "error.h"
#include <errno.h>
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
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

// Maximum path length accepted (sanity bound only - the OS enforces real limits)
#define NPAD_MAX_PATH_LEN 4096

// CRC32 lookup table for write verification in atomic operations
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// Global error message
static char g_last_error[512] = { 0 };

static void set_error(const char *message) {
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static void set_errno_error(const char *operation, const char *filename) {
    snprintf(g_last_error, sizeof(g_last_error), "%.100s '%.200s': %.150s",
             operation ? operation : "Unknown", filename ? filename : "Unknown", strerror(errno));
}

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
    int path_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, wpath, NPAD_MAX_PATH_LEN);
    int mode_len = MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
    if (path_len > 0 && mode_len > 0) {
        return _wfopen(wpath, wmode);
    }
    return fopen(filename, mode); // Fallback for unconvertible paths
#else
    return fopen(filename, mode);
#endif
}

// ---------------------------------------------------------------------------
// UTF conversion helpers (portable, no OS dependencies, unit-testable)
// ---------------------------------------------------------------------------

static bool utf8_is_valid(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t byte = data[i];
        size_t remaining = len - i;

        if (byte < 0x80) {
            i++;
        } else if ((byte & 0xE0) == 0xC0) {
            if (remaining < 2 || (data[i + 1] & 0xC0) != 0x80 || byte < 0xC2)
                return false; // Overlong or truncated
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            if (remaining < 3 || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80)
                return false;
            uint32_t cp = ((uint32_t) (byte & 0x0F) << 12) |
                          ((uint32_t) (data[i + 1] & 0x3F) << 6) | (data[i + 2] & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))
                return false; // Overlong or surrogate
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            if (remaining < 4 || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 ||
                (data[i + 3] & 0xC0) != 0x80)
                return false;
            uint32_t cp = ((uint32_t) (byte & 0x07) << 18) |
                          ((uint32_t) (data[i + 1] & 0x3F) << 12) |
                          ((uint32_t) (data[i + 2] & 0x3F) << 6) | (data[i + 3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF)
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
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

// Convert UTF-16 bytes (without BOM) to a NUL-terminated UTF-8 string
static char *utf16_to_utf8(const uint8_t *data, size_t byte_len, bool big_endian) {
    size_t unit_count = byte_len / 2;
    // Worst case: every UTF-16 unit becomes 3 UTF-8 bytes (surrogate pairs: 2 units -> 4 bytes)
    char *out = malloc(unit_count * 3 + 1);
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
                i++;
            }
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            cp = 0xFFFD; // Unpaired surrogate
            i++;
        } else {
            cp = unit;
            i++;
        }

        out_pos += utf8_encode(cp, out + out_pos);
    }

    out[out_pos] = '\0';
    return out;
}

// Convert a NUL-terminated UTF-8 string to UTF-16 bytes (BOM included).
// Returns malloc'd buffer, sets *out_len to the byte length.
static uint8_t *utf8_to_utf16(const char *utf8, bool big_endian, size_t *out_len) {
    size_t len = strlen(utf8);
    // Worst case: every UTF-8 byte becomes one UTF-16 unit, plus BOM
    uint8_t *out = malloc(len * 2 + 4);
    if (!out)
        return NULL;

    size_t out_pos = 0;
    write_u16(out, 0xFEFF, big_endian); // BOM
    out_pos = 2;

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

// Convert system code page bytes to UTF-8 (used for files that are not
// valid UTF-8). On non-Windows platforms Latin-1 is assumed.
static char *ansi_to_utf8(const uint8_t *data, size_t len) {
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_ACP, 0, (const char *) data, (int) len, NULL, 0);
    if (wide_len <= 0)
        return NULL;
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_ACP, 0, (const char *) data, (int) len, wide, wide_len);

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    char *out = malloc((size_t) utf8_len + 1);
    if (!out) {
        free(wide);
        return NULL;
    }
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, out, utf8_len, NULL, NULL);
    out[utf8_len] = '\0';
    free(wide);
    return out;
#else
    // Latin-1 fallback: each byte maps to the same code point
    char *out = malloc(len * 2 + 1);
    if (!out)
        return NULL;
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i++) {
        out_pos += utf8_encode(data[i], out + out_pos);
    }
    out[out_pos] = '\0';
    return out;
#endif
}

// Convert UTF-8 to system code page bytes for saving ANSI files.
// Unmappable characters degrade to the system default character.
static uint8_t *utf8_to_ansi(const char *utf8, size_t *out_len) {
#ifdef _WIN32
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0)
        return NULL;
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_len);

    int ansi_len = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    uint8_t *out = malloc((size_t) ansi_len);
    if (!out) {
        free(wide);
        return NULL;
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, (char *) out, ansi_len, NULL, NULL);
    free(wide);
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
        out[out_pos++] = (cp < 0x100) ? (uint8_t) cp : '?';
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
    // Round-trip through the system code page and see whether any character
    // had to fall back to the default character
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wide_len <= 0)
        return false;
    wchar_t *wide = malloc((size_t) wide_len * sizeof(wchar_t));
    if (!wide)
        return false;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_len);

    BOOL used_default = FALSE;
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, NULL, 0, NULL, &used_default);
    free(wide);
    return used_default != FALSE;
#else
    // Latin-1 model (matches utf8_to_ansi): anything above U+00FF is lost
    size_t len = strlen(utf8);
    size_t i = 0;
    while (i < len) {
        if (utf8_decode((const uint8_t *) utf8, len, &i) > 0xFF) {
            return true;
        }
    }
    return false;
#endif
}

// Heuristic for UTF-16 files without a BOM: mostly-ASCII text has NUL
// bytes in every other position
static bool looks_like_utf16(const uint8_t *data, size_t len, bool *big_endian) {
    if (len < 4 || (len % 2) != 0)
        return false;

    size_t sample = len < 512 ? len : 512;
    size_t pairs = sample / 2;
    size_t even_nuls = 0, odd_nuls = 0;
    for (size_t p = 0; p < pairs; p++) {
        if (data[p * 2] == 0)
            even_nuls++;
        if (data[p * 2 + 1] == 0)
            odd_nuls++;
    }
    if (odd_nuls > pairs * 4 / 10 && even_nuls < pairs / 10) {
        *big_endian = false; // ASCII chars in low byte -> little endian
        return true;
    }
    if (even_nuls > pairs * 4 / 10 && odd_nuls < pairs / 10) {
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

    if (lf > crlf && lf > cr)
        return NPAD_EOL_LF;
    if (cr > crlf && cr > lf)
        return NPAD_EOL_CR;
    return NPAD_EOL_CRLF; // Majority CRLF, or no line breaks at all
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
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename, "Failed to open file for reading: %s",
                         strerror(errno));
        set_errno_error("Failed to open file", filename);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        set_errno_error("Failed to seek in file", filename);
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        set_errno_error("Failed to get file size", filename);
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        set_errno_error("Failed to seek in file", filename);
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
            set_error("I/O error during file read");
            free(content);
            fclose(file);
            return NULL;
        }
        total_read += chunk_read;
    }

    fclose(file);

    if (total_read != (size_t) size) {
        NPAD_ERROR_ERROR(NPAD_ERROR_FILE_IO, errno, filename,
                         "Failed to read complete file: expected %ld bytes, got %zu bytes", size,
                         total_read);
        set_errno_error("Failed to read file completely", filename);
        free(content);
        return NULL;
    }

    content[size] = '\0';
    *out_size = (size_t) size;
    return content;
}

char *file_read_text(const char *filename) {
    size_t size;
    return (char *) read_all_bytes(filename, &size);
}

char *file_read_text_ex(const char *filename, TextFileInfo *info) {
    size_t size;
    uint8_t *raw = read_all_bytes(filename, &size);
    if (!raw)
        return NULL;

    TextEncoding encoding;
    char *utf8 = NULL;

    if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        encoding = NPAD_ENC_UTF8_BOM;
        utf8 = malloc(size - 3 + 1);
        if (utf8) {
            memcpy(utf8, raw + 3, size - 3);
            utf8[size - 3] = '\0';
        }
    } else if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        encoding = NPAD_ENC_UTF16_LE;
        utf8 = utf16_to_utf8(raw + 2, size - 2, false);
    } else if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        encoding = NPAD_ENC_UTF16_BE;
        utf8 = utf16_to_utf8(raw + 2, size - 2, true);
    } else {
        bool big_endian;
        if (looks_like_utf16(raw, size, &big_endian)) {
            encoding = big_endian ? NPAD_ENC_UTF16_BE : NPAD_ENC_UTF16_LE;
            utf8 = utf16_to_utf8(raw, size, big_endian);
        } else if (utf8_is_valid(raw, size)) {
            encoding = NPAD_ENC_UTF8;
            utf8 = (char *) raw;
            raw = NULL; // Ownership transferred
        } else {
            encoding = NPAD_ENC_ANSI;
            utf8 = ansi_to_utf8(raw, size);
        }
    }

    free(raw);

    if (!utf8) {
        set_error("Failed to convert file contents");
        return NULL;
    }

    if (info) {
        info->encoding = encoding;
        info->line_ending = file_detect_line_ending(utf8);
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

// Plain write. Never deletes the destination on failure - a failed
// overwrite must not destroy the previous file contents. Callers that need
// stronger guarantees use the atomic variants.
static bool write_all_bytes(const char *filename, const void *data, size_t size) {
    if (!is_valid_path(filename)) {
        set_error("Invalid file path");
        return false;
    }

    FILE *file = open_file_utf8(filename, "wb");
    if (!file) {
        set_errno_error("Failed to create file", filename);
        return false;
    }

    size_t written = fwrite(data, 1, size, file);
    if (written != size) {
        set_errno_error("Failed to write complete file", filename);
        fclose(file);
        return false;
    }

    if (fflush(file) != 0) {
        set_errno_error("Failed to flush data to disk", filename);
        fclose(file);
        return false;
    }

    if (fclose(file) != 0) {
        set_errno_error("Failed to close file after writing", filename);
        return false;
    }

    return true;
}

bool file_write_text(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters");
        return false;
    }
    return write_all_bytes(filename, content, strlen(content));
}

bool file_write_binary(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters");
        return false;
    }
    return write_all_bytes(filename, data, size);
}

// Convert UTF-8 content to the target encoding/line endings as raw bytes.
// Returns malloc'd buffer, sets *out_len.
static uint8_t *encode_text(const char *utf8_content, const TextFileInfo *info, size_t *out_len) {
    TextFileInfo defaults = { NPAD_ENC_UTF8, NPAD_EOL_CRLF };
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
            bytes = utf8_to_utf16(converted, false, &len);
            break;

        case NPAD_ENC_UTF16_BE:
            bytes = utf8_to_utf16(converted, true, &len);
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

bool file_exists(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    wchar_t wpath[NPAD_MAX_PATH_LEN];
    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, wpath, NPAD_MAX_PATH_LEN) > 0) {
        return _waccess(wpath, F_OK) == 0;
    }
#endif
    return access(filename, F_OK) == 0;
}

bool file_is_readable(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    wchar_t wpath[NPAD_MAX_PATH_LEN];
    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, wpath, NPAD_MAX_PATH_LEN) > 0) {
        return _waccess(wpath, R_OK) == 0;
    }
#endif
    return access(filename, R_OK) == 0;
}

bool file_is_writable(const char *filename) {
    if (!is_valid_path(filename))
        return false;
#ifdef _WIN32
    wchar_t wpath[NPAD_MAX_PATH_LEN];
    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, wpath, NPAD_MAX_PATH_LEN) > 0) {
        return _waccess(wpath, W_OK) == 0;
    }
#endif
    return access(filename, W_OK) == 0;
}

size_t file_get_size(const char *filename) {
    if (!is_valid_path(filename))
        return 0;

    FILE *file = open_file_utf8(filename, "rb");
    if (!file)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long size = ftell(file);
    fclose(file);

    return (size >= 0) ? (size_t) size : 0;
}

bool file_delete(const char *filename) {
    if (!is_valid_path(filename)) {
        set_error("Invalid filename");
        return false;
    }

    if (remove(filename) != 0) {
        set_errno_error("Failed to delete file", filename);
        return false;
    }

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

    bool result = file_write_binary(destination, data, size);
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

const char *file_get_last_error(void) {
    return g_last_error;
}

// ---------------------------------------------------------------------------
// Atomic writes and validation
// ---------------------------------------------------------------------------

// Calculate CRC32 checksum for write verification
static uint32_t calculate_crc32(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *) data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// Generate a temporary filename for atomic operations
static char *generate_temp_filename(const char *original_filename) {
    if (!original_filename) {
        return NULL;
    }

    size_t len = strlen(original_filename);
    char *temp_filename = malloc(len + 16); // Extra space for suffix
    if (!temp_filename) {
        return NULL;
    }

    snprintf(temp_filename, len + 16, "%s.tmp.%d", original_filename, (int) time(NULL));
    return temp_filename;
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
    if (GetDiskFreeSpaceEx(dir_path, &free_bytes_available, NULL, NULL)) {
        result = (free_bytes_available.QuadPart >= required_bytes);
        if (!result) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Insufficient disk space: %llu bytes available, %zu bytes required",
                     (unsigned long long) free_bytes_available.QuadPart, required_bytes);
        }
    }
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

        bool can_create = file_is_writable(dir_path);
        if (!can_create) {
            set_error("Cannot create file in directory: permission denied");
            free(dir_path);
            return false;
        }

        free(dir_path);
        return true;
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
    DWORD attributes = GetFileAttributes(filename);
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

// Atomic write: write to a temp file, verify it, then rename over the
// destination. The original file is never touched until the replacement
// is known good.
bool file_write_binary_atomic(const char *filename, const void *data, size_t size) {
    if (!filename || !data) {
        set_error("Invalid parameters for atomic binary write");
        return false;
    }

    if (!is_valid_path(filename)) {
        set_error("Invalid file path");
        return false;
    }

    if (!file_check_disk_space(filename, size + 1024)) {
        return false;
    }

    if (!file_validate_permissions(filename, true)) {
        return false;
    }

    char *temp_filename = generate_temp_filename(filename);
    if (!temp_filename) {
        set_error("Failed to generate temporary filename");
        return false;
    }

    if (!file_write_binary(temp_filename, data, size)) {
        // Error already set by file_write_binary
        remove(temp_filename);
        free(temp_filename);
        return false;
    }

    // Verify the written data by reading it back and comparing checksums
    void *read_data;
    size_t read_size;
    if (!file_read_binary(temp_filename, &read_data, &read_size)) {
        set_error("Failed to verify written data");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }

    bool data_matches =
        (read_size == size && calculate_crc32(data, size) == calculate_crc32(read_data, read_size));
    free(read_data);

    if (!data_matches) {
        set_error("Data integrity verification failed after write");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }

    // Atomically replace the original file
#ifdef _WIN32
    wchar_t wtemp[NPAD_MAX_PATH_LEN], wdest[NPAD_MAX_PATH_LEN];
    bool moved = false;
    if (MultiByteToWideChar(CP_UTF8, 0, temp_filename, -1, wtemp, NPAD_MAX_PATH_LEN) > 0 &&
        MultiByteToWideChar(CP_UTF8, 0, filename, -1, wdest, NPAD_MAX_PATH_LEN) > 0) {
        moved = MoveFileExW(wtemp, wdest, MOVEFILE_REPLACE_EXISTING) != 0;
    } else {
        moved = MoveFileEx(temp_filename, filename, MOVEFILE_REPLACE_EXISTING) != 0;
    }
    if (!moved) {
        set_error("Failed to atomically replace file");
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#else
    if (rename(temp_filename, filename) != 0) {
        set_errno_error("Failed to atomically replace file", filename);
        remove(temp_filename);
        free(temp_filename);
        return false;
    }
#endif

    free(temp_filename);
    return true;
}

bool file_write_text_atomic(const char *filename, const char *content) {
    if (!filename || !content) {
        set_error("Invalid parameters for atomic write");
        return false;
    }
    return file_write_binary_atomic(filename, content, strlen(content));
}
