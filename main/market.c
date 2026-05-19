#include "market.h"
#include "beacon_price.h"
#include "config.h"
#include "identity.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "market";
static market_t s_market;
static bool s_initialized = false;

static int64_t get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static bool oui_matches(const uint8_t oui[3])
{
    return oui[0] == TOLLGATE_OUI_0 && oui[1] == TOLLGATE_OUI_1 && oui[2] == TOLLGATE_OUI_2;
}

static int find_entry_by_bssid(const uint8_t bssid[6])
{
    for (int i = 0; i < MARKET_MAX_ENTRIES; i++) {
        if (s_market.entries[i].valid && memcmp(s_market.entries[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MARKET_MAX_ENTRIES; i++) {
        if (!s_market.entries[i].valid) return i;
    }
    int oldest = 0;
    int64_t oldest_time = s_market.entries[0].discovered_ms;
    for (int i = 1; i < MARKET_MAX_ENTRIES; i++) {
        if (s_market.entries[i].discovered_ms < oldest_time) {
            oldest_time = s_market.entries[i].discovered_ms;
            oldest = i;
        }
    }
    return oldest;
}

void market_parse_vendor_ie(const uint8_t sa[6], const vendor_ie_data_t *ie, int rssi)
{
    if (!ie || ie->length < 4 + TOLLGATE_IE_PAYLOAD_SIZE) return;
    if (!oui_matches(ie->vendor_oui)) return;
    if (ie->vendor_oui_type != TOLLGATE_IE_TYPE) return;

    const tollgate_price_payload_t *payload = (const tollgate_price_payload_t *)ie->payload;
    if (payload->version != TOLLGATE_IE_VERSION) return;

    const tollgate_identity_t *id = identity_get();
    if (id && id->initialized) {
        uint8_t my_npub_hash[4];
        beacon_price_hash_npub(id->npub_hex, my_npub_hash);
        if (memcmp(payload->npub_hash, my_npub_hash, 4) == 0) return;
    }

    int idx = find_entry_by_bssid(sa);
    if (idx < 0) {
        idx = find_free_slot();
        if (s_market.count < MARKET_MAX_ENTRIES) s_market.count++;
    }

    market_entry_t *entry = &s_market.entries[idx];
    memcpy(entry->bssid, sa, 6);
    entry->rssi = (int8_t)rssi;
    entry->price_per_step = payload->price_per_step;
    entry->step_size = payload->step_size;
    entry->metric = payload->metric;
    memcpy(entry->mint_hash, payload->mint_hash, 4);
    memcpy(entry->npub_hash, payload->npub_hash, 4);

    uint8_t gh_len = payload->geohash_len;
    if (gh_len > TOLLGATE_IE_GEOHASH_MAX) gh_len = TOLLGATE_IE_GEOHASH_MAX;
    memcpy(entry->geohash, payload->geohash, gh_len);
    entry->geohash[gh_len] = '\0';

    entry->discovered_ms = get_time_ms();
    entry->valid = true;
    entry->ssid[0] = '\0';

    ESP_LOGI(TAG, "Discovered TollGate %02X:%02X:%02X:%02X:%02X:%02X price=%lu sats step=%lu metric=%s RSSI=%d",
             sa[0], sa[1], sa[2], sa[3], sa[4], sa[5],
             (unsigned long)payload->price_per_step, (unsigned long)payload->step_size,
             payload->metric ? "bytes" : "milliseconds", rssi);
}

static void vendor_ie_cb(void *ctx, wifi_vendor_ie_type_t type,
                          const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi)
{
    (void)ctx;
    (void)type;
    if (!vnd_ie) return;
    market_parse_vendor_ie(sa, vnd_ie, rssi);
}

static void scan_done_cb(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    s_market.scanning = false;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return;

    uint16_t max_aps = ap_count > 20 ? 20 : ap_count;
    wifi_ap_record_t *ap_records = malloc(max_aps * sizeof(wifi_ap_record_t));
    if (!ap_records) return;

    esp_wifi_scan_get_ap_records(&max_aps, ap_records);

    for (int i = 0; i < max_aps; i++) {
        for (int j = 0; j < MARKET_MAX_ENTRIES; j++) {
            if (!s_market.entries[j].valid) continue;
            if (memcmp(s_market.entries[j].bssid, ap_records[i].bssid, 6) == 0) {
                memcpy(s_market.entries[j].ssid, ap_records[i].ssid, 32);
                s_market.entries[j].ssid[32] = '\0';
                s_market.entries[j].rssi = ap_records[i].rssi;
                break;
            }
        }
    }
    free(ap_records);
    s_market.last_scan_ms = get_time_ms();

    ESP_LOGI(TAG, "Scan complete: %d APs, %d TollGates found", max_aps, s_market.count);
}

static esp_event_handler_instance_t s_scan_done_handler = NULL;

esp_err_t market_init(void)
{
    memset(&s_market, 0, sizeof(s_market));

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

    if (s_market.scanning) return;

    int64_t now = get_time_ms();
    int64_t elapsed = now - s_market.last_scan_ms;
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
        s_market.scanning = true;
        s_market.last_scan_ms = now;
        s_market.consecutive_failures = 0;
        ESP_LOGD(TAG, "Market scan started");
    } else {
        s_market.consecutive_failures++;
        s_market.last_scan_ms = now;
        if (s_market.consecutive_failures <= 3 || s_market.consecutive_failures % 30 == 0) {
            ESP_LOGW(TAG, "Scan start failed: %s (failures: %d)", esp_err_to_name(ret), s_market.consecutive_failures);
        }
    }
}

const market_t *market_get(void)
{
    return &s_market;
}

int market_find_cheapest(void)
{
    int cheapest = -1;
    uint32_t best_eff_price = UINT32_MAX;

    for (int i = 0; i < MARKET_MAX_ENTRIES; i++) {
        if (!s_market.entries[i].valid) continue;
        if (s_market.entries[i].ssid[0] == '\0') continue;

        uint32_t step = s_market.entries[i].step_size;
        if (step == 0) continue;

        uint32_t eff;
        if (s_market.entries[i].metric == 0) {
            eff = (uint32_t)s_market.entries[i].price_per_step * 60000 / step;
        } else {
            uint32_t eff_mb = (uint32_t)s_market.entries[i].price_per_step * 1048576 / step;
            eff = eff_mb;
        }

        if (eff < best_eff_price) {
            best_eff_price = eff;
            cheapest = i;
        }
    }
    return cheapest;
}
