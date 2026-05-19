#include "test_framework.h"
#include "../../main/beacon_price.h"
#include "../../main/market.h"
#include "../../main/config.h"
#include "../../main/identity.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static tollgate_config_t g_test_config;
static tollgate_identity_t g_test_identity;

const tollgate_config_t *tollgate_config_get(void) { return &g_test_config; }
const tollgate_identity_t *identity_get(void) { return &g_test_identity; }

static void build_test_ie(tollgate_price_ie_t *ie, uint16_t price, uint32_t step, uint8_t metric,
                           const char *geohash, const char *mint_url, const char *npub_hex)
{
    memset(ie, 0, sizeof(*ie));
    ie->element_id = 0xDD;
    ie->length = 4 + TOLLGATE_IE_PAYLOAD_SIZE;
    ie->vendor_oui[0] = TOLLGATE_OUI_0;
    ie->vendor_oui[1] = TOLLGATE_OUI_1;
    ie->vendor_oui[2] = TOLLGATE_OUI_2;
    ie->vendor_oui_type = TOLLGATE_IE_TYPE;

    ie->payload.version = TOLLGATE_IE_VERSION;
    ie->payload.metric = metric;
    ie->payload.price_per_step = price;
    ie->payload.step_size = step;

    if (mint_url) beacon_price_hash_mint(mint_url, ie->payload.mint_hash);
    if (npub_hex) beacon_price_hash_npub(npub_hex, ie->payload.npub_hash);

    uint8_t gh_len = (uint8_t)strnlen(geohash, TOLLGATE_IE_GEOHASH_MAX);
    ie->payload.geohash_len = gh_len;
    memcpy(ie->payload.geohash, geohash, gh_len);
}

static void reset_market(void)
{
    market_t *m = (market_t *)market_get();
    memset(m, 0, sizeof(*m));
}

int main(void)
{
    printf("=== test_market ===\n");

    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.market_enabled = true;
    g_test_config.market_scan_interval_s = 30;
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);

    memset(&g_test_identity, 0, sizeof(g_test_identity));
    strncpy(g_test_identity.npub_hex, "0000000000000000000000000000000000000000000000000000000000000001", 64);
    g_test_identity.initialized = true;

    printf("\n--- parse vendor IE (valid) ---\n");
    {
        reset_market();
        tollgate_price_ie_t ie;
        build_test_ie(&ie, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space",
                      "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");

        uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -45);

        const market_t *m = market_get();
        ASSERT_EQ_INT(1, m->count, "one entry added");
        ASSERT(m->entries[0].valid, "entry is valid");
        ASSERT_EQ_INT(21, m->entries[0].price_per_step, "price is 21");
        ASSERT_EQ_INT(60000, (int)m->entries[0].step_size, "step_size is 60000");
        ASSERT_EQ_INT(0, m->entries[0].metric, "metric is 0 (time)");
        ASSERT_EQ_INT(-45, m->entries[0].rssi, "rssi is -45");
        ASSERT(memcmp(m->entries[0].bssid, bssid, 6) == 0, "bssid matches");
    }

    printf("\n--- parse vendor IE (ignore self) ---\n");
    {
        reset_market();
        tollgate_price_ie_t ie;
        build_test_ie(&ie, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space",
                      g_test_identity.npub_hex);

        uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -50);

        const market_t *m = market_get();
        ASSERT_EQ_INT(0, m->count, "self-entry ignored");
    }

    printf("\n--- parse vendor IE (wrong OUI) ---\n");
    {
        reset_market();
        tollgate_price_ie_t ie;
        build_test_ie(&ie, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space",
                      "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
        ie.vendor_oui[0] = 0x00;

        uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -40);

        const market_t *m = market_get();
        ASSERT_EQ_INT(0, m->count, "wrong OUI rejected");
    }

    printf("\n--- market_find_cheapest ---\n");
    {
        reset_market();

        tollgate_price_ie_t ie1, ie2, ie3;
        build_test_ie(&ie1, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "aaa...npub1");
        build_test_ie(&ie2, 10, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "bbb...npub2");
        build_test_ie(&ie3, 50, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "ccc...npub3");

        uint8_t bssid1[6] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
        uint8_t bssid2[6] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
        uint8_t bssid3[6] = {0x03, 0x03, 0x03, 0x03, 0x03, 0x03};

        market_parse_vendor_ie(bssid1, (vendor_ie_data_t *)&ie1, -45);
        market_parse_vendor_ie(bssid2, (vendor_ie_data_t *)&ie2, -50);
        market_parse_vendor_ie(bssid3, (vendor_ie_data_t *)&ie3, -55);

        const market_t *m = market_get();
        ASSERT_EQ_INT(3, m->count, "three entries");

        strncpy((char *)m->entries[0].ssid, "TollGate-A", 32);
        strncpy((char *)m->entries[1].ssid, "TollGate-B", 32);
        strncpy((char *)m->entries[2].ssid, "TollGate-C", 32);

        int cheapest = market_find_cheapest();
        ASSERT(cheapest >= 0, "found a cheapest entry");
        ASSERT_EQ_INT(10, m->entries[cheapest].price_per_step, "cheapest is 10 sats");
    }

    printf("\n--- update existing entry ---\n");
    {
        reset_market();
        tollgate_price_ie_t ie;
        build_test_ie(&ie, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "npub1");
        uint8_t bssid[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -45);
        ASSERT_EQ_INT(1, market_get()->count, "first add");

        build_test_ie(&ie, 15, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "npub1");
        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -47);
        ASSERT_EQ_INT(1, market_get()->count, "update doesn't increase count");
        ASSERT_EQ_INT(15, market_get()->entries[0].price_per_step, "price updated to 15");
    }

    printf("\n--- geohash preserved ---\n");
    {
        reset_market();
        tollgate_price_ie_t ie;
        build_test_ie(&ie, 21, 60000, 0, "u281w0dfz",
                      "https://testnut.cashu.space", "npub1");
        uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

        market_parse_vendor_ie(bssid, (vendor_ie_data_t *)&ie, -40);

        const market_t *m = market_get();
        ASSERT(m->entries[0].valid, "entry valid");
        ASSERT_EQ_STR("u281w0dfz", m->entries[0].geohash, "geohash is u281w0dfz");
    }

    TEST_SUMMARY();
}
