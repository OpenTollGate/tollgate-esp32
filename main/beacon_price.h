#ifndef BEACON_PRICE_H
#define BEACON_PRICE_H

#include "tollgate_core_beacon.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t beacon_price_start(void);
esp_err_t beacon_price_stop(void);
void beacon_price_build_ie(tollgate_price_ie_t *ie);
void beacon_price_hash_mint(const char *mint_url, uint8_t hash_out[4]);
void beacon_price_hash_npub(const char *npub_hex, uint8_t hash_out[4]);

#endif
