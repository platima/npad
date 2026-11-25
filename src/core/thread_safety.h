/*
 * npad - Thread Safety Utilities
 * Basic thread safety primitives for critical sections
 *
 * Author: Platima
 * https://npad.dev
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

// Mutex operations. NOT recursive on every platform: a Win32 critical section
// lets the owning thread lock again, a default pthread mutex deadlocks. Code
// must never lock a mutex it already holds (nor call back into code that
// does), or it works on Windows and hangs on Linux.
bool npad_mutex_init(npad_mutex_t *mutex);
void npad_mutex_destroy(npad_mutex_t *mutex);
void npad_mutex_lock(npad_mutex_t *mutex);
void npad_mutex_unlock(npad_mutex_t *mutex);

// Global mutexes for critical sections. Only settings has one: the editor
// state runs on the UI thread alone and is not locked (see editor.h).
extern npad_mutex_t g_settings_mutex;

// Initialization/cleanup
bool thread_safety_init(void);
void thread_safety_cleanup(void);

#endif // THREAD_SAFETY_H