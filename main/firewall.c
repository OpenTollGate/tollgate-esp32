#include "firewall.h"
#include "dns_server.h"
#include "esp_log.h"
#include "lwip/lwip_napt.h"
#include <string.h>

#define MAX_CLIENTS 10

static const char *TAG = "firewall";
static esp_ip4_addr_t s_ap_ip;
static bool s_nat_enabled = false;

typedef struct {
    uint32_t ip;
} fw_client_t;

static fw_client_t s_clients[MAX_CLIENTS];
static int s_client_count = 0;

esp_err_t firewall_init(esp_ip4_addr_t ap_ip)
{
    s_ap_ip = ap_ip;
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0;
    ESP_LOGI(TAG, "Firewall initialized with AP IP=" IPSTR, IP2STR(&s_ap_ip));
    return ESP_OK;
}

void firewall_enable_nat(void)
{
    if (s_nat_enabled) return;
    ip_napt_enable(s_ap_ip.addr, 1);
    s_nat_enabled = true;
    ESP_LOGI(TAG, "NAT enabled");
}

void firewall_disable_nat(void)
{
    if (!s_nat_enabled) return;
    ip_napt_enable(s_ap_ip.addr, 0);
    s_nat_enabled = false;
    ESP_LOGI(TAG, "NAT disabled");
}

void firewall_grant_access(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) return;
    }
    if (s_client_count >= MAX_CLIENTS) {
        ESP_LOGW(TAG, "Max clients reached, cannot grant access");
        return;
    }
    s_clients[s_client_count].ip = client_ip;
    s_client_count++;
    dns_server_set_client_authenticated(client_ip, true);

    esp_ip4_addr_t ip_addr = { .addr = client_ip };
    ESP_LOGI(TAG, "Access granted to " IPSTR, IP2STR(&ip_addr));
}

void firewall_revoke_access(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) {
            s_clients[i] = s_clients[s_client_count - 1];
            s_client_count--;
            dns_server_set_client_authenticated(client_ip, false);
            esp_ip4_addr_t ip_addr = { .addr = client_ip };
            ESP_LOGI(TAG, "Access revoked for " IPSTR, IP2STR(&ip_addr));
            return;
        }
    }
}

void firewall_revoke_all(void)
{
    for (int i = 0; i < s_client_count; i++) {
        dns_server_set_client_authenticated(s_clients[i].ip, false);
    }
    s_client_count = 0;
    ESP_LOGI(TAG, "All client access revoked");
}

bool firewall_is_client_allowed(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) return true;
    }
    return false;
}

int firewall_client_count(void)
{
    return s_client_count;
}
