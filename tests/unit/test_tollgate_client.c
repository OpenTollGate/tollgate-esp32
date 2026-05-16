#include "test_framework.h"
#include "../../main/config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

static tollgate_config_t g_test_config;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

uint64_t nucula_wallet_balance(void) { return 100; }
esp_err_t nucula_wallet_send(uint64_t a, char *b, size_t c) { (void)a; (void)b; (void)c; return ESP_OK; }

#include "freertos/FreeRTOS.h"

#include "../../main/tollgate_client.c"

static void reset_state(void) {
    s_state = TG_CLIENT_IDLE;
    memset(&s_discovery, 0, sizeof(s_discovery));
    memset(s_gw_ip, 0, sizeof(s_gw_ip));
    s_allotment_ms = 0;
    s_remaining_ms = -1;
    s_last_pay_time_ms = 0;
    s_retry_count = 0;
}

int main(void)
{
    printf("=== test_tollgate_client ===\n");

    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.client_enabled = true;
    g_test_config.client_steps_to_buy = 1;
    g_test_config.client_renewal_threshold_pct = 20;
    g_test_config.client_retry_interval_ms = 30000;

    printf("\n--- parse_discovery_response (valid kind=10021) ---\n");
    {
        const char *json = "{\"kind\":10021,\"pubkey\":\"abcdef\",\"tags\":["
            "[\"metric\",\"milliseconds\"],"
            "[\"step_size\",\"60000\"],"
            "[\"price_per_step\",\"cashu\",\"21\",\"sat\",\"https://testnut.cashu.space\",\"1\"],"
            "[\"tips\",\"1\",\"2\",\"5\"]"
            "],\"content\":\"\"}";

        tollgate_discovery_t disc;
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(ok, "valid discovery parsed");
        ASSERT(disc.is_tollgate, "is_tollgate=true");
        ASSERT_EQ_INT(21, disc.price_per_step, "price_per_step=21");
        ASSERT_EQ_INT(60000, disc.step_size_ms, "step_size_ms=60000");
        ASSERT_EQ_STR("milliseconds", disc.metric, "metric=milliseconds");
        ASSERT_EQ_STR("https://testnut.cashu.space", disc.mint_url, "mint_url");
    }

    printf("\n--- parse_discovery_response (wrong kind) ---\n");
    {
        const char *json = "{\"kind\":1,\"tags\":[]}";
        tollgate_discovery_t disc;
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(!ok, "wrong kind rejected");
    }

    printf("\n--- parse_discovery_response (no tags) ---\n");
    {
        const char *json = "{\"kind\":10021,\"content\":\"\"}";
        tollgate_discovery_t disc = {0};
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(ok, "no tags still parses");
        ASSERT(disc.is_tollgate, "is_tollgate=true even without tags");
        ASSERT_EQ_INT(0, disc.price_per_step, "price=0 when no tags");
    }

    printf("\n--- parse_discovery_response (garbage) ---\n");
    {
        tollgate_discovery_t disc;
        bool ok = parse_discovery_response("not json", &disc);
        ASSERT(!ok, "garbage rejected");
    }

    printf("\n--- parse_session_response (valid kind=1022) ---\n");
    {
        const char *json = "{\"kind\":1022,\"pubkey\":\"abcdef\",\"tags\":["
            "[\"p\",\"unknown\"],"
            "[\"device-identifier\",\"mac\",\"10.0.0.2\"],"
            "[\"allotment\",\"60000\"],"
            "[\"metric\",\"milliseconds\"]"
            "],\"content\":\"\"}";

        int64_t allotment = 0;
        bool ok = parse_session_response(json, &allotment);
        ASSERT(ok, "valid session parsed");
        ASSERT_EQ_INT(60000, (int)allotment, "allotment=60000");
    }

    printf("\n--- parse_session_response (error kind=21023) ---\n");
    {
        const char *json = "{\"kind\":21023,\"tags\":[[\"level\",\"error\"],[\"code\",\"payment-error-token-spent\"]],\"content\":\"Token spent\"}";
        int64_t allotment = 999;
        bool ok = parse_session_response(json, &allotment);
        ASSERT(!ok, "error kind rejected");
        ASSERT_EQ_INT(999, (int)allotment, "allotment unchanged on error");
    }

    printf("\n--- parse_usage_response ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = parse_usage_response("30000/60000", &remaining, &total);
        ASSERT(ok, "valid usage parsed");
        ASSERT_EQ_INT(30000, (int)remaining, "remaining=30000");
        ASSERT_EQ_INT(60000, (int)total, "total=60000");
    }

    printf("\n--- parse_usage_response (-1/-1) ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = parse_usage_response("-1/-1", &remaining, &total);
        ASSERT(ok, "no-session usage parsed");
        ASSERT_EQ_INT(-1, (int)remaining, "remaining=-1");
        ASSERT_EQ_INT(-1, (int)total, "total=-1");
    }

    printf("\n--- parse_usage_response (garbage) ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = parse_usage_response("garbage", &remaining, &total);
        ASSERT(!ok, "no slash rejected");
    }

    printf("\n--- renewal threshold calculation ---\n");
    {
        reset_state();
        s_state = TG_CLIENT_PAID;
        s_allotment_ms = 60000;
        s_remaining_ms = 10000;
        strncpy(s_gw_ip, "10.0.0.1", sizeof(s_gw_ip) - 1);

        int remaining_pct = (int)((s_remaining_ms * 100) / s_allotment_ms);
        ASSERT(remaining_pct <= 20, "10/60 = 16% <= 20% triggers renewal");
    }

    printf("\n--- renewal threshold no-renew (above 20%%) ---\n");
    {
        reset_state();
        s_allotment_ms = 60000;
        s_remaining_ms = 50000;
        int remaining_pct = (int)((s_remaining_ms * 100) / s_allotment_ms);
        ASSERT(remaining_pct > 20, "50/60 = 83% > 20% no renewal");
    }

    printf("\n--- state machine: init ---\n");
    {
        reset_state();
        tollgate_client_init();
        ASSERT_EQ_INT(TG_CLIENT_IDLE, (int)tollgate_client_get_state(), "init sets IDLE");
    }

    printf("\n--- config: client_enabled=false ---\n");
    {
        reset_state();
        g_test_config.client_enabled = false;
        esp_err_t ret = tollgate_client_on_sta_connected("10.0.0.1");
        ASSERT_EQ_INT(ESP_OK, (int)ret, "returns OK when disabled");
        ASSERT_EQ_INT(TG_CLIENT_IDLE, (int)tollgate_client_get_state(), "stays IDLE when disabled");
        g_test_config.client_enabled = true;
    }

    printf("\n--- state machine: disconnect resets ---\n");
    {
        reset_state();
        s_state = TG_CLIENT_PAID;
        strncpy(s_gw_ip, "10.0.0.1", sizeof(s_gw_ip) - 1);
        s_allotment_ms = 60000;
        s_remaining_ms = 30000;
        tollgate_client_on_sta_disconnected();
        ASSERT_EQ_INT(TG_CLIENT_IDLE, (int)tollgate_client_get_state(), "disconnect resets to IDLE");
        ASSERT_EQ_INT(-1, (int)tollgate_client_get_remaining_ms(), "remaining reset to -1");
        ASSERT_EQ_INT(0, (int)tollgate_client_get_allotment_ms(), "allotment reset to 0");
    }

    TEST_SUMMARY();
}
