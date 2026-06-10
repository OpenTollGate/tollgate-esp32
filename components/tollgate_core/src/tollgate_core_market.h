#ifndef TOLLGATE_CORE_MARKET_H
#define TOLLGATE_CORE_MARKET_H

#include "tollgate_core_beacon.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TG_MARKET_MAX_ENTRIES 10

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
} tollgate_market_entry_t;

typedef struct {
    tollgate_market_entry_t entries[TG_MARKET_MAX_ENTRIES];
    int count;
    int64_t last_scan_ms;
    bool scanning;
    int consecutive_failures;
} tollgate_market_t;

typedef struct {
    const uint8_t *vendor_oui;
    uint8_t vendor_oui_type;
    const uint8_t *payload;
    size_t payload_len;
} tollgate_vendor_ie_t;

typedef int64_t (*tollgate_market_get_time_ms_fn)(void);

void tollgate_core_market_init(tollgate_market_get_time_ms_fn get_time_ms);
void tollgate_core_market_parse_ie(const uint8_t sa[6], const tollgate_vendor_ie_t *ie, int rssi,
                                    const uint8_t *self_npub_hash);
int tollgate_core_market_find_cheapest(const tollgate_market_t *market);
const tollgate_market_t *tollgate_core_market_get(void);
void tollgate_core_market_update_ssid(const uint8_t bssid[6], const char *ssid, int8_t rssi);

#endif
