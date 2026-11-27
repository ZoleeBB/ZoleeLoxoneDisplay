#pragma once
#include <pthread.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    return &lock;
}
#define xSemaphoreTake(lock,timeout) pthread_mutex_lock(lock)
#define xSemaphoreGive(lock) pthread_mutex_unlock(lock)
