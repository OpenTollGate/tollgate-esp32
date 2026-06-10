#ifndef STUB_ESP_HEAP_CAPS_H
#define STUB_ESP_HEAP_CAPS_H

#include <stdlib.h>

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) {
    (void)caps;
    return calloc(n, size);
}

static inline void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps) {
    (void)caps;
    return realloc(ptr, size);
}

#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_DMA 2
#define MALLOC_CAP_SPIRAM 3

#endif
