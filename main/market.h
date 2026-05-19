#ifndef MARKET_H
#define MARKET_H

#include "beacon_price.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define MARKET_MAX_ENTRIES 10

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    uint16_t price_per_step;
    uint32_t step_size;
    uint8_t metric;
    uint8_t mint_hash[4];
    uint8_t npub_hash[4];
    char geohash[TOLLGATE_IE_GEOHASH_MAX + 1];
    int64_t discovered_ms;
    bool valid;
} market_entry_t;

typedef struct {
    market_entry_t entries[MARKET_MAX_ENTRIES];
    int count;
    int64_t last_scan_ms;
    bool scanning;
    int consecutive_failures;
} market_t;

esp_err_t market_init(void);
void market_tick(void);
const market_t *market_get(void);
int market_find_cheapest(void);
void market_parse_vendor_ie(const uint8_t sa[6], const vendor_ie_data_t *ie, int rssi);

#endif
