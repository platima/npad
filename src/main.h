/*
 * npad - Main Header
 * Global definitions and version information
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef MAIN_H
#define MAIN_H

// Two-level macro so the argument is expanded before stringification
#define NPAD_STRINGIFY_HELPER(x) #x
#define NPAD_STRINGIFY(x) NPAD_STRINGIFY_HELPER(x)

#define NPAD_VERSION_MAJOR 0
#define NPAD_VERSION_MINOR 10
#define NPAD_VERSION_PATCH 2
#define NPAD_VERSION_RELEASE "dev"

// Version information (the Makefile may override with a git-derived string)
#ifndef NPAD_VERSION
#define NPAD_VERSION                                                                               \
    NPAD_STRINGIFY(NPAD_VERSION_MAJOR)                                                             \
    "." NPAD_STRINGIFY(NPAD_VERSION_MINOR) "." NPAD_STRINGIFY(                                     \
        NPAD_VERSION_PATCH) "-" NPAD_VERSION_RELEASE
#endif

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define NPAD_PLATFORM_WINDOWS
#elif defined(__APPLE__)
#define NPAD_PLATFORM_MACOS
#elif defined(__linux__)
#define NPAD_PLATFORM_LINUX
#else
#error "Unsupported platform"
#endif

#endif // MAIN_H
