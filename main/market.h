#ifndef MARKET_H
#define MARKET_H

#include "tollgate_core_beacon.h"
#include "tollgate_core_market.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define MARKET_MAX_ENTRIES TG_MARKET_MAX_ENTRIES

typedef tollgate_market_entry_t market_entry_t;
typedef tollgate_market_t market_t;

esp_err_t market_init(void);
void market_tick(void);
const market_t *market_get(void);
int market_find_cheapest(void);
void market_parse_vendor_ie(const uint8_t sa[6], const vendor_ie_data_t *ie, int rssi);

#endif
