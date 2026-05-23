#include "test_framework.h"
#include "tollgate_core_mining.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int main(void)
{
    printf("=== test_mining_payment ===\n");

    tollgate_core_mining_init();

    printf("\n--- tollgate_core_mining_nbits_to_difficulty ---\n");
    uint64_t d1 = tollgate_core_mining_nbits_to_difficulty(0);
    ASSERT(d1 == UINT64_MAX, "nbits=0 returns max");

    uint64_t d2 = tollgate_core_mining_nbits_to_difficulty(0x170309E2);
    ASSERT(d2 > 0, "mainnet nbits gives positive difficulty");
    printf("  INFO: difficulty for 0x170309E2 = %llu\n", (unsigned long long)d2);

    uint64_t d3 = tollgate_core_mining_nbits_to_difficulty(0x1d00ffff);
    ASSERT(d3 == 1, "pseudotransaction nbits = difficulty 1");

    printf("\n--- tollgate_core_mining_calc_hashprice ---\n");
    double hp1 = tollgate_core_mining_calc_hashprice(0);
    ASSERT(hp1 == 0.0, "nbits=0 gives hashprice 0");

    double hp2 = tollgate_core_mining_calc_hashprice(0x170309E2);
    ASSERT(hp2 > 0.0, "mainnet nbits gives positive hashprice");
    printf("  INFO: hashprice for 0x170309E2 = %.6f sat/GH/s/day\n", hp2);

    printf("\n--- tollgate_core_mining_calc_hashprice_override ---\n");
    double hp3 = tollgate_core_mining_calc_hashprice_override(1000);
    ASSERT(fabs(hp3 - 1000.0) < 0.001, "override returns exact value");

    printf("\n--- tollgate_core_mining_set_current_nbits ---\n");
    tollgate_core_mining_set_current_nbits(0x170309E2);
    double hp4 = tollgate_core_mining_get_current_hashprice();
    ASSERT(fabs(hp4 - hp2) < 0.0001, "stored hashprice matches calculated");

    printf("\n--- tollgate_core_mining_get_or_create_client ---\n");
    tollgate_mining_client_stats_t *c1 = tollgate_core_mining_get_or_create_client(0x0A010203);
    ASSERT(c1 != NULL, "created client 1");
    ASSERT(c1->ip == 0x0A010203, "client 1 IP matches");
    ASSERT(c1->shares_accepted == 0, "client 1 starts with 0 shares");

    tollgate_mining_client_stats_t *c2 = tollgate_core_mining_get_or_create_client(0x0A010204);
    ASSERT(c2 != NULL, "created client 2");
    ASSERT(c2->ip == 0x0A010204, "client 2 IP matches");

    tollgate_mining_client_stats_t *c1_again = tollgate_core_mining_get_or_create_client(0x0A010203);
    ASSERT(c1_again == c1, "same IP returns same client");

    printf("\n--- tollgate_core_mining_update_hashrate ---\n");
    tollgate_core_mining_update_hashrate(0x0A010203, true);
    tollgate_core_mining_update_hashrate(0x0A010203, true);
    tollgate_core_mining_update_hashrate(0x0A010203, false);

    const tollgate_mining_client_stats_t *c1_stats = tollgate_core_mining_get_client_stats(0x0A010203);
    ASSERT(c1_stats != NULL, "client 1 stats found");
    ASSERT(c1_stats->shares_accepted == 2, "client 1 has 2 accepted");
    ASSERT(c1_stats->shares_rejected == 1, "client 1 has 1 rejected");

    printf("\n--- tollgate_core_mining_get_client_stats ---\n");
    const tollgate_mining_client_stats_t *notfound = tollgate_core_mining_get_client_stats(0xFFFFFFFF);
    ASSERT(notfound == NULL, "nonexistent client returns NULL");

    printf("\n--- tollgate_core_mining_shares_to_allotment_ms ---\n");
    uint64_t allot1 = tollgate_core_mining_shares_to_allotment_ms(0.0, 100.0, 21, 60000);
    ASSERT(allot1 == 0, "zero hashrate = zero allotment");

    uint64_t allot2 = tollgate_core_mining_shares_to_allotment_ms(1.0, 100.0, 21, 60000);
    ASSERT(allot2 > 0, "positive hashrate gives positive allotment");
    printf("  INFO: 1 GH/s at 100 sat/GH/s/day, 21 sats/60s => %llu ms\n", (unsigned long long)allot2);

    uint64_t allot3 = tollgate_core_mining_shares_to_allotment_ms(10.0, 50.0, 10, 30000);
    ASSERT(allot3 > 0, "10 GH/s at 50 sat gives positive allotment");
    printf("  INFO: 10 GH/s at 50 sat/GH/s/day, 10 sats/30s => %llu ms\n", (unsigned long long)allot3);

    printf("\n--- tollgate_core_mining_shares_to_allotment_bytes ---\n");
    uint64_t ab1 = tollgate_core_mining_shares_to_allotment_bytes(0.0, 100.0, 21, 1048576);
    ASSERT(ab1 == 0, "zero hashrate = zero bytes");

    uint64_t ab2 = tollgate_core_mining_shares_to_allotment_bytes(1.0, 100.0, 21, 1048576);
    ASSERT(ab2 > 0, "positive hashrate gives positive bytes");

    printf("\n--- tollgate_core_mining_validate_share (stub) ---\n");
    esp_err_t vr = tollgate_core_mining_validate_share(NULL, 0, NULL, 0);
    ASSERT(vr == ESP_OK, "validate share stub returns OK");

    TEST_SUMMARY();
}
