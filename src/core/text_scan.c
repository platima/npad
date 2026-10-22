/*
 * npad - Chunked UTF-16 text scans
 *
 * Author: Platima
 * https://npad.dev
 */

#include "text_scan.h"

void line_scan_init(LineScan *scan) {
    if (!scan)
        return;
    scan->breaks = 0;
    scan->line_start = 0;
    scan->consumed = 0;
}

void line_scan_feed(LineScan *scan, const uint16_t *units, size_t count) {
    if (!scan || !units)
        return;
    for (size_t i = 0; i < count; i++) {
        if (units[i] == 0x000D) {
            scan->breaks++;
            // The line after this break starts at the next unit
            scan->line_start = scan->consumed + (long) i + 1;
        }
    }
    scan->consumed += (long) count;
}

static bool is_high_surrogate(uint16_t u) {
    return u >= 0xD800 && u <= 0xDBFF;
}

static bool is_low_surrogate(uint16_t u) {
    return u >= 0xDC00 && u <= 0xDFFF;
}

void astral_scan_init(AstralScan *scan) {
    if (!scan)
        return;
    scan->found = false;
    scan->pending_high = false;
}

void astral_scan_feed(AstralScan *scan, const uint16_t *units, size_t count) {
    if (!scan || !units || count == 0 || scan->found)
        return;
    for (size_t i = 0; i < count; i++) {
        if (scan->pending_high && is_low_surrogate(units[i])) {
            scan->found = true;
            scan->pending_high = false;
            return;
        }
        scan->pending_high = is_high_surrogate(units[i]);
    }
}
