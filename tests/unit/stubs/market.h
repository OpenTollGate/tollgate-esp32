#ifndef STUBS_MARKET_H
#define STUBS_MARKET_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t bssid[6];
    int8_t rssi;
    uint16_t price_per_step;
    uint32_t step_size;
    uint8_t metric;
    uint8_t mint_hash[4];
    uint8_t npub_hash[4];
    int64_t discovered_ms;
} market_t;

static inline const market_t *market_get(void) { return NULL; }
static inline int market_find_cheapest(void) { return -1; }

#endif
