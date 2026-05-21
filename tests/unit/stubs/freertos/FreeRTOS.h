#ifndef STUBS_FREERTOS_FREERTOS_H
#define STUBS_FREERTOS_FREERTOS_H

#include <stdint.h>

typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

static inline uint32_t xTaskGetTickCount(void) { return 0; }
static inline void vTaskDelay(uint32_t ticks) { (void)ticks; }
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#define portTICK_PERIOD_MS 10
#define portMAX_DELAY 0xFFFFFFFF
#define pdTRUE 1
#define pdFALSE 0

#endif
