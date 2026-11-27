#pragma once
#include "FreeRTOS.h"

typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s)
{
    return pthread_mutex_init(s, NULL) == 0 ? s : NULL;
}
static inline void xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout)
{
    (void)timeout;
    pthread_mutex_lock(s);
}
static inline void xSemaphoreGive(SemaphoreHandle_t s) { pthread_mutex_unlock(s); }
