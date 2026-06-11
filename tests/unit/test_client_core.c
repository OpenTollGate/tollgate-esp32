#include "test_framework.h"
#include "../../components/tollgate_core/src/tollgate_core_client.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    printf("=== test_client_core ===\n");

    printf("\n--- parse_discovery valid ---\n");
    {
        const char *json = "{\"kind\":10021,\"content\":\"discovery\","
                           "\"tags\":["
                           "[\"metric\",\"milliseconds\"],"
                           "[\"step_size\",\"60000\"],"
                           "[\"price_per_step\",\"cashu\",\"21\",\"sat\",\"https://test.mint\",\"1\"]"
                           "]}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "parse_discovery returns true");
        ASSERT(disc.is_tollgate, "is_tollgate set");
        ASSERT_EQ_INT(21, disc.price_per_step, "price_per_step");
        ASSERT_EQ_INT(60000, disc.step_size_ms, "step_size_ms");
        ASSERT(strcmp(disc.metric, "milliseconds") == 0, "metric");
        ASSERT(strcmp(disc.mint_url, "https://test.mint") == 0, "mint_url");
        ASSERT(strcmp(disc.unit, "sat") == 0, "unit is sat");
    }

    printf("\n--- parse_discovery wrong kind ---\n");
    {
        const char *json = "{\"kind\":9999}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(!ok, "wrong kind returns false");
    }

    printf("\n--- parse_discovery mining payment ---\n");
    {
        const char *json = "{\"kind\":10021,\"content\":\"\","
                           "\"tags\":["
                           "[\"metric\",\"milliseconds\"],"
                           "[\"step_size\",\"60000\"],"
                           "[\"price_per_step\",\"0\",\"mining\",\"3333\",\"extra\"]"
                           "]}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "parse_discovery mining returns true");
        ASSERT(disc.mining_available, "mining_available set");
        ASSERT_EQ_INT(3333, (int)disc.mining_port, "mining_port");
    }

    printf("\n--- parse_discovery empty tags ---\n");
    {
        const char *json = "{\"kind\":10021,\"tags\":[]}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "empty tags returns true");
        ASSERT(disc.is_tollgate, "is_tollgate set");
    }

    printf("\n--- parse_discovery no tags ---\n");
    {
        const char *json = "{\"kind\":10021}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "no tags returns true");
    }

    printf("\n--- parse_discovery hash unit ---\n");
    {
        const char *json = "{\"kind\":10021,\"content\":\"\","
                           "\"tags\":["
                           "[\"metric\",\"milliseconds\"],"
                           "[\"step_size\",\"60000\"],"
                           "[\"price_per_step\",\"cashu\",\"21\",\"hash\",\"http://66.92.204.38:3338\",\"1\"]"
                           "]}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "parse_discovery hash returns true");
        ASSERT_EQ_INT(21, disc.price_per_step, "price_per_step");
        ASSERT(strcmp(disc.unit, "hash") == 0, "unit is hash");
        ASSERT(strcmp(disc.mint_url, "http://66.92.204.38:3338") == 0, "mint_url");
    }

    printf("\n--- parse_discovery unit default ---\n");
    {
        const char *json = "{\"kind\":10021,\"tags\":[]}";
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery(json, &disc);
        ASSERT(ok, "empty tags returns true");
        ASSERT(strcmp(disc.unit, "sat") == 0, "unit defaults to sat");
    }

    printf("\n--- parse_discovery invalid JSON ---\n");
    {
        tollgate_discovery_t disc = {0};
        bool ok = tollgate_core_client_parse_discovery("not json", &disc);
        ASSERT(!ok, "invalid JSON returns false");
    }

    printf("\n--- parse_session valid ---\n");
    {
        const char *json = "{\"kind\":1022,\"content\":\"session\","
                           "\"tags\":[[\"allotment\",\"120000\"]]}";
        int64_t allotment = 0;
        bool ok = tollgate_core_client_parse_session(json, &allotment);
        ASSERT(ok, "parse_session returns true");
        ASSERT_EQ_UINT64(120000, (unsigned long long)allotment, "allotment_ms");
    }

    printf("\n--- parse_session wrong kind ---\n");
    {
        const char *json = "{\"kind\":10021}";
        int64_t allotment = 0;
        bool ok = tollgate_core_client_parse_session(json, &allotment);
        ASSERT(!ok, "wrong kind returns false");
    }

    printf("\n--- parse_session no allotment tag ---\n");
    {
        const char *json = "{\"kind\":1022,\"tags\":[]}";
        int64_t allotment = 999;
        bool ok = tollgate_core_client_parse_session(json, &allotment);
        ASSERT(ok, "returns true");
        ASSERT_EQ_UINT64(999, (unsigned long long)allotment, "allotment unchanged when no tag");
    }

    printf("\n--- parse_usage valid ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_client_parse_usage("45000/60000", &remaining, &total);
        ASSERT(ok, "parse_usage returns true");
        ASSERT_EQ_UINT64(45000, (unsigned long long)remaining, "remaining");
        ASSERT_EQ_UINT64(60000, (unsigned long long)total, "total");
    }

    printf("\n--- parse_usage no slash ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_client_parse_usage("45000", &remaining, &total);
        ASSERT(!ok, "no slash returns false");
    }

    printf("\n--- parse_usage zero values ---\n");
    {
        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_client_parse_usage("0/0", &remaining, &total);
        ASSERT(ok, "parse_usage zero values");
        ASSERT_EQ_UINT64(0, (unsigned long long)remaining, "remaining is 0");
        ASSERT_EQ_UINT64(0, (unsigned long long)total, "total is 0");
    }

    printf("\n--- should_renew ---\n");
    {
        ASSERT(tollgate_core_client_should_renew(1000, 60000, 20),
               "1.6% remaining → should renew");
        ASSERT(tollgate_core_client_should_renew(0, 60000, 20),
               "0% remaining → should renew");
        ASSERT(!tollgate_core_client_should_renew(30000, 60000, 20),
               "50% remaining → should not renew");
        ASSERT(!tollgate_core_client_should_renew(60000, 60000, 20),
               "100% remaining → should not renew");
        ASSERT(tollgate_core_client_should_renew(11999, 60000, 20),
               "19.99% → should renew");
        ASSERT(tollgate_core_client_should_renew(12000, 60000, 20),
               "20% exactly at threshold → should renew");
    }

    printf("\n--- should_renew default threshold ---\n");
    {
        ASSERT(tollgate_core_client_should_renew(1000, 60000, 0),
               "threshold=0 defaults to 20%");
        ASSERT(tollgate_core_client_should_renew(1000, 60000, -1),
               "negative threshold defaults to 20%");
    }

    printf("\n--- should_renew edge cases ---\n");
    {
        ASSERT(!tollgate_core_client_should_renew(-1, 60000, 20),
               "negative remaining → false");
        ASSERT(!tollgate_core_client_should_renew(1000, 0, 20),
               "zero allotment → false");
    }

    printf("\n--- calc_price_per_min ---\n");
    {
        ASSERT_EQ_INT(21, tollgate_core_client_calc_price_per_min(21, 60000),
                      "21 sats/60s = 21 sats/min");
        ASSERT_EQ_INT(42, tollgate_core_client_calc_price_per_min(21, 30000),
                      "21 sats/30s = 42 sats/min");
        ASSERT_EQ_INT(21000, tollgate_core_client_calc_price_per_min(21, 60),
                      "21 sats/60ms effective");
    }

    printf("\n--- calc_price_per_min zero step ---\n");
    {
        int result = tollgate_core_client_calc_price_per_min(21, 0);
        ASSERT(result > 0, "zero step_size_ms doesn't crash");
    }

    printf("\n--- calc_steps ---\n");
    {
        ASSERT_EQ_INT(21, tollgate_core_client_calc_steps(1, 21, 21),
                      "1 step * 21 = 21");
        ASSERT_EQ_INT(42, tollgate_core_client_calc_steps(2, 21, 21),
                      "2 steps * 21 = 42");
        ASSERT_EQ_INT(21, tollgate_core_client_calc_steps(0, 21, 21),
                      "0 steps defaults to 1");
        ASSERT_EQ_INT(21, tollgate_core_client_calc_steps(-1, 21, 21),
                      "negative steps defaults to 1");
    }

    TEST_SUMMARY();
}
