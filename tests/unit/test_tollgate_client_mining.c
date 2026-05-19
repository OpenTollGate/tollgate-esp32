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

int main(void)
{
    printf("=== test_tollgate_client_mining ===\n");

    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.client_enabled = true;

    printf("\n--- mining tag: mining_available=true, port=3333 ---\n");
    {
        const char *json = "{\"kind\":10021,\"tags\":["
            "[\"metric\",\"milliseconds\"],"
            "[\"step_size\",\"60000\"],"
            "[\"price_per_step\",\"0\",\"mining\",\"3333\",\"sat\"],"
            "[\"tips\",\"1\",\"2\",\"5\"]"
            "]}";

        tollgate_discovery_t disc;
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(ok, "mining discovery parsed");
        ASSERT(disc.is_tollgate, "is_tollgate=true");
        ASSERT(disc.mining_available, "mining_available=true");
        ASSERT_EQ_INT(3333, (int)disc.mining_port, "mining_port=3333");
    }

    printf("\n--- mining tag: no mining tag ---\n");
    {
        const char *json = "{\"kind\":10021,\"tags\":["
            "[\"metric\",\"milliseconds\"],"
            "[\"step_size\",\"60000\"],"
            "[\"price_per_step\",\"cashu\",\"21\",\"sat\",\"https://testnut.cashu.space\",\"1\"]"
            "]}";

        tollgate_discovery_t disc;
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(ok, "cashu discovery parsed");
        ASSERT(disc.is_tollgate, "is_tollgate=true");
        ASSERT(!disc.mining_available, "mining_available=false");
        ASSERT_EQ_INT(0, (int)disc.mining_port, "mining_port=0 when no mining");
        ASSERT_EQ_INT(21, disc.price_per_step, "price_per_step=21 for cashu");
    }

    printf("\n--- mining tag: custom port 4033 ---\n");
    {
        const char *json = "{\"kind\":10021,\"tags\":["
            "[\"metric\",\"milliseconds\"],"
            "[\"step_size\",\"60000\"],"
            "[\"price_per_step\",\"0\",\"mining\",\"4033\",\"sat\"]"
            "]}";

        tollgate_discovery_t disc;
        bool ok = parse_discovery_response(json, &disc);
        ASSERT(ok, "mining custom port parsed");
        ASSERT(disc.mining_available, "mining_available=true");
        ASSERT_EQ_INT(4033, (int)disc.mining_port, "mining_port=4033");
    }

    printf("\n--- tollgate_discovery_t zero-init ---\n");
    {
        tollgate_discovery_t disc = {0};
        ASSERT(!disc.is_tollgate, "zero-init: is_tollgate=false");
        ASSERT(!disc.mining_available, "zero-init: mining_available=false");
        ASSERT_EQ_INT(0, (int)disc.mining_port, "zero-init: mining_port=0");
        ASSERT_EQ_INT(0, disc.price_per_step, "zero-init: price=0");
    }

    printf("\n--- TG_CLIENT_MINING state enum ---\n");
    {
        ASSERT(TG_CLIENT_MINING > TG_CLIENT_PAID, "MINING > PAID in enum");
        ASSERT(TG_CLIENT_MINING < TG_CLIENT_ERROR, "MINING < ERROR in enum");
    }

    printf("\n--- discovery struct fields ---\n");
    {
        tollgate_discovery_t disc;
        memset(&disc, 0, sizeof(disc));
        disc.mining_available = true;
        disc.mining_port = 9999;
        ASSERT(disc.mining_available, "mining_available set");
        ASSERT_EQ_INT(9999, (int)disc.mining_port, "mining_port set");
    }

    TEST_SUMMARY();
}
