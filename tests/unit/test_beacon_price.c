#include "test_framework.h"
#include "../../main/config.h"
#include "../../main/identity.h"
#include <string.h>
#include <stdio.h>
#include <mbedtls/sha256.h>

#include "../../main/beacon_price.h"

static tollgate_config_t g_test_config;
static tollgate_identity_t g_test_identity;

const tollgate_config_t *tollgate_config_get(void) { return &g_test_config; }
const tollgate_identity_t *identity_get(void) { return &g_test_identity; }

int main(void)
{
    printf("=== test_beacon_price ===\n");

    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.mint_url, "https://testnut.cashu.space", sizeof(g_test_config.mint_url) - 1);
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);
    g_test_config.price_per_step = 21;
    g_test_config.step_size_ms = 60000;
    strncpy(g_test_config.nostr_geohash, "u281w0dfz", sizeof(g_test_config.nostr_geohash) - 1);

    memset(&g_test_identity, 0, sizeof(g_test_identity));
    strncpy(g_test_identity.npub_hex, "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890", 64);
    g_test_identity.initialized = true;

    printf("\n--- tollgate_price_payload_t size ---\n");
    {
        ASSERT_EQ_INT(26, (int)TOLLGATE_IE_PAYLOAD_SIZE, "payload is 26 bytes");
        ASSERT_EQ_INT(32, (int)TOLLGATE_IE_TOTAL_SIZE, "total IE is 32 bytes");
    }

    printf("\n--- beacon_price_hash_mint ---\n");
    {
        uint8_t hash[4];
        beacon_price_hash_mint("https://testnut.cashu.space", hash);

        uint8_t expected[32];
        mbedtls_sha256((const unsigned char *)"https://testnut.cashu.space",
                       strlen("https://testnut.cashu.space"), expected, 0);
        ASSERT_MEM_EQ(expected, hash, 4, "mint_hash matches SHA-256 prefix");

        uint8_t hash2[4];
        beacon_price_hash_mint("https://other.mint.url", hash2);
        ASSERT(memcmp(hash, hash2, 4) != 0, "different mint URLs produce different hashes");
    }

    printf("\n--- beacon_price_hash_npub ---\n");
    {
        uint8_t hash[4];
        beacon_price_hash_npub("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890", hash);

        uint8_t expected[32];
        mbedtls_sha256((const unsigned char *)"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                       64, expected, 0);
        ASSERT_MEM_EQ(expected, hash, 4, "npub_hash matches SHA-256 prefix");
    }

    printf("\n--- beacon_price_build_ie (time metric) ---\n");
    {
        tollgate_price_ie_t ie;
        beacon_price_build_ie(&ie);

        ASSERT_EQ_INT(0xDD, ie.element_id, "element_id is 0xDD");
        ASSERT_EQ_INT(4 + 26, ie.length, "length is 30 (4 header + 26 payload)");
        ASSERT_EQ_INT(0xC0, ie.vendor_oui[0], "OUI byte 0");
        ASSERT_EQ_INT(0xFF, ie.vendor_oui[1], "OUI byte 1");
        ASSERT_EQ_INT(0xEE, ie.vendor_oui[2], "OUI byte 2");
        ASSERT_EQ_INT(0x01, ie.vendor_oui_type, "OUI type is 0x01");

        ASSERT_EQ_INT(1, ie.payload.version, "version is 1");
        ASSERT_EQ_INT(0, ie.payload.metric, "metric is 0 (milliseconds)");
        ASSERT_EQ_INT(21, ie.payload.price_per_step, "price is 21");
        ASSERT_EQ_INT(60000, (int)ie.payload.step_size, "step_size is 60000");

        uint8_t expected_mint_hash[4];
        beacon_price_hash_mint("https://testnut.cashu.space", expected_mint_hash);
        ASSERT_MEM_EQ(expected_mint_hash, ie.payload.mint_hash, 4, "mint_hash matches");

        ASSERT_EQ_INT(9, ie.payload.geohash_len, "geohash_len is 9");
        ASSERT(memcmp(ie.payload.geohash, "u281w0dfz", 9) == 0, "geohash matches");
    }

    printf("\n--- beacon_price_build_ie (bytes metric) ---\n");
    {
        strncpy(g_test_config.metric, "bytes", sizeof(g_test_config.metric) - 1);
        g_test_config.step_size_bytes = 22020096;
        g_test_config.price_per_step = 5;

        tollgate_price_ie_t ie;
        beacon_price_build_ie(&ie);

        ASSERT_EQ_INT(1, ie.payload.metric, "metric is 1 (bytes)");
        ASSERT_EQ_INT(5, ie.payload.price_per_step, "price is 5");
        ASSERT_EQ_INT(22020096, (int)ie.payload.step_size, "step_size is 22020096 bytes");

        strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);
        g_test_config.step_size_ms = 60000;
        g_test_config.price_per_step = 21;
    }

    printf("\n--- roundtrip: build → parse ---\n");
    {
        tollgate_price_ie_t ie;
        beacon_price_build_ie(&ie);

        vendor_ie_data_t *vnd_ie = (vendor_ie_data_t *)&ie;

        ASSERT(vnd_ie->length >= 4 + (int)TOLLGATE_IE_PAYLOAD_SIZE, "vendor IE length sufficient");

        const tollgate_price_payload_t *parsed = (const tollgate_price_payload_t *)vnd_ie->payload;
        ASSERT_EQ_INT(1, parsed->version, "parsed version");
        ASSERT_EQ_INT(0, parsed->metric, "parsed metric");
        ASSERT_EQ_INT(21, parsed->price_per_step, "parsed price");
        ASSERT_EQ_INT(60000, (int)parsed->step_size, "parsed step_size");
        ASSERT_EQ_INT(9, parsed->geohash_len, "parsed geohash_len");
    }

    printf("\n--- struct packing check ---\n");
    {
        tollgate_price_ie_t ie;
        memset(&ie, 0, sizeof(ie));
        int expected_size = 2 + 3 + 1 + 26;
        ASSERT_EQ_INT(expected_size, (int)sizeof(tollgate_price_ie_t), "no padding in struct");
    }

    TEST_SUMMARY();
}
