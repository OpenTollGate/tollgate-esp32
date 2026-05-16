#ifndef STUBS_FREERTOS_FREERTOS_H
#define STUBS_FREERTOS_FREERTOS_H

#include <stdint.h>

static inline uint32_t xTaskGetTickCount(void) { return 0; }
static inline void vTaskDelay(uint32_t ticks) { (void)ticks; }
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#define portTICK_PERIOD_MS 10
#define portMAX_DELAY 0xFFFFFFFF

#endif
