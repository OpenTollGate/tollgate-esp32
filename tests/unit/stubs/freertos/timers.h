#ifndef STUBS_FREERTOS_TIMERS_H
#define STUBS_FREERTOS_TIMERS_H

#include <stdint.h>

typedef void *TimerHandle_t;

static inline TimerHandle_t xTimerCreate(const char *n, uint32_t pd, int ux, void *id, void *cb) {
    (void)n; (void)pd; (void)ux; (void)id; (void)cb; return (TimerHandle_t)1;
}
static inline int xTimerStart(TimerHandle_t t, uint32_t blk) { (void)t; (void)blk; return 1; }
static inline int xTimerStop(TimerHandle_t t, uint32_t blk) { (void)t; (void)blk; return 1; }
static inline void xTimerDelete(TimerHandle_t t, uint32_t blk) { (void)t; (void)blk; }

#endif
