#ifndef STUBS_FREERTOS_EVENT_GROUPS_H
#define STUBS_FREERTOS_EVENT_GROUPS_H

#include <stdint.h>

typedef void *EventGroupHandle_t;
#define BIT0 (1 << 0)

static inline EventGroupHandle_t xEventGroupCreate(void) { return (EventGroupHandle_t)1; }
static inline uint32_t xEventGroupSetBits(EventGroupHandle_t eg, uint32_t bits) { (void)eg; return bits; }
static inline uint32_t xEventGroupClearBits(EventGroupHandle_t eg, uint32_t bits) { (void)eg; return bits; }

#endif
