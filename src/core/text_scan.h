/*
 * npad - Chunked UTF-16 text scans
 * Questions a UI layer asks about its document's text without holding a copy
 * of all of it: the Win32 port reads RichEdit's text a fixed-size chunk at a
 * time (EM_GETTEXTRANGE) and feeds each chunk here, so the answer for a 50 MB
 * document costs a small buffer rather than a second 100 MB copy.
 *
 *   - LineScan: logical lines before a position, and where the last one
 *     starts (Ln/Col and the line count with Word Wrap on, where RichEdit's
 *     own line messages count wrapped display rows)
 *   - AstralScan: does the text hold a character outside the BMP (a
 *     surrogate pair), even one split across two chunks
 *
 * Breaks are bare CR (U+000D), the form RichEdit stores in plain-text mode
 * and returns from EM_GETTEXTRANGE; LF is ordinary text here. Units are
 * UTF-16 code units, so a column counted from these offsets matches the
 * control's own character positions.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef TEXT_SCAN_H
#define TEXT_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    long breaks;     // CR breaks seen so far = the 0-based line of the next unit
    long line_start; // Offset just after the last CR (0 when there was none)
    long consumed;   // Units fed so far = the offset of the next unit
} LineScan;

void line_scan_init(LineScan *scan);

// Feed the next count units, in document order, starting at offset
// scan->consumed. NULL or zero units is a no-op.
void line_scan_feed(LineScan *scan, const uint16_t *units, size_t count);

typedef struct {
    bool found;        // A high surrogate followed by a low one was seen
    bool pending_high; // The last unit fed was a high surrogate
} AstralScan;

void astral_scan_init(AstralScan *scan);

// Feed the next count units; a pair split across two feeds still counts. A
// lone surrogate does not (no font renders it either way).
void astral_scan_feed(AstralScan *scan, const uint16_t *units, size_t count);

#endif // TEXT_SCAN_H
