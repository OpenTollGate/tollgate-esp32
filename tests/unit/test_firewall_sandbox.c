#include "test_framework.h"
#include "../../main/firewall.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== test_firewall_sandbox ===\n");

    printf("\n--- FW_MAX_MAC_LEN is 18 ---\n");
    {
        ASSERT_EQ_INT(18, FW_MAX_MAC_LEN, "MAC length is 18 (17 chars + null)");
    }

    printf("\n--- esp_ip4_addr_t available ---\n");
    {
        esp_ip4_addr_t ip;
        ip.addr = 0x0102A8C0;
        ASSERT(ip.addr == 0x0102A8C0, "ip4_addr stores value");
    }

    printf("\n--- firewall_set_mining_port / set_sandbox_mint_access compile ---\n");
    {
        firewall_set_mining_port(3333);
        firewall_set_mining_port(4033);
        firewall_set_sandbox_mint_access(true);
        firewall_set_sandbox_mint_access(false);
        ASSERT(true, "setters compile and run without crash");
    }

    printf("\n--- firewall_init + client management ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        esp_err_t ret = firewall_init(ap_ip);
        ASSERT_EQ_INT(ESP_OK, (int)ret, "firewall_init succeeds");
        ASSERT_EQ_INT(0, firewall_client_count(), "no clients after init");

        firewall_grant_access(0x0201A8C0);
        ASSERT_EQ_INT(1, firewall_client_count(), "1 client after grant");
        ASSERT(firewall_is_client_allowed(0x0201A8C0), "client is allowed");

        firewall_revoke_access(0x0201A8C0);
        ASSERT_EQ_INT(0, firewall_client_count(), "0 clients after revoke");
        ASSERT(!firewall_is_client_allowed(0x0201A8C0), "client not allowed after revoke");
    }

    printf("\n--- grant same IP twice ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        firewall_init(ap_ip);

        firewall_grant_access(0x0301A8C0);
        firewall_grant_access(0x0301A8C0);
        ASSERT_EQ_INT(1, firewall_client_count(), "duplicate grant does not double count");
    }

    printf("\n--- revoke non-existent ---\n");
    {
        firewall_revoke_access(0x99999999);
        ASSERT_EQ_INT(1, firewall_client_count(), "revoke non-existent no effect");
    }

    printf("\n--- revoke_all ---\n");
    {
        firewall_grant_access(0x0401A8C0);
        firewall_grant_access(0x0501A8C0);
        ASSERT_EQ_INT(3, firewall_client_count(), "3 clients");
        firewall_revoke_all();
        ASSERT_EQ_INT(0, firewall_client_count(), "0 after revoke_all");
    }

    printf("\n--- max clients (10) ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        firewall_init(ap_ip);

        for (int i = 0; i < 10; i++) {
            firewall_grant_access(0x0A000000 + i);
        }
        ASSERT_EQ_INT(10, firewall_client_count(), "10 clients at max");

        firewall_grant_access(0x0A000100);
        ASSERT_EQ_INT(10, firewall_client_count(), "still 10 after exceeding max");
    }

    printf("\n--- is_mac_allowed (no MACs resolved in stub) ---\n");
    {
        firewall_init((esp_ip4_addr_t){ .addr = 0x012FA80A });
        firewall_grant_access(0x0601A8C0);
        ASSERT(!firewall_is_mac_allowed(""), "empty MAC not allowed");
    }

    TEST_SUMMARY();
}
