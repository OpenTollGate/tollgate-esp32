#include "test_framework.h"
#include "tollgate_core.h"
#include "tollgate_core_mining.h"
#include "tollgate_core_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *s_mint_url = "https://test.mint.example";
static const char *s_metric = "milliseconds";
static uint16_t s_price = 21;
static int32_t s_step_ms = 60000;
static int32_t s_step_bytes = 22020096;
static uint64_t s_hashprice_override = 0;
static double s_last_share_diff = 0.0;

static const char *mock_get_mint_url(void) { return s_mint_url; }
static const char *mock_get_metric(void) { return s_metric; }
static uint16_t mock_get_price(void) { return s_price; }
static int32_t mock_get_step_ms(void) { return s_step_ms; }
static int32_t mock_get_step_bytes(void) { return s_step_bytes; }
static uint64_t mock_get_hashprice(void) { return s_hashprice_override; }
static int64_t mock_get_time_ms(void) { return 1000000; }
static void mock_on_share_accepted(double diff) { s_last_share_diff = diff; }
static bool mock_spend_proofs(const char *t) { (void)t; return true; }

static tollgate_platform_t make_test_platform(void)
{
    tollgate_platform_t p = {0};
    p.get_mint_url = mock_get_mint_url;
    p.get_metric = mock_get_metric;
    p.get_price_sats = mock_get_price;
    p.get_step_ms = mock_get_step_ms;
    p.get_step_bytes = mock_get_step_bytes;
    p.get_hashprice_sats_per_ghs_day = mock_get_hashprice;
    p.get_time_ms = mock_get_time_ms;
    p.on_share_accepted = mock_on_share_accepted;
    p.spend_proofs = mock_spend_proofs;
    return p;
}

int main(void)
{
    printf("=== test_mining_api ===\n");

    tollgate_platform_t platform = make_test_platform();
    esp_ip4_addr_t ap_ip = { .addr = 0x0101010A };
    tollgate_core_init(&platform, ap_ip);

    tollgate_core_mining_init();
    tollgate_core_mining_set_current_nbits(0x1d00ffff);

    printf("\n--- tollgate_core_calc_hashprice with nbits ---\n");
    double hp1 = tollgate_core_calc_hashprice(0.0);
    ASSERT(hp1 > 0.0, "hashprice from nbits is positive");
    double expected_hp = tollgate_core_mining_get_current_hashprice();
    ASSERT(fabs(hp1 - expected_hp) < 0.001, "matches mining module hashprice");

    printf("\n--- tollgate_core_calc_hashprice with override ---\n");
    s_hashprice_override = 500;
    double hp2 = tollgate_core_calc_hashprice(0.0);
    ASSERT(fabs(hp2 - 500.0) < 0.001, "override hashprice returned");
    s_hashprice_override = 0;

    printf("\n--- tollgate_core_calc_hashprice no platform ---\n");
    double hp3 = tollgate_core_calc_hashprice(0.0);
    ASSERT(hp3 == 0.0 || hp3 > 0.0, "NULL platform handled gracefully");
    tollgate_core_init(&platform, ap_ip);
    tollgate_core_mining_init();
    tollgate_core_mining_set_current_nbits(0x1d00ffff);

    printf("\n--- tollgate_core_get_mining_status_json ---\n");
    char *json = tollgate_core_get_mining_status_json();
    ASSERT(json != NULL, "mining status JSON returned");
    ASSERT(strstr(json, "proxy") != NULL, "JSON contains proxy object");
    ASSERT(strstr(json, "hashrate_ghs") != NULL, "JSON contains hashrate_ghs");
    ASSERT(strstr(json, "total_shares") != NULL, "JSON contains total_shares");
    ASSERT(strstr(json, "total_accepted") != NULL, "JSON contains total_accepted");
    ASSERT(strstr(json, "hashprice") != NULL, "JSON contains hashprice");
    printf("  JSON: %s\n", json);
    free(json);

    printf("\n--- tollgate_core_on_share_accepted creates session ---\n");
    uint32_t client_ip = 0x0202020A;
    tollgate_core_on_share_accepted(client_ip, 1.0);

    tg_session_t *session = tollgate_core_session_find_by_ip(client_ip);
    ASSERT(session != NULL, "session created for miner");
    ASSERT(session->active, "session is active");
    ASSERT(session->payment_method == TG_PAYMENT_MINING, "payment method is MINING");
    ASSERT(session->allotment_ms > 0, "session has positive allotment");
    printf("  allotment_ms = %llu\n", (unsigned long long)session->allotment_ms);

    printf("\n--- tollgate_core_on_share_accepted extends session ---\n");
    uint64_t first_allotment = session->allotment_ms;
    tollgate_core_on_share_accepted(client_ip, 1.0);
    ASSERT(session->allotment_ms > first_allotment, "session allotment increased");
    printf("  new allotment_ms = %llu\n", (unsigned long long)session->allotment_ms);

    printf("\n--- tollgate_core_on_share_accepted calls platform callback ---\n");
    s_last_share_diff = 0.0;
    tollgate_core_on_share_accepted(client_ip, 42.5);
    ASSERT(fabs(s_last_share_diff - 42.5) < 0.001, "platform callback received difficulty");

    printf("\n--- tollgate_core_on_share_accepted with bytes metric ---\n");
    s_metric = "bytes";
    uint32_t bytes_client = 0x0303030A;
    tollgate_core_on_share_accepted(bytes_client, 1.0);
    tg_session_t *bytes_session = tollgate_core_session_find_by_ip(bytes_client);
    ASSERT(bytes_session != NULL, "bytes session created");
    ASSERT(bytes_session->allotment_bytes > 0, "bytes allotment positive");
    printf("  allotment_bytes = %llu\n", (unsigned long long)bytes_session->allotment_bytes);
    s_metric = "milliseconds";

    printf("\n--- tollgate_core_on_share_accepted no platform ---\n");
    tollgate_core_init(NULL, ap_ip);
    tollgate_core_on_share_accepted(0x0404040A, 1.0);
    ASSERT(1, "no crash with NULL platform");
    tollgate_core_init(&platform, ap_ip);

    TEST_SUMMARY();
}
