#include "test_framework.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== test_firewall_sandbox ===\n");

    printf("\n--- TG_FW_MAX_MAC_LEN is 18 ---\n");
    {
        ASSERT_EQ_INT(18, TG_FW_MAX_MAC_LEN, "MAC length is 18 (17 chars + null)");
    }

    printf("\n--- esp_ip4_addr_t available ---\n");
    {
        esp_ip4_addr_t ip;
        ip.addr = 0x0102A8C0;
        ASSERT(ip.addr == 0x0102A8C0, "ip4_addr stores value");
    }

    printf("\n--- tollgate_core_fw_set_sandbox_ports / set_sandbox_mint_access compile ---\n");
    {
        tollgate_core_fw_set_sandbox_ports(3333);
        tollgate_core_fw_set_sandbox_ports(4033);
        tollgate_core_fw_set_sandbox_mint_access(true);
        tollgate_core_fw_set_sandbox_mint_access(false);
        ASSERT(true, "setters compile and run without crash");
    }

    printf("\n--- tollgate_core_fw_init + client management ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        esp_err_t ret = tollgate_core_fw_init(ap_ip);
        ASSERT_EQ_INT(ESP_OK, (int)ret, "tollgate_core_fw_init succeeds");
        ASSERT_EQ_INT(0, tollgate_core_fw_client_count(), "no clients after init");

        tollgate_core_fw_grant(0x0201A8C0);
        ASSERT_EQ_INT(1, tollgate_core_fw_client_count(), "1 client after grant");
        ASSERT(tollgate_core_fw_is_allowed(0x0201A8C0), "client is allowed");

        tollgate_core_fw_revoke(0x0201A8C0);
        ASSERT_EQ_INT(0, tollgate_core_fw_client_count(), "0 clients after revoke");
        ASSERT(!tollgate_core_fw_is_allowed(0x0201A8C0), "client not allowed after revoke");
    }

    printf("\n--- grant same IP twice ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        tollgate_core_fw_init(ap_ip);

        tollgate_core_fw_grant(0x0301A8C0);
        tollgate_core_fw_grant(0x0301A8C0);
        ASSERT_EQ_INT(1, tollgate_core_fw_client_count(), "duplicate grant does not double count");
    }

    printf("\n--- revoke non-existent ---\n");
    {
        tollgate_core_fw_revoke(0x99999999);
        ASSERT_EQ_INT(1, tollgate_core_fw_client_count(), "revoke non-existent no effect");
    }

    printf("\n--- revoke_all ---\n");
    {
        tollgate_core_fw_grant(0x0401A8C0);
        tollgate_core_fw_grant(0x0501A8C0);
        ASSERT_EQ_INT(3, tollgate_core_fw_client_count(), "3 clients");
        tollgate_core_fw_revoke_all();
        ASSERT_EQ_INT(0, tollgate_core_fw_client_count(), "0 after revoke_all");
    }

    printf("\n--- max clients (10) ---\n");
    {
        esp_ip4_addr_t ap_ip = { .addr = 0x012FA80A };
        tollgate_core_fw_init(ap_ip);

        for (int i = 0; i < 10; i++) {
            tollgate_core_fw_grant(0x0A000000 + i);
        }
        ASSERT_EQ_INT(10, tollgate_core_fw_client_count(), "10 clients at max");

        tollgate_core_fw_grant(0x0A000100);
        ASSERT_EQ_INT(10, tollgate_core_fw_client_count(), "still 10 after exceeding max");
    }

    printf("\n--- is_mac_allowed (no MACs resolved in stub) ---\n");
    {
        tollgate_core_fw_init((esp_ip4_addr_t){ .addr = 0x012FA80A });
        tollgate_core_fw_grant(0x0601A8C0);
        ASSERT(!tollgate_core_fw_is_mac_allowed(""), "empty MAC not allowed");
    }

    TEST_SUMMARY();
}
