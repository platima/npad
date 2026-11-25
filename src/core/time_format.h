/*
 * npad - Short time and date formats
 * Notepad's F5 stamp and the print header's &t / &d use Windows' short forms:
 * the time without seconds (TIME_NOSECONDS) and the short date, which in
 * every common Windows locale carries a four-digit year. A POSIX locale only
 * offers its full forms (nl_langinfo T_FMT and D_FMT, often "%T" or "%r" with
 * seconds, and "%d/%m/%y"-style two-digit years), so these rewrite a format
 * string into the short form. Pure string work: no locale is read here, which
 * keeps it testable and lets the platform layer pass whatever its locale
 * reports.
 *
 * Both take a strftime format and write one to out. The text between
 * conversions is copied byte for byte, so a format in the locale's own
 * encoding stays in it.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef TIME_FORMAT_H
#define TIME_FORMAT_H

#include <stdbool.h>
#include <stddef.h>

// The time format minus its seconds: "%H:%M:%S" -> "%H:%M", "%I:%M:%S %p" ->
// "%I:%M %p", "%T" -> "%H:%M", "%r" -> "%I:%M %p", and a seconds field with a
// unit of its own (ja_JP and ko_KR write one) loses the unit too. NULL, empty,
// or a format left empty gives "%H:%M". false (out set to "" when cap allows)
// only when out is too small.
bool time_format_short_time(const char *fmt, char *out, size_t cap);

// The date format with four-digit years: "%y" -> "%Y" and "%D" ->
// "%m/%d/%Y". NULL or empty gives "%m/%d/%Y". false as above.
bool time_format_short_date(const char *fmt, char *out, size_t cap);

#endif // TIME_FORMAT_H
