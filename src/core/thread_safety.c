/*
 * npad - Thread Safety Utilities Implementation
 * Basic thread safety primitives for critical sections
 *
 * Author: Platima
 * https://github.com/platima/npad
 */

#include "thread_safety.h"

// Global mutexes
npad_mutex_t g_settings_mutex;
npad_mutex_t g_editor_mutex;

bool npad_mutex_init(npad_mutex_t *mutex) {
    if (!mutex)
        return false;

#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

void npad_mutex_destroy(npad_mutex_t *mutex) {
    if (!mutex)
        return;

#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void npad_mutex_lock(npad_mutex_t *mutex) {
    if (!mutex)
        return;

#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

void npad_mutex_unlock(npad_mutex_t *mutex) {
    if (!mutex)
        return;

#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

bool thread_safety_init(void) {
    if (!npad_mutex_init(&g_settings_mutex)) {
        return false;
    }
    if (!npad_mutex_init(&g_editor_mutex)) {
        npad_mutex_destroy(&g_settings_mutex);
        return false;
    }
    return true;
}

void thread_safety_cleanup(void) {
    npad_mutex_destroy(&g_settings_mutex);
    npad_mutex_destroy(&g_editor_mutex);
}