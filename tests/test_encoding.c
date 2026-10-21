/*
 * npad - Encoding and Line Ending Tests
 * Unit tests for text encoding detection/conversion and line ending handling
 *
 * Author: Platima
 * https://npad.dev
 */

#include "test_framework.h"
#include "../src/core/file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FILE "test_encoding_temp.txt"

// Write raw bytes to the test file
static void write_bytes(const char *filename, const void *data, size_t size) {
    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

// Read raw bytes back; returns malloc'd buffer, sets *size
static unsigned char *read_bytes(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t) len + 1);
    if (buf) {
        *size = fread(buf, 1, (size_t) len, f);
        buf[*size] = '\0';
    }
    fclose(f);
    return buf;
}

// Write bytes, read them back with file_read_text_ex, save the text with the
// detected info and report whether the file is byte-for-byte what it was.
// The decoded text and info are handed back when the out-params are non-NULL.
static bool roundtrip_bytes(const unsigned char *data, size_t size, TextFileInfo *info_out,
                            char **text_out) {
    write_bytes(TEST_FILE, data, size);
    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    if (!content)
        return false;
    bool written = file_write_text_ex(TEST_FILE, content, &info);
    size_t back_size = 0;
    unsigned char *back = read_bytes(TEST_FILE, &back_size);
    bool same = written && back && back_size == size && memcmp(back, data, size) == 0;
    free(back);
    if (info_out)
        *info_out = info;
    if (text_out)
        *text_out = content;
    else
        free(content);
    return same;
}

TEST_CASE(detect_line_ending_crlf) {
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, file_detect_line_ending("one\r\ntwo\r\nthree"),
                   "CRLF text should detect as CRLF");
}

TEST_CASE(detect_line_ending_lf) {
    TEST_ASSERT_EQ(NPAD_EOL_LF, file_detect_line_ending("one\ntwo\nthree"),
                   "LF text should detect as LF");
}

TEST_CASE(detect_line_ending_cr) {
    TEST_ASSERT_EQ(NPAD_EOL_CR, file_detect_line_ending("one\rtwo\rthree"),
                   "CR text should detect as CR");
}

TEST_CASE(detect_line_ending_no_breaks) {
    TEST_ASSERT_EQ(NPAD_DEFAULT_LINE_ENDING, file_detect_line_ending("no line breaks"),
                   "Text without line breaks should get the platform's line ending");
    TEST_ASSERT_EQ(NPAD_DEFAULT_LINE_ENDING, file_detect_line_ending(""),
                   "Empty text should get the platform's line ending");
}

TEST_CASE(detect_line_ending_mixed_majority) {
    TEST_ASSERT_EQ(NPAD_EOL_LF, file_detect_line_ending("a\nb\nc\nd\r\ne"),
                   "Majority LF should win over minority CRLF");
}

TEST_CASE(detect_line_ending_tie_picks_a_present_style) {
    // A tie must never pick a style the file does not use: "a\nb\r" has no
    // CRLF at all, so saving it as CRLF would rewrite every line break
    TEST_ASSERT_EQ(NPAD_EOL_LF, file_detect_line_ending("a\nb\r"),
                   "LF/CR tie should pick LF, never the absent CRLF");
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, file_detect_line_ending("a\r\nb\r"),
                   "CRLF/CR tie should pick CRLF");
    TEST_ASSERT_EQ(NPAD_DEFAULT_LINE_ENDING, file_detect_line_ending("a\r\nb\n"),
                   "CRLF/LF tie should pick the platform's own style");
    TEST_ASSERT_EQ(NPAD_DEFAULT_LINE_ENDING, file_detect_line_ending("a\r\nb\nc\r"),
                   "Three-way tie should pick the platform's own style");
    TEST_ASSERT_EQ(NPAD_EOL_CR, file_detect_line_ending("a\rb\r\nc\nd\r"),
                   "A CR majority should still win");
}

TEST_CASE(convert_line_endings_to_lf) {
    char *converted = file_convert_line_endings("one\r\ntwo\rthree\nfour", NPAD_EOL_LF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\ntwo\nthree\nfour", converted,
                       "All line ending styles should convert to LF");
    free(converted);
}

TEST_CASE(convert_line_endings_to_crlf) {
    char *converted = file_convert_line_endings("one\ntwo\rthree", NPAD_EOL_CRLF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\r\ntwo\r\nthree", converted,
                       "All line ending styles should convert to CRLF");
    free(converted);
}

TEST_CASE(convert_line_endings_idempotent) {
    char *converted = file_convert_line_endings("one\r\ntwo\r\n", NPAD_EOL_CRLF);
    TEST_ASSERT_NOT_NULL(converted, "Conversion should succeed");
    TEST_ASSERT_STR_EQ("one\r\ntwo\r\n", converted, "CRLF to CRLF should be unchanged");
    free(converted);
}

TEST_CASE(read_utf8_plain) {
    write_bytes(TEST_FILE, "plain ascii\r\n", 13);

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("plain ascii\r\n", content, "Content should round-trip");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, info.encoding, "Plain ASCII should detect as UTF-8");
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, info.line_ending, "Line ending should be CRLF");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf8_bom) {
    const unsigned char data[] = { 0xEF, 0xBB, 0xBF, 'h', 'i', '\n' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("hi\n", content, "BOM should be stripped from content");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8_BOM, info.encoding, "UTF-8 BOM should be detected");
    TEST_ASSERT_EQ(NPAD_EOL_LF, info.line_ending, "Line ending should be LF");
    TEST_ASSERT(!info.decode_lossy, "Valid UTF-8 after the BOM is not lossy");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf8_bom_invalid_bytes_are_flagged) {
    // The BOM is authoritative (as in notepad.exe), but an invalid byte after
    // it cannot be saved back: it becomes U+FFFD and the load is flagged so
    // the editor asks before writing EF BF BD over the original
    const unsigned char latin1[] = { 0xEF, 0xBB, 0xBF, 'c', 'a', 'f', 0xE9 };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(!roundtrip_bytes(latin1, sizeof(latin1), &info, &content),
                "An invalid byte cannot round-trip");
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("caf\xEF\xBF\xBD", content, "The bad byte becomes U+FFFD");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8_BOM, info.encoding, "Still UTF-8 with BOM");
    TEST_ASSERT(info.decode_lossy, "The load is flagged as lossy");
    TEST_ASSERT(!info.truncated, "A lossy decode is not a truncation");
    free(content);

    // A sequence cut short at the end is ONE replacement, not one per byte
    const unsigned char cut[] = { 0xEF, 0xBB, 0xBF, 'a', 0xE4, 0xB8 };
    write_bytes(TEST_FILE, cut, sizeof(cut));
    content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("a\xEF\xBF\xBD", content, "A truncated sequence is one U+FFFD");
    TEST_ASSERT(info.decode_lossy, "And it is flagged");
    free(content);

    // Overlongs and encoded surrogates are invalid too; text around them stays
    const unsigned char forms[] = { 0xEF, 0xBB, 0xBF, 'x', 0xC0, 0xAF, 'y', 0xED, 0xA0, 0x80, 'z' };
    write_bytes(TEST_FILE, forms, sizeof(forms));
    content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("x\xEF\xBF\xBD\xEF\xBF\xBDy\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBDz", content,
                       "C0 AF and ED A0 80 are each one U+FFFD per byte (no valid prefix)");
    TEST_ASSERT(info.decode_lossy, "And it is flagged");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf8_detection_matches_unicode_well_formedness) {
    // Exactly the well-formed sequences of Unicode table 3-7 make a file UTF-8
    struct {
        unsigned char bytes[5];
        size_t len;
        TextEncoding expected;
        const char *what;
    } cases[] = {
        { { 'a', 0xF0, 0x9F, 0x98, 0x80 }, 5, NPAD_ENC_UTF8, "4-byte emoji" },
        { { 'a', 0xEF, 0xBF, 0xBF }, 4, NPAD_ENC_UTF8, "U+FFFF" },
        { { 'a', 0xF4, 0x8F, 0xBF, 0xBF }, 5, NPAD_ENC_UTF8, "U+10FFFF" },
        { { 'a', 0xED, 0x9F, 0xBF }, 4, NPAD_ENC_UTF8, "U+D7FF, just below the surrogates" },
        { { 'a', 0xC0, 0xAF }, 3, NPAD_ENC_ANSI, "overlong 2-byte" },
        { { 'a', 0xE0, 0x80, 0xAF }, 4, NPAD_ENC_ANSI, "overlong 3-byte" },
        { { 'a', 0xED, 0xA0, 0x80 }, 4, NPAD_ENC_ANSI, "encoded surrogate" },
        { { 'a', 0xF4, 0x90, 0x80, 0x80 }, 5, NPAD_ENC_ANSI, "above U+10FFFF" },
        { { 'a', 0xF5, 0x80, 0x80, 0x80 }, 5, NPAD_ENC_ANSI, "F5 lead byte" },
        { { 'a', 0xE4, 0xB8 }, 3, NPAD_ENC_ANSI, "truncated sequence" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        write_bytes(TEST_FILE, cases[i].bytes, cases[i].len);
        TextFileInfo info;
        char *content = file_read_text_ex(TEST_FILE, &info);
        TEST_ASSERT_NOT_NULL(content, cases[i].what);
        TEST_ASSERT_EQ(cases[i].expected, info.encoding, cases[i].what);
        free(content);
    }
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_resets_every_flag) {
    // file_read_text_ex owns the whole struct: stale flags from a previous
    // document must not survive into a clean one
    write_bytes(TEST_FILE, "clean\n", 6);
    TextFileInfo info;
    info.truncated = true;
    info.decode_lossy = true;
    info.utf16_no_bom = true;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT(!info.truncated && !info.decode_lossy && !info.utf16_no_bom,
                "A clean UTF-8 file clears every flag");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf16_le_bom) {
    // "Hi\r\n" in UTF-16 LE with BOM
    const unsigned char data[] = { 0xFF, 0xFE, 'H', 0, 'i', 0, '\r', 0, '\n', 0 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("Hi\r\n", content, "UTF-16 LE should convert to UTF-8");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, info.encoding, "UTF-16 LE BOM should be detected");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf16_be_bom) {
    // "Hi" in UTF-16 BE with BOM
    const unsigned char data[] = { 0xFE, 0xFF, 0, 'H', 0, 'i' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_STR_EQ("Hi", content, "UTF-16 BE should convert to UTF-8");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_BE, info.encoding, "UTF-16 BE BOM should be detected");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(read_utf8_multibyte) {
    // "héllo" in UTF-8 (é = 0xC3 0xA9)
    const unsigned char data[] = { 'h', 0xC3, 0xA9, 'l', 'l', 'o' };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_ENC_UTF8, info.encoding, "Valid multi-byte UTF-8 should detect as UTF-8");
    TEST_ASSERT_EQ(6, (long) strlen(content), "UTF-8 bytes should be preserved");

    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(roundtrip_utf16_le) {
    // Write a UTF-16 LE file, read it, save it back, verify identical bytes
    const unsigned char data[] = { 0xFF, 0xFE, 'A', 0, '\r', 0, '\n', 0, 'B', 0 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");

    bool written = file_write_text_ex(TEST_FILE, content, &info);
    TEST_ASSERT(written, "Write should succeed");
    free(content);

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(sizeof(data), size, "Round-tripped file should have identical size");
    TEST_ASSERT_EQ(0, memcmp(raw, data, sizeof(data)), "Round-tripped bytes should be identical");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

// Fill buf with n UTF-16 code units of "abc...z" (cycling), little or big
// endian, and return the byte length
static size_t make_utf16_ascii(unsigned char *buf, size_t n, bool big_endian) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) ('a' + i % 26);
        buf[i * 2] = big_endian ? 0 : c;
        buf[i * 2 + 1] = big_endian ? c : 0;
    }
    return n * 2;
}

TEST_CASE(utf16_without_bom_detected_at_every_size) {
    // Below 10 code units the old threshold could never be met, and every
    // BOM-less file also tripped the binary-file prompt
    const size_t sizes[] = { 2, 4, 9, 10, 300 };
    for (int be = 0; be <= 1; be++) {
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            unsigned char buf[600];
            size_t len = make_utf16_ascii(buf, sizes[s], be != 0);
            TextFileInfo info;
            char *content = NULL;
            TEST_ASSERT(roundtrip_bytes(buf, len, &info, &content),
                        "BOM-less UTF-16 should round-trip byte for byte");
            TEST_ASSERT_EQ(be ? NPAD_ENC_UTF16_BE : NPAD_ENC_UTF16_LE, info.encoding,
                           "BOM-less UTF-16 should be detected with its byte order");
            TEST_ASSERT(info.utf16_no_bom, "The missing BOM should be recorded");
            TEST_ASSERT(!info.truncated && !info.decode_lossy, "Nothing lost");
            TEST_ASSERT_EQ((long) sizes[s], (long) strlen(content), "Every unit decoded");
            TEST_ASSERT(content[0] == 'a' &&
                            content[sizes[s] - 1] == (char) ('a' + (sizes[s] - 1) % 26),
                        "Text decoded in order");
            free(content);

            write_bytes(TEST_FILE, buf, len);
            TEST_ASSERT(!file_looks_binary(TEST_FILE), "BOM-less UTF-16 is text, not binary");
        }
    }
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_without_bom_short_crlf_file) {
    // The report's case: "hi\r\n" opened as "h" behind a binary warning
    const unsigned char data[] = { 'h', 0, 'i', 0, '\r', 0, '\n', 0 };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(roundtrip_bytes(data, sizeof(data), &info, &content), "Round-trips exactly");
    TEST_ASSERT_STR_EQ("hi\r\n", content, "Whole text decoded");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, info.encoding, "Detected as UTF-16 LE");
    TEST_ASSERT_EQ(NPAD_EOL_CRLF, info.line_ending, "CRLF detected");
    TEST_ASSERT(!info.truncated, "Not truncated");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_without_bom_tolerates_zero_low_bytes) {
    // U+4E00 and U+0100 have a zero LOW byte; a minority of them must not
    // stop the file being recognised
    const unsigned char le[] = { 'a', 0, 0x00, 0x4E, 'b', 0, 0x00, 0x01, 'c', 0 };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(roundtrip_bytes(le, sizeof(le), &info, &content), "Round-trips exactly");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_LE, info.encoding, "Detected as UTF-16 LE");
    TEST_ASSERT_STR_EQ("a\xE4\xB8\x80" "b\xC4\x80" "c", content, "U+4E00 and U+0100 decoded");
    free(content);

    const unsigned char be[] = { 0, 'a', 0x4E, 0x00, 0, 'b', 0x01, 0x00, 0, 'c' };
    TEST_ASSERT(roundtrip_bytes(be, sizeof(be), &info, &content), "Round-trips exactly");
    TEST_ASSERT_EQ(NPAD_ENC_UTF16_BE, info.encoding, "Detected as UTF-16 BE");
    TEST_ASSERT_STR_EQ("a\xE4\xB8\x80" "b\xC4\x80" "c", content, "U+4E00 and U+0100 decoded");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_heuristic_still_rejects_binary) {
    // Little-endian 32-bit integers have zero bytes in odd positions too, but
    // also whole zero code units, which text never has
    const unsigned char ints[] = { 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0 };
    write_bytes(TEST_FILE, ints, sizeof(ints));
    TEST_ASSERT(file_looks_binary(TEST_FILE), "Integer data still looks binary");
    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "The readable prefix loads");
    TEST_ASSERT(info.encoding != NPAD_ENC_UTF16_LE, "Not mistaken for UTF-16");
    TEST_ASSERT(info.truncated, "And its NUL marks it truncated, as before");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_without_bom_gains_no_bom) {
    // The report's repro: a 26-byte BOM-less "abcdefghijk\r\n" came back as
    // 28 bytes starting FF FE, which a program reading it raw sees as U+FEFF
    unsigned char data[26];
    size_t len = make_utf16_ascii(data, 11, false);
    data[len++] = '\r';
    data[len++] = 0;
    data[len++] = '\n';
    data[len++] = 0;
    TextFileInfo info;
    TEST_ASSERT(roundtrip_bytes(data, len, &info, NULL), "Saved back without a BOM");

    // An edit keeps the BOM-less form, too
    TEST_ASSERT(file_write_text_ex(TEST_FILE, "x", &info), "Write should succeed");
    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(2, (long) size, "Just the one code unit, no BOM");
    TEST_ASSERT(raw[0] == 'x' && raw[1] == 0, "UTF-16 LE 'x'");
    free(raw);

    // New UTF-16 documents (all flags clear) still get a BOM, as in notepad.exe
    TextFileInfo fresh = { .encoding = NPAD_ENC_UTF16_BE, .line_ending = NPAD_EOL_CRLF };
    TEST_ASSERT(file_write_text_ex(TEST_FILE, "x", &fresh), "Write should succeed");
    raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(4, (long) size, "BOM plus one code unit");
    TEST_ASSERT(raw[0] == 0xFE && raw[1] == 0xFF && raw[2] == 0 && raw[3] == 'x',
                "UTF-16 BE BOM then 'x'");
    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_odd_byte_count_is_flagged) {
    // The dangling last byte used to vanish on save without a word
    const unsigned char data[] = { 0xFF, 0xFE, 'h', 0, 'i', 0, 'X' };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(!roundtrip_bytes(data, sizeof(data), &info, &content),
                "Half a code unit cannot be saved back");
    TEST_ASSERT_STR_EQ("hi\xEF\xBF\xBD", content, "The dangling byte shows as U+FFFD");
    TEST_ASSERT(info.decode_lossy, "And the load is flagged so the editor asks first");
    TEST_ASSERT(!info.truncated, "Not a truncation");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(utf16_unpaired_surrogates_are_flagged) {
    const unsigned char high[] = { 0xFF, 0xFE, 'a', 0, 0x00, 0xD8, 'b', 0 };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(!roundtrip_bytes(high, sizeof(high), &info, &content),
                "An unpaired surrogate cannot be saved back");
    TEST_ASSERT_STR_EQ("a\xEF\xBF\xBD" "b", content, "Lone high surrogate becomes U+FFFD");
    TEST_ASSERT(info.decode_lossy, "And the load is flagged");
    free(content);

    const unsigned char low[] = { 0xFF, 0xFE, 0x00, 0xDC, 'b', 0 };
    TEST_ASSERT(!roundtrip_bytes(low, sizeof(low), &info, &content), "Not saved back either");
    TEST_ASSERT_STR_EQ("\xEF\xBF\xBD" "b", content, "Lone low surrogate becomes U+FFFD");
    TEST_ASSERT(info.decode_lossy, "And the load is flagged");
    free(content);

    // A proper pair is not lossy and round-trips
    const unsigned char pair[] = { 0xFF, 0xFE, 0x3D, 0xD8, 0x00, 0xDE };
    TEST_ASSERT(roundtrip_bytes(pair, sizeof(pair), &info, &content), "A pair round-trips");
    TEST_ASSERT_STR_EQ("\xF0\x9F\x98\x80", content, "U+1F600 decoded");
    TEST_ASSERT(!info.decode_lossy, "A valid pair is not lossy");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(detect_and_roundtrip_ansi_high_byte) {
    // A byte outside ASCII that is not valid UTF-8 marks the file as ANSI
    // (Windows code page; Latin-1 on other platforms). Saving it back as ANSI
    // must reproduce the original byte, so a "save as ANSI" / reopen keeps ANSI
    // rather than reverting to UTF-8. (Pure-ASCII files stay UTF-8 by design,
    // since ANSI and UTF-8 are byte-identical for ASCII.)
    const unsigned char data[] = { 'c', 'a', 'f', 0xE9 };
    write_bytes(TEST_FILE, data, sizeof(data));

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_ENC_ANSI, info.encoding, "High-byte non-UTF-8 text should detect as ANSI");

    bool written = file_write_text_ex(TEST_FILE, content, &info);
    TEST_ASSERT(written, "Write should succeed");
    free(content);

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_EQ(sizeof(data), size, "Round-tripped ANSI file should have identical size");
    TEST_ASSERT_EQ(0, memcmp(raw, data, sizeof(data)), "Round-tripped ANSI bytes should match");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(ansi_lossy_detection) {
    // ASCII and Latin-1-representable text survives an ANSI save unchanged;
    // anything beyond (emoji, CJK) degrades to '?' and must be flagged so
    // the editor can warn before a lossy save
    TEST_ASSERT(!file_ansi_is_lossy("plain ascii text"), "ASCII should not be lossy in ANSI");
    TEST_ASSERT(!file_ansi_is_lossy("caf\xC3\xA9"), "Latin-1 text (e-acute) should not be lossy");
    TEST_ASSERT(file_ansi_is_lossy("hi \xF0\x9F\x98\x91"), "Emoji should be lossy in ANSI");
    TEST_ASSERT(!file_ansi_is_lossy(NULL), "NULL content should not be flagged");
#ifndef _WIN32
    // Off Windows "ANSI" is Windows-1252: the euro sign, curly quotes and
    // dashes are representable, the C1 control U+0080 (whose byte 1252 uses
    // for the euro) and best-fit candidates are not
    TEST_ASSERT(!file_ansi_is_lossy("\xE2\x82\xAC 5 \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x94"),
                "Euro, curly quotes and em dash are in Windows-1252");
    TEST_ASSERT(file_ansi_is_lossy("\xC2\x80"), "U+0080 is not in Windows-1252");
    TEST_ASSERT(file_ansi_is_lossy("\xE2\x88\x9E"), "U+221E is not in Windows-1252");
    TEST_ASSERT(file_ansi_is_lossy("\xC5\x82"), "U+0142 is not in Windows-1252");
#endif
}

#ifndef _WIN32
TEST_CASE(ansi_is_windows_1252_off_windows) {
    // "Quote" in curly quotes, an en dash and a euro sign, as Windows writes
    // them. Read as Latin-1 these were C1 controls (U+0093, U+0094, ...).
    const unsigned char data[] = { 0x93, 'Q', 'u', 'o', 't', 'e', 0x94, ' ',
                                   0x96, ' ', 0x80, '5' };
    TextFileInfo info;
    char *content = NULL;
    TEST_ASSERT(roundtrip_bytes(data, sizeof(data), &info, &content), "Round-trips exactly");
    TEST_ASSERT_EQ(NPAD_ENC_ANSI, info.encoding, "Detected as ANSI");
    TEST_ASSERT_STR_EQ("\xE2\x80\x9CQuote\xE2\x80\x9D \xE2\x80\x93 \xE2\x82\xAC" "5", content,
                       "Decoded as Windows-1252");
    TEST_ASSERT(!info.decode_lossy, "Windows-1252 decoding loses nothing");
    free(content);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(ansi_every_byte_roundtrips_off_windows) {
    // Every non-NUL byte, including the five 1252 leaves undefined. CR is left
    // out: beside the LF it would make mixed line endings, which a save
    // normalises by design
    unsigned char data[254];
    size_t n = 0;
    for (int b = 1; b <= 255; b++) {
        if (b != '\r')
            data[n++] = (unsigned char) b;
    }
    TextFileInfo info;
    TEST_ASSERT(roundtrip_bytes(data, sizeof(data), &info, NULL), "Every byte round-trips");
    TEST_ASSERT_EQ(NPAD_ENC_ANSI, info.encoding, "Detected as ANSI");
    TEST_ASSERT(!info.decode_lossy && !info.truncated, "Nothing lost");
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(ansi_save_writes_question_marks_not_lookalikes) {
    // What the lossy warning promises: unrepresentable characters become '?'
    // (never a best-fit look-alike), representable ones their 1252 byte
    TextFileInfo info = { .encoding = NPAD_ENC_ANSI, .line_ending = NPAD_EOL_LF };
    TEST_ASSERT(file_write_text_ex(TEST_FILE, "\xE2\x82\xAC \xE2\x88\x9E \xC5\x82", &info),
                "Write should succeed");
    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    const unsigned char expected[] = { 0x80, ' ', '?', ' ', '?' };
    TEST_ASSERT_EQ(sizeof(expected), size, "One byte per character");
    TEST_ASSERT_EQ(0, memcmp(raw, expected, sizeof(expected)), "Euro is 0x80, the rest '?'");
    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}
#endif

TEST_CASE(roundtrip_lf_preserved) {
    // An LF file saved with CRLF content from the editor keeps LF on disk
    write_bytes(TEST_FILE, "one\ntwo\n", 8);

    TextFileInfo info;
    char *content = file_read_text_ex(TEST_FILE, &info);
    TEST_ASSERT_NOT_NULL(content, "Read should succeed");
    TEST_ASSERT_EQ(NPAD_EOL_LF, info.line_ending, "LF should be detected");
    free(content);

    // The editor control hands back CRLF; the writer must convert to LF
    bool written = file_write_text_ex(TEST_FILE, "one\r\ntwo\r\nthree\r\n", &info);
    TEST_ASSERT(written, "Write should succeed");

    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Raw read should succeed");
    TEST_ASSERT_STR_EQ("one\ntwo\nthree\n", (char *) raw,
                       "Saved file should use the original LF endings");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(write_failure_preserves_original) {
    // A failed write must never delete the existing file
    write_bytes(TEST_FILE, "original", 8);

    // Writing to an invalid path must fail cleanly...
    bool result = file_write_text("", "new content");
    TEST_ASSERT(!result, "Write to empty path should fail");

    // ...and the unrelated original file is intact
    size_t size = 0;
    unsigned char *raw = read_bytes(TEST_FILE, &size);
    TEST_ASSERT_NOT_NULL(raw, "Original file should still exist");
    TEST_ASSERT_STR_EQ("original", (char *) raw, "Original content should be intact");

    free(raw);
    test_cleanup_temp_file(TEST_FILE);
}

TEST_CASE(encoding_names) {
    TEST_ASSERT_STR_EQ("UTF-8", file_encoding_name(NPAD_ENC_UTF8), "UTF-8 name");
    TEST_ASSERT_STR_EQ("UTF-16 LE", file_encoding_name(NPAD_ENC_UTF16_LE), "UTF-16 LE name");
    TEST_ASSERT_STR_EQ("Windows (CRLF)", file_line_ending_name(NPAD_EOL_CRLF), "CRLF name");
    TEST_ASSERT_STR_EQ("Unix (LF)", file_line_ending_name(NPAD_EOL_LF), "LF name");
}

TEST_CASE(open_relative_parent_path) {
    // Paths containing ".." are legitimate for a desktop editor and must
    // not be rejected by path validation (regression test: earlier
    // versions blocked every path containing "..")
    write_bytes(TEST_FILE, "content", 7);

    char *content = file_read_text("tests/../" TEST_FILE);
    TEST_ASSERT_NOT_NULL(content, "Reading via a path containing .. should work");
    TEST_ASSERT_STR_EQ("content", content, "Content should match");
    free(content);

    test_cleanup_temp_file(TEST_FILE);
}

int main(void) {
    TEST_INIT();

    RUN_TEST(detect_line_ending_crlf);
    RUN_TEST(detect_line_ending_lf);
    RUN_TEST(detect_line_ending_cr);
    RUN_TEST(detect_line_ending_no_breaks);
    RUN_TEST(detect_line_ending_mixed_majority);
    RUN_TEST(detect_line_ending_tie_picks_a_present_style);
    RUN_TEST(convert_line_endings_to_lf);
    RUN_TEST(convert_line_endings_to_crlf);
    RUN_TEST(convert_line_endings_idempotent);
    RUN_TEST(read_utf8_plain);
    RUN_TEST(read_utf8_bom);
    RUN_TEST(read_utf8_bom_invalid_bytes_are_flagged);
    RUN_TEST(utf8_detection_matches_unicode_well_formedness);
    RUN_TEST(read_resets_every_flag);
    RUN_TEST(read_utf16_le_bom);
    RUN_TEST(read_utf16_be_bom);
    RUN_TEST(read_utf8_multibyte);
    RUN_TEST(roundtrip_utf16_le);
    RUN_TEST(utf16_without_bom_detected_at_every_size);
    RUN_TEST(utf16_without_bom_short_crlf_file);
    RUN_TEST(utf16_without_bom_tolerates_zero_low_bytes);
    RUN_TEST(utf16_heuristic_still_rejects_binary);
    RUN_TEST(utf16_without_bom_gains_no_bom);
    RUN_TEST(utf16_odd_byte_count_is_flagged);
    RUN_TEST(utf16_unpaired_surrogates_are_flagged);
    RUN_TEST(detect_and_roundtrip_ansi_high_byte);
    RUN_TEST(ansi_lossy_detection);
#ifndef _WIN32
    RUN_TEST(ansi_is_windows_1252_off_windows);
    RUN_TEST(ansi_every_byte_roundtrips_off_windows);
    RUN_TEST(ansi_save_writes_question_marks_not_lookalikes);
#endif
    RUN_TEST(roundtrip_lf_preserved);
    RUN_TEST(write_failure_preserves_original);
    RUN_TEST(encoding_names);
    RUN_TEST(open_relative_parent_path);

    TEST_SUMMARY();
    return 0;
}
