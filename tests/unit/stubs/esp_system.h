#ifndef STUBS_ESP_SYSTEM_H
#define STUBS_ESP_SYSTEM_H

#include <stdlib.h>
#include <stdint.h>

static inline void esp_fill_random(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(rand() & 0xFF);
    }
}

#endif
