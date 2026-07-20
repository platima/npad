/*
 * npad - Update check helpers
 * Pure parsing/comparison logic for the on-demand "Check for Updates"
 * feature (Help menu). Networking lives in the platform layer; everything
 * here is testable without it. Updates are strictly user-initiated - npad
 * never checks in the background.
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

#endif // UPDATE_CHECK_H
