/*
 * npad - Thread Safety Utilities
 * Basic thread safety primitives for critical sections
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#ifndef THREAD_SAFETY_H
#define THREAD_SAFETY_H

#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION npad_mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t npad_mutex_t;
#endif

// Mutex operations
bool npad_mutex_init(npad_mutex_t *mutex);
void npad_mutex_destroy(npad_mutex_t *mutex);
void npad_mutex_lock(npad_mutex_t *mutex);
void npad_mutex_unlock(npad_mutex_t *mutex);

// Global mutexes for critical sections
extern npad_mutex_t g_settings_mutex;
extern npad_mutex_t g_editor_mutex;

// Initialization/cleanup
bool thread_safety_init(void);
void thread_safety_cleanup(void);

#endif // THREAD_SAFETY_H