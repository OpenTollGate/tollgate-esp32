#include "beacon_price.h"
#include "tollgate_core_beacon.h"
#include "config.h"
#include "identity.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "beacon_price";
static bool s_active = false;

void beacon_price_hash_mint(const char *mint_url, uint8_t hash_out[4])
{
    tollgate_core_beacon_hash_mint(mint_url, hash_out);
}

void beacon_price_hash_npub(const char *npub_hex, uint8_t hash_out[4])
{
    tollgate_core_beacon_hash_npub(npub_hex, hash_out);
}

void beacon_price_build_ie(tollgate_price_ie_t *ie)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    const tollgate_identity_t *id = identity_get();

    tollgate_beacon_config_t bcfg = {
        .mint_url = cfg->mint_url,
        .metric = cfg->metric,
        .price_per_step = cfg->price_per_step,
        .step_size_ms = cfg->step_size_ms,
        .step_size_bytes = cfg->step_size_bytes,
        .geohash = cfg->nostr_geohash,
        .npub_hex = (id && id->initialized) ? id->npub_hex : NULL,
        .identity_initialized = (id && id->initialized),
    };
    tollgate_core_beacon_build_ie(&bcfg, ie);

    ESP_LOGI(TAG, "Built IE: price=%lu sats, step=%lu, metric=%s, geohash=%.*s",
             (unsigned long)ie->payload.price_per_step, (unsigned long)ie->payload.step_size,
             ie->payload.metric ? "bytes" : "milliseconds",
             ie->payload.geohash_len, ie->payload.geohash);
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
