#include "tollgate_core_beacon.h"
#include "mbedtls/sha256.h"
#include <string.h>

void tollgate_core_beacon_hash_mint(const char *mint_url, uint8_t hash_out[4])
{
    uint8_t full_hash[32];
    mbedtls_sha256((const unsigned char *)mint_url, strlen(mint_url), full_hash, 0);
    memcpy(hash_out, full_hash, 4);
}

void tollgate_core_beacon_hash_npub(const char *npub_hex, uint8_t hash_out[4])
{
    uint8_t full_hash[32];
    mbedtls_sha256((const unsigned char *)npub_hex, strlen(npub_hex), full_hash, 0);
    memcpy(hash_out, full_hash, 4);
}

void tollgate_core_beacon_build_ie(const tollgate_beacon_config_t *cfg, tollgate_price_ie_t *ie)
{
    memset(ie, 0, sizeof(*ie));
    ie->element_id = TOLLGATE_IE_ELEMENT_ID;
    ie->length = 4 + TOLLGATE_IE_PAYLOAD_SIZE;
    ie->vendor_oui[0] = TOLLGATE_OUI_0;
    ie->vendor_oui[1] = TOLLGATE_OUI_1;
    ie->vendor_oui[2] = TOLLGATE_OUI_2;
    ie->vendor_oui_type = TOLLGATE_IE_TYPE;

    tollgate_price_payload_t *p = &ie->payload;
    p->version = TOLLGATE_IE_VERSION;
    p->metric = (strcmp(cfg->metric, "bytes") == 0) ? 1 : 0;
    p->price_per_step = (uint16_t)cfg->price_per_step;

    bool is_bytes = (strcmp(cfg->metric, "bytes") == 0);
    p->step_size = is_bytes ? (uint32_t)cfg->step_size_bytes : (uint32_t)cfg->step_size_ms;

    tollgate_core_beacon_hash_mint(cfg->mint_url, p->mint_hash);

    p->geohash_len = (uint8_t)strnlen(cfg->geohash, TOLLGATE_IE_GEOHASH_MAX);
    memcpy(p->geohash, cfg->geohash, p->geohash_len);
    if (p->geohash_len < TOLLGATE_IE_GEOHASH_MAX) {
        memset(p->geohash + p->geohash_len, 0, TOLLGATE_IE_GEOHASH_MAX - p->geohash_len);
    }

    if (cfg->identity_initialized && cfg->npub_hex) {
        tollgate_core_beacon_hash_npub(cfg->npub_hex, p->npub_hash);
    }
}
