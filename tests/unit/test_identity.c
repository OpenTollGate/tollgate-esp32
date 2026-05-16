#include "test_framework.h"
#include "../../main/identity.h"
#include <string.h>
#include <stdio.h>

static const char *TEST_NSEC = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";
static const char *TEST_NSEC2 = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

int main(void)
{
    printf("=== test_identity ===\n");

    printf("\n--- identity_init with valid nsec ---\n");
    esp_err_t ret = identity_init(TEST_NSEC);
    ASSERT_EQ_INT(ESP_OK, ret, "identity_init returns ESP_OK");

    const tollgate_identity_t *id = identity_get();
    ASSERT(id != NULL, "identity_get returns non-NULL");
    ASSERT(id->initialized, "identity is marked initialized");

    printf("\n--- npub derivation ---\n");
    ASSERT_EQ_INT(64, (int)strlen(id->npub_hex), "npub is 64 hex chars");
    ASSERT(id->npub_hex[0] != '\0', "npub is not empty");

    printf("\n--- STA MAC derivation ---\n");
    uint8_t expected_sta[] = {0xF2, 0x4D, 0x55, 0x33, 0xDC, 0x9C};
    ASSERT_MEM_EQ(expected_sta, id->sta_mac, 6, "STA MAC matches golden vector");
    ASSERT_EQ_INT(2, id->sta_mac[0] & 0x02, "STA MAC has locally-administered bit set");
    ASSERT_EQ_INT(0, id->sta_mac[0] & 0x01, "STA MAC has multicast bit cleared");

    printf("\n--- AP MAC derivation ---\n");
    uint8_t expected_ap[] = {0x3A, 0x2A, 0xEB, 0xC0, 0xE9, 0xCA};
    ASSERT_MEM_EQ(expected_ap, id->ap_mac, 6, "AP MAC matches golden vector");
    ASSERT_EQ_INT(2, id->ap_mac[0] & 0x02, "AP MAC has locally-administered bit set");
    ASSERT_EQ_INT(0, id->ap_mac[0] & 0x01, "AP MAC has multicast bit cleared");

    printf("\n--- SSID derivation ---\n");
    ASSERT_EQ_STR("TollGate-C0E9CA", id->ap_ssid, "SSID derived from AP MAC last 3 bytes");

    printf("\n--- AP IP derivation ---\n");
    ASSERT_EQ_STR("10.192.45.1", id->ap_ip_str, "AP IP derived from AP MAC bytes");

    printf("\n--- Determinism ---\n");
    ret = identity_init(TEST_NSEC);
    ASSERT_EQ_INT(ESP_OK, ret, "Second init with same nsec succeeds");
    const tollgate_identity_t *id2 = identity_get();
    ASSERT_MEM_EQ(id->sta_mac, id2->sta_mac, 6, "STA MAC is deterministic");
    ASSERT_MEM_EQ(id->ap_mac, id2->ap_mac, 6, "AP MAC is deterministic");
    ASSERT_EQ_STR(id->ap_ssid, id2->ap_ssid, "SSID is deterministic");

    printf("\n--- Different nsec produces different identity ---\n");
    ret = identity_init(TEST_NSEC2);
    ASSERT_EQ_INT(ESP_OK, ret, "Init with different nsec succeeds");
    const tollgate_identity_t *id3 = identity_get();
    ASSERT(memcmp(id->sta_mac, id3->sta_mac, 6) != 0, "Different nsec produces different STA MAC");
    ASSERT(memcmp(id->ap_mac, id3->ap_mac, 6) != 0, "Different nsec produces different AP MAC");
    ASSERT(strcmp(id->ap_ssid, id3->ap_ssid) != 0, "Different nsec produces different SSID");

    printf("\n--- Invalid nsec ---\n");
    ret = identity_init(NULL);
    ASSERT(ret != ESP_OK, "NULL nsec returns error");
    ret = identity_init("tooshort");
    ASSERT(ret != ESP_OK, "Short nsec returns error");
    ret = identity_init("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    ASSERT(ret != ESP_OK, "Invalid hex nsec returns error");

    TEST_SUMMARY();
}
