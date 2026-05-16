#include "test_framework.h"
#include "../../main/cashu.h"
#include "../../main/config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static tollgate_config_t g_test_config;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

int main(void)
{
    printf("=== test_cashu ===\n");

    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.mint_url, "https://testnut.cashu.space", sizeof(g_test_config.mint_url) - 1);
    g_test_config.price_per_step = 21;
    g_test_config.step_size_ms = 60000;

    printf("\n--- cashu_calculate_allotment_ms ---\n");
    uint64_t a1 = cashu_calculate_allotment_ms(21, 21, 60000);
    ASSERT_EQ_INT(60000, (int)a1, "21 sats at 21 sats/min = 60000ms");

    uint64_t a2 = cashu_calculate_allotment_ms(42, 21, 60000);
    ASSERT_EQ_INT(120000, (int)a2, "42 sats at 21 sats/min = 120000ms");

    uint64_t a3 = cashu_calculate_allotment_ms(1, 21, 60000);
    ASSERT_EQ_INT(0, (int)a3, "1 sat at 21 sats/min = 0ms (rounds down)");

    uint64_t a4 = cashu_calculate_allotment_ms(100, 10, 30000);
    ASSERT_EQ_INT(300000, (int)a4, "100 sats at 10 sats/30s = 300000ms");

    printf("\n--- cashu_is_mint_accepted ---\n");
    ASSERT(cashu_is_mint_accepted("https://testnut.cashu.space"), "testnut.cashu.space accepted");
    ASSERT(!cashu_is_mint_accepted("https://evil.mint.example.com"), "evil mint rejected");
    ASSERT(!cashu_is_mint_accepted(""), "empty string rejected");

    printf("\n--- cashu_decode_token with garbage ---\n");
    cashu_token_t token;
    memset(&token, 0, sizeof(token));
    esp_err_t ret = cashu_decode_token("garbage", &token);
    ASSERT(ret != ESP_OK, "Garbage input returns error");

    ret = cashu_decode_token("", &token);
    ASSERT(ret != ESP_OK, "Empty string returns error");

    ret = cashu_decode_token("cashuA!!invalid-base64!!", &token);
    ASSERT(ret != ESP_OK, "Invalid base64url returns error");

    TEST_SUMMARY();
}
