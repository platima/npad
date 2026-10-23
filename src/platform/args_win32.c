/*
 * npad - Command-line arguments as UTF-8 (Win32)
 *
 * Author: Platima
 * https://npad.dev
 */

#include "args_win32.h"
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>

void args_win32_free_argv(char **argv, int argc) {
    if (!argv)
        return;
    for (int i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);
}

char **args_win32_utf8_argv(const wchar_t *command_line, int *argc_out) {
    if (!command_line || !argc_out)
        return NULL;

    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(command_line, &wargc);
    if (!wargv)
        return NULL;
    if (wargc < 0) {
        LocalFree(wargv);
        return NULL;
    }

    char **argv = calloc((size_t) wargc + 1, sizeof(char *));
    if (!argv) {
        LocalFree(wargv);
        return NULL;
    }

    for (int i = 0; i < wargc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        char *arg = bytes > 0 ? malloc((size_t) bytes) : NULL;
        if (!arg || WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, bytes, NULL, NULL) <= 0) {
            free(arg);
            args_win32_free_argv(argv, i);
            LocalFree(wargv);
            return NULL;
        }
        argv[i] = arg;
    }

    LocalFree(wargv);
    *argc_out = wargc;
    return argv;
}
