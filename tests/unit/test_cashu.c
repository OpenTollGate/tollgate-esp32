#include "test_framework.h"
#include "tollgate_core_cashu.h"
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

    const char *mints[] = {
        "https://testnut.cashu.space",
        "https://mint.minibits.cash/Bitcoin",
        "https://mint.coinos.io",
        "https://21mint.me",
    };
    for (int i = 0; i < 4; i++) {
        strncpy(g_test_config.accepted_mints[i], mints[i],
                sizeof(g_test_config.accepted_mints[i]) - 1);
    }
    g_test_config.accepted_mint_count = 4;

    printf("\n--- tollgate_core_cashu_calculate_allotment ---\n");
    uint64_t a1 = tollgate_core_cashu_calculate_allotment(21, 21, 60000);
    ASSERT_EQ_INT(60000, (int)a1, "21 sats at 21 sats/min = 60000ms");

    uint64_t a2 = tollgate_core_cashu_calculate_allotment(42, 21, 60000);
    ASSERT_EQ_INT(120000, (int)a2, "42 sats at 21 sats/min = 120000ms");

    uint64_t a3 = tollgate_core_cashu_calculate_allotment(1, 21, 60000);
    ASSERT_EQ_INT(0, (int)a3, "1 sat at 21 sats/min = 0ms (rounds down)");

    uint64_t a4 = tollgate_core_cashu_calculate_allotment(100, 10, 30000);
    ASSERT_EQ_INT(300000, (int)a4, "100 sats at 10 sats/30s = 300000ms");

    printf("\n--- tollgate_core_cashu_is_mint_accepted (multi-mint) ---\n");
    ASSERT(tollgate_core_cashu_is_mint_accepted("https://testnut.cashu.space", "https://testnut.cashu.space"), "testnut.cashu.space accepted");
    ASSERT(tollgate_core_cashu_is_mint_accepted("https://mint.minibits.cash/Bitcoin", "https://mint.minibits.cash/Bitcoin"), "minibits accepted");
    ASSERT(tollgate_core_cashu_is_mint_accepted("https://mint.coinos.io", "https://mint.coinos.io"), "coinos accepted");
    ASSERT(tollgate_core_cashu_is_mint_accepted("https://21mint.me", "https://21mint.me"), "21mint accepted");
    ASSERT(!tollgate_core_cashu_is_mint_accepted("https://evil.mint.example.com", "https://testnut.cashu.space"), "evil mint rejected");
    ASSERT(!tollgate_core_cashu_is_mint_accepted("", "https://testnut.cashu.space"), "empty string rejected");
    ASSERT(!tollgate_core_cashu_is_mint_accepted(NULL, "https://testnut.cashu.space"), "NULL rejected");

    printf("\n--- tollgate_core_cashu_decode_token with garbage ---\n");
    tg_cashu_token_t token;
    memset(&token, 0, sizeof(token));
    esp_err_t ret = tollgate_core_cashu_decode_token("garbage", &token);
    ASSERT(ret != ESP_OK, "Garbage input returns error");

    ret = tollgate_core_cashu_decode_token("", &token);
    ASSERT(ret != ESP_OK, "Empty string returns error");

    ret = tollgate_core_cashu_decode_token("cashuA!!invalid-base64!!", &token);
    ASSERT(ret != ESP_OK, "Invalid base64url returns error");

    TEST_SUMMARY();
}
