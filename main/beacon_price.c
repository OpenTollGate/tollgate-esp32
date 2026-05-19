#include "beacon_price.h"
#include "config.h"
#include "identity.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mbedtls/sha256.h"
#include <string.h>

static const char *TAG = "beacon_price";
static bool s_active = false;

void beacon_price_hash_mint(const char *mint_url, uint8_t hash_out[4])
{
    uint8_t full_hash[32];
    mbedtls_sha256((const unsigned char *)mint_url, strlen(mint_url), full_hash, 0);
    memcpy(hash_out, full_hash, 4);
}

void beacon_price_hash_npub(const char *npub_hex, uint8_t hash_out[4])
{
    uint8_t full_hash[32];
    mbedtls_sha256((const unsigned char *)npub_hex, strlen(npub_hex), full_hash, 0);
    memcpy(hash_out, full_hash, 4);
}

void beacon_price_build_ie(tollgate_price_ie_t *ie)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    const tollgate_identity_t *id = identity_get();

    memset(ie, 0, sizeof(*ie));
    ie->element_id = WIFI_VENDOR_IE_ELEMENT_ID;
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

    beacon_price_hash_mint(cfg->mint_url, p->mint_hash);

    p->geohash_len = (uint8_t)strnlen(cfg->nostr_geohash, TOLLGATE_IE_GEOHASH_MAX);
    memcpy(p->geohash, cfg->nostr_geohash, p->geohash_len);
    if (p->geohash_len < TOLLGATE_IE_GEOHASH_MAX) {
        memset(p->geohash + p->geohash_len, 0, TOLLGATE_IE_GEOHASH_MAX - p->geohash_len);
    }

    if (id && id->initialized) {
        beacon_price_hash_npub(id->npub_hex, p->npub_hash);
    }

    ESP_LOGI(TAG, "Built IE: price=%lu sats, step=%lu, metric=%s, geohash=%.*s",
             (unsigned long)p->price_per_step, (unsigned long)p->step_size,
             p->metric ? "bytes" : "milliseconds",
             p->geohash_len, p->geohash);
}

esp_err_t beacon_price_start(void)
{
    if (s_active) {
        ESP_LOGW(TAG, "Already active");
        return ESP_OK;
    }

    static tollgate_price_ie_t s_ie;
    beacon_price_build_ie(&s_ie);

    esp_err_t ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON,
                                            WIFI_VND_IE_ID_0, &s_ie);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set beacon vendor IE: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP,
                                  WIFI_VND_IE_ID_1, &s_ie);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set probe resp vendor IE: %s", esp_err_to_name(ret));
    }

    s_active = true;
    ESP_LOGI(TAG, "Price advertising started (beacon + probe response)");
    return ESP_OK;
}

esp_err_t beacon_price_stop(void)
{
    if (!s_active) return ESP_OK;

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, NULL);
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, NULL);

    s_active = false;
    ESP_LOGI(TAG, "Price advertising stopped");
    return ESP_OK;
}
