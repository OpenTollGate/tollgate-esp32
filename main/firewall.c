#include "firewall.h"
#include "tollgate_core.h"
#include "tollgate_core_firewall.h"
#include "esp_log.h"
#include "lwip/prot/ip4.h"
#include <string.h>

static const char *TAG = "firewall";
static esp_ip4_addr_t s_ap_ip;

esp_err_t firewall_init(esp_ip4_addr_t ap_ip)
{
    s_ap_ip = ap_ip;
    ESP_LOGI(TAG, "Firewall initialized with AP IP=" IPSTR, IP2STR(&s_ap_ip));
    return ESP_OK;
}

void firewall_set_mining_port(uint16_t port)
{
    tollgate_core_fw_set_sandbox_ports(port);
}

void firewall_set_sandbox_mint_access(bool enabled)
{
    tollgate_core_fw_set_sandbox_mint_access(enabled);
}

esp_err_t firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, size_t mac_out_size)
{
    return tollgate_core_fw_get_mac_for_ip(client_ip, mac_out, mac_out_size);
}

int tollgate_ip4_canforward_filter(struct pbuf *p, u32_t dest_addr_hostorder)
{
    return tollgate_core_ip4_canforward_filter(p, dest_addr_hostorder);
}

void firewall_grant_access(uint32_t client_ip)
{
    tollgate_core_fw_grant(client_ip);
}

void firewall_revoke_access(uint32_t client_ip)
{
    tollgate_core_fw_revoke(client_ip);
}

void firewall_revoke_all(void)
{
    tollgate_core_fw_revoke_all();
}

bool firewall_is_client_allowed(uint32_t client_ip)
{
    return tollgate_core_is_client_allowed(client_ip);
}

bool firewall_is_mac_allowed(const char *mac)
{
    return tollgate_core_fw_is_mac_allowed(mac);
}

int firewall_client_count(void)
{
    return tollgate_core_allowed_client_count();
}
