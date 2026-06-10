#include "tollgate_core_market.h"
#include <string.h>

static tollgate_market_t s_market;
static tollgate_market_get_time_ms_fn s_get_time_ms;

static int find_entry_by_bssid(const uint8_t bssid[6])
{
    for (int i = 0; i < TG_MARKET_MAX_ENTRIES; i++) {
        if (s_market.entries[i].valid && memcmp(s_market.entries[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < TG_MARKET_MAX_ENTRIES; i++) {
        if (!s_market.entries[i].valid) return i;
    }
    int oldest = 0;
    int64_t oldest_time = s_market.entries[0].discovered_ms;
    for (int i = 1; i < TG_MARKET_MAX_ENTRIES; i++) {
        if (s_market.entries[i].discovered_ms < oldest_time) {
            oldest_time = s_market.entries[i].discovered_ms;
            oldest = i;
        }
    }
    return oldest;
}

void tollgate_core_market_init(tollgate_market_get_time_ms_fn get_time_ms)
{
    memset(&s_market, 0, sizeof(s_market));
    s_get_time_ms = get_time_ms;
}

void tollgate_core_market_parse_ie(const uint8_t sa[6], const tollgate_vendor_ie_t *ie, int rssi,
                                    const uint8_t *self_npub_hash)
{
    if (!ie || ie->payload_len < TOLLGATE_IE_PAYLOAD_SIZE) return;

    static const uint8_t oui[3] = { TOLLGATE_OUI_0, TOLLGATE_OUI_1, TOLLGATE_OUI_2 };
    if (memcmp(ie->vendor_oui, oui, 3) != 0) return;
    if (ie->vendor_oui_type != TOLLGATE_IE_TYPE) return;

    const tollgate_price_payload_t *payload = (const tollgate_price_payload_t *)ie->payload;
    if (payload->version != TOLLGATE_IE_VERSION) return;

    if (self_npub_hash && memcmp(payload->npub_hash, self_npub_hash, 4) == 0) return;

    int idx = find_entry_by_bssid(sa);
    if (idx < 0) {
        idx = find_free_slot();
        if (s_market.count < TG_MARKET_MAX_ENTRIES) s_market.count++;
    }

    tollgate_market_entry_t *entry = &s_market.entries[idx];
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

    entry->discovered_ms = s_get_time_ms ? s_get_time_ms() : 0;
    entry->valid = true;
    entry->ssid[0] = '\0';
}

int tollgate_core_market_find_cheapest(const tollgate_market_t *market)
{
    int cheapest = -1;
    uint32_t best_eff_price = UINT32_MAX;

    for (int i = 0; i < TG_MARKET_MAX_ENTRIES; i++) {
        if (!market->entries[i].valid) continue;
        if (market->entries[i].ssid[0] == '\0') continue;

        uint32_t step = market->entries[i].step_size;
        if (step == 0) continue;

        uint32_t eff;
        if (market->entries[i].metric == 0) {
            eff = (uint32_t)market->entries[i].price_per_step * 60000 / step;
        } else {
            eff = (uint32_t)market->entries[i].price_per_step * 1048576 / step;
        }

        if (eff < best_eff_price) {
            best_eff_price = eff;
            cheapest = i;
        }
    }
    return cheapest;
}

const tollgate_market_t *tollgate_core_market_get(void)
{
    return &s_market;
}

void tollgate_core_market_update_ssid(const uint8_t bssid[6], const char *ssid, int8_t rssi)
{
    for (int i = 0; i < TG_MARKET_MAX_ENTRIES; i++) {
        if (!s_market.entries[i].valid) continue;
        if (memcmp(s_market.entries[i].bssid, bssid, 6) == 0) {
            memcpy(s_market.entries[i].ssid, ssid, 32);
            s_market.entries[i].ssid[32] = '\0';
            s_market.entries[i].rssi = rssi;
            break;
        }
    }
}
