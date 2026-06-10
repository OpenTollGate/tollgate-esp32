#include "market.h"
#include "tollgate_core_market.h"
#include "tollgate_core_beacon.h"
#include "config.h"
#include "identity.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "market";
static bool s_initialized = false;
static esp_event_handler_instance_t s_scan_done_handler = NULL;

static int64_t get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void vendor_ie_cb(void *ctx, wifi_vendor_ie_type_t type,
                          const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi)
{
    (void)ctx;
    (void)type;
    if (!vnd_ie) return;

    tollgate_vendor_ie_t ie = {
        .vendor_oui = vnd_ie->vendor_oui,
        .vendor_oui_type = vnd_ie->vendor_oui_type,
        .payload = vnd_ie->payload,
        .payload_len = vnd_ie->length - 4,
    };

    const tollgate_identity_t *id = identity_get();
    uint8_t self_hash[4] = {0};
    const uint8_t *self_ptr = NULL;
    if (id && id->initialized) {
        tollgate_core_beacon_hash_npub(id->npub_hex, self_hash);
        self_ptr = self_hash;
    }

    tollgate_core_market_parse_ie(sa, &ie, rssi, self_ptr);

    ESP_LOGI(TAG, "Discovered TollGate %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
             sa[0], sa[1], sa[2], sa[3], sa[4], sa[5], rssi);
}

static void scan_done_cb(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg; (void)event_base; (void)event_id; (void)event_data;

    tollgate_market_t *m = (tollgate_market_t *)tollgate_core_market_get();
    m->scanning = false;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return;

    uint16_t max_aps = ap_count > 20 ? 20 : ap_count;
    wifi_ap_record_t *ap_records = malloc(max_aps * sizeof(wifi_ap_record_t));
    if (!ap_records) return;

    esp_wifi_scan_get_ap_records(&max_aps, ap_records);

    for (int i = 0; i < max_aps; i++) {
        tollgate_core_market_update_ssid(ap_records[i].bssid, (const char *)ap_records[i].ssid, ap_records[i].rssi);
    }
    free(ap_records);
    m->last_scan_ms = get_time_ms();

    ESP_LOGI(TAG, "Scan complete: %d APs, %d TollGates found", max_aps, m->count);
}

esp_err_t market_init(void)
{
    tollgate_core_market_init(get_time_ms);

    esp_err_t ret = esp_wifi_set_vendor_ie_cb(vendor_ie_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register vendor IE callback: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!s_scan_done_handler) {
        ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                                   scan_done_cb, NULL, &s_scan_done_handler);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register scan done handler: %s", esp_err_to_name(ret));
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Market scanner initialized");
    return ESP_OK;
}

void market_tick(void)
{
    if (!s_initialized) return;

    const tollgate_config_t *cfg = tollgate_config_get();
    if (!cfg->market_enabled) return;

    tollgate_market_t *m = (tollgate_market_t *)tollgate_core_market_get();
    if (m->scanning) return;

    int64_t now = get_time_ms();
    int64_t elapsed = now - m->last_scan_ms;
    int64_t interval_ms = (int64_t)cfg->market_scan_interval_s * 1000;
    if (elapsed < interval_ms) return;

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 120,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret == ESP_OK) {
        m->scanning = true;
        m->last_scan_ms = now;
        m->consecutive_failures = 0;
    } else {
        m->consecutive_failures++;
        m->last_scan_ms = now;
        if (m->consecutive_failures <= 3 || m->consecutive_failures % 30 == 0) {
            ESP_LOGW(TAG, "Scan start failed: %s (failures: %d)", esp_err_to_name(ret), m->consecutive_failures);
        }
    }
}

const market_t *market_get(void)
{
    return (const market_t *)tollgate_core_market_get();
}

int market_find_cheapest(void)
{
    return tollgate_core_market_find_cheapest(tollgate_core_market_get());
}

void market_parse_vendor_ie(const uint8_t sa[6], const vendor_ie_data_t *ie, int rssi)
{
    if (!ie || ie->length < 4 + (int)TOLLGATE_IE_PAYLOAD_SIZE) return;

    tollgate_vendor_ie_t vnd = {
        .vendor_oui = ie->vendor_oui,
        .vendor_oui_type = ie->vendor_oui_type,
        .payload = ie->payload,
        .payload_len = ie->length - 4,
    };

    const tollgate_identity_t *id = identity_get();
    uint8_t self_hash[4] = {0};
    const uint8_t *self_ptr = NULL;
    if (id && id->initialized) {
        tollgate_core_beacon_hash_npub(id->npub_hex, self_hash);
        self_ptr = self_hash;
    }

    tollgate_core_market_parse_ie(sa, &vnd, rssi, self_ptr);
}
