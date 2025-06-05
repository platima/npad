/*
 * npad - Main Header
 * Global definitions and version information
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef MAIN_H
#define MAIN_H

#define STRINGIFY(x) #x

#define NPAD_VERSION_MAJOR 0
#define NPAD_VERSION_MINOR 1
#define NPAD_VERSION_PATCH 4
#define NPAD_VERSION_RELEASE "dev"

// Version information
#ifndef NPAD_VERSION
#define NPAD_VERSION                                                                               \
    STRINGIFY(NPAD_VERSION_MAJOR)                                                                  \
    "." STRINGIFY(NPAD_VERSION_MINOR) "." STRINGIFY(NPAD_VERSION_PATCH) "-" NPAD_VERSION_RELEASE
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
