#ifndef STUBS_FREERTOS_TASK_H
#define STUBS_FREERTOS_TASK_H

#include <stdint.h>
#include <stdlib.h>

typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;

#define pdPASS 1

static inline void vTaskDelete(TaskHandle_t t) { (void)t; }
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)malloc(1); }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { free(s); }
static inline int xSemaphoreTake(SemaphoreHandle_t s, uint32_t blk) { (void)s; (void)blk; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
static inline BaseType_t xTaskCreate(void (*fn)(void*), const char *n, uint32_t st, void *p, uint32_t pri, TaskHandle_t *h) {
    (void)fn; (void)n; (void)st; (void)p; (void)pri; (void)h; return pdPASS;
}

#endif
