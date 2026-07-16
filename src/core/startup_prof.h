/*
 * npad - Startup profiling
 * Lightweight monotonic timestamps for the launch path, surfaced on the
 * hidden Debug page in Preferences (Ctrl+Shift+. or Shift+click the menu).
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef STARTUP_PROF_H
#define STARTUP_PROF_H

#define STARTUP_PROF_MAX 16

// Record a named phase mark at the current monotonic time. Cheap (one
// clock read); silently ignores marks beyond STARTUP_PROF_MAX.
void startup_prof_mark(const char *phase);

// Number of marks recorded so far
int startup_prof_count(void);

// Phase name of mark i (borrowed pointer; valid for the process lifetime)
const char *startup_prof_name(int i);

// Milliseconds from the first mark to mark i
double startup_prof_ms(int i);

#endif // STARTUP_PROF_H
