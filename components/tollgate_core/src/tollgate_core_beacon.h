#ifndef TOLLGATE_CORE_BEACON_H
#define TOLLGATE_CORE_BEACON_H

#include <stdint.h>
#include <stdbool.h>

#define TOLLGATE_OUI_0       0xC0
#define TOLLGATE_OUI_1       0xFF
#define TOLLGATE_OUI_2       0xEE
#define TOLLGATE_IE_TYPE     0x01
#define TOLLGATE_IE_VERSION  1
#define TOLLGATE_IE_ELEMENT_ID 0xDD

#define TOLLGATE_IE_GEOHASH_MAX 9

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t metric;
    uint16_t price_per_step;
    uint32_t step_size;
    uint8_t mint_hash[4];
    uint8_t geohash_len;
    char geohash[TOLLGATE_IE_GEOHASH_MAX];
    uint8_t npub_hash[4];
} tollgate_price_payload_t;

#define TOLLGATE_IE_PAYLOAD_SIZE sizeof(tollgate_price_payload_t)
#define TOLLGATE_IE_TOTAL_SIZE   (6 + TOLLGATE_IE_PAYLOAD_SIZE)

typedef struct __attribute__((packed)) {
    uint8_t element_id;
    uint8_t length;
    uint8_t vendor_oui[3];
    uint8_t vendor_oui_type;
    tollgate_price_payload_t payload;
} tollgate_price_ie_t;

typedef struct {
    const char *mint_url;
    const char *metric;
    int price_per_step;
    int step_size_ms;
    int step_size_bytes;
    const char *geohash;
    const char *npub_hex;
    bool identity_initialized;
} tollgate_beacon_config_t;

void tollgate_core_beacon_hash_mint(const char *mint_url, uint8_t hash_out[4]);
void tollgate_core_beacon_hash_npub(const char *npub_hex, uint8_t hash_out[4]);
void tollgate_core_beacon_build_ie(const tollgate_beacon_config_t *cfg, tollgate_price_ie_t *ie);

#endif
