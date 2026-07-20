/*
 * npad - Update check helpers
 * Pure parsing/comparison logic for the "Check for Updates" feature.
 * Networking lives in the platform layer; everything here is testable
 * without it. Updates are opt-in and off by default - npad never checks
 * automatically unless the user enables it in Preferences > Updates.
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <stdbool.h>
#include <stddef.h>

// Extract the "tag_name" value (e.g. "v0.15.0") from a GitHub releases API
// JSON response into out. Returns false when absent or too long.
bool update_extract_tag(const char *json, char *out, size_t out_cap);

// Compare two dotted version strings numerically (an optional leading 'v'
// and any suffix after the numeric triple, e.g. "-dev", are ignored):
// negative when a < b, 0 when equal, positive when a > b.
int update_version_compare(const char *a, const char *b);

// Parse the leading 64-hex-digit SHA-256 digest from a .sha256 file's text
// (formats like "<hex>  <filename>") into out[65], lowercased. Returns
// false when the text does not start with a valid digest.
bool update_parse_sha256(const char *text, char *out);

// Whether an available update should be surfaced: true when latest is
// version-newer than current AND latest != skipped. An empty/NULL latest
// (no successful check yet) yields false. Drives the Help-menu indicator
// and the notify/prompt/auto decision.
bool update_is_newer_unskipped(const char *current, const char *latest, const char *skipped);

#endif // UPDATE_CHECK_H
