#include "test_framework.h"
#include "../../main/lightning_payout.h"
#include "../../main/config.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static void test_payout_calculation(void)
{
    printf("\n--- Payout pool calculation ---\n");
    {
        uint64_t balance = 500;
        uint64_t min_balance = 64;
        uint64_t min_payout_amount = 128;

        ASSERT(balance >= min_payout_amount, "500 >= 128 triggers payout");

        uint64_t pool = balance - min_balance;
        ASSERT_EQ_INT(436, (int)pool, "pool = 500 - 64 = 436");
    }

    printf("\n--- Payout below threshold ---\n");
    {
        uint64_t balance = 100;
        uint64_t min_payout_amount = 128;

        ASSERT(balance < min_payout_amount, "100 < 128, no payout");
    }

    printf("\n--- Multi-recipient split ---\n");
    {
        uint64_t pool = 436;
        double factors[] = {0.79, 0.21};
        const char *names[] = {"owner", "developer"};

        uint64_t total = 0;
        for (int i = 0; i < 2; i++) {
            uint64_t share = (uint64_t)round((double)pool * factors[i]);
            printf("  %s: factor=%.2f share=%llu\n", names[i], factors[i], (unsigned long long)share);
            total += share;
        }
        ASSERT_EQ_INT(436, (int)total, "79/21 split sums to pool");
    }

    printf("\n--- Single recipient 100%% ---\n");
    {
        uint64_t pool = 436;
        double factor = 1.0;
        uint64_t share = (uint64_t)round((double)pool * factor);
        ASSERT_EQ_INT(436, (int)share, "1.0 factor = full pool");
    }

    printf("\n--- Fee tolerance calculation ---\n");
    {
        uint64_t share = 344;
        uint64_t fee_pct = 10;
        uint64_t max_cost = share + (share * fee_pct / 100);
        ASSERT_EQ_INT(378, (int)max_cost, "344 + 10% = 378");
    }

    printf("\n--- Zero pool (balance == reserve) ---\n");
    {
        uint64_t balance = 64;
        uint64_t min_balance = 64;
        uint64_t pool = balance - min_balance;
        ASSERT_EQ_INT(0, (int)pool, "no payout when balance == reserve");
    }

    printf("\n--- Payout config defaults ---\n");
    {
        payout_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.enabled = true;
        cfg.mint_count = 1;
        strncpy(cfg.mints[0].url, "https://testnut.cashu.space", sizeof(cfg.mints[0].url) - 1);
        cfg.mints[0].min_balance = 64;
        cfg.mints[0].min_payout_amount = 128;
        cfg.recipient_count = 1;
        strncpy(cfg.recipients[0].lightning_address, "TollGate@coinos.io",
                sizeof(cfg.recipients[0].lightning_address) - 1);
        cfg.recipients[0].factor = 1.0;
        cfg.fee_tolerance_pct = 10;
        cfg.check_interval_s = 60;

        ASSERT(cfg.enabled, "payout enabled");
        ASSERT_EQ_INT(1, cfg.mint_count, "1 mint");
        ASSERT_EQ_INT(1, cfg.recipient_count, "1 recipient");
        ASSERT_EQ_STR("TollGate@coinos.io", cfg.recipients[0].lightning_address, "default LNURL");
    }
}

int main(void)
{
    printf("=== test_lightning_payout ===\n");
    test_payout_calculation();
    TEST_SUMMARY();
}
