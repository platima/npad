/*
 * npad - Command-line arguments as UTF-8 (Win32)
 *
 * The narrow argv a MinGW main() receives is in the ANSI code page, and npad's
 * core reads every string as UTF-8. So "Résumé.txt" double-clicked in
 * Explorer arrived as bytes that decoded to U+FFFD in place of each accented
 * letter, and a name the code page cannot spell at all (Japanese, on a Western
 * system) arrived as "??.txt": both failed to open, on the launch path
 * non-English users meet most. The wide command line is the only lossless
 * source.
 *
 * The manifest's activeCodePage=UTF-8 would also fix argv, but it changes
 * CP_ACP for the whole process - and CP_ACP is how npad reads and writes the
 * ANSI files that classic Notepad users have. So it is deliberately not used.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef ARGS_WIN32_H
#define ARGS_WIN32_H

#include <wchar.h>

// Split a wide command line (GetCommandLineW()) the way the C runtime does
// (CommandLineToArgvW) and return its arguments as malloc'd UTF-8 strings,
// NULL-terminated, with the count in *argc_out. NULL on failure, in which case
// the caller keeps the runtime's own argv. Free with args_win32_free_argv.
char **args_win32_utf8_argv(const wchar_t *command_line, int *argc_out);
void args_win32_free_argv(char **argv, int argc);

#endif // ARGS_WIN32_H
