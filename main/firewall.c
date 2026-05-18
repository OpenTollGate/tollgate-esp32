#include "firewall.h"
#include "dns_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "lwip/lwip_napt.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/ip.h"
#include <string.h>

#define MAX_CLIENTS 10

static const char *TAG = "firewall";
static esp_ip4_addr_t s_ap_ip;
static uint16_t s_mining_port = 3333;
static bool s_sandbox_mint_access = false;

typedef struct {
    uint32_t ip;
    char mac[FW_MAX_MAC_LEN];
} fw_client_t;

static fw_client_t s_clients[MAX_CLIENTS];
static int s_client_count = 0;

esp_err_t firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, size_t mac_out_size)
{
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        wifi_sta_mac_ip_list_t ip_mac_list;
        if (esp_wifi_ap_get_sta_list_with_ip(&sta_list, &ip_mac_list) == ESP_OK) {
            for (int i = 0; i < ip_mac_list.num; i++) {
                if (ip_mac_list.sta[i].ip.addr == client_ip) {
                    snprintf(mac_out, mac_out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
                             ip_mac_list.sta[i].mac[0], ip_mac_list.sta[i].mac[1],
                             ip_mac_list.sta[i].mac[2], ip_mac_list.sta[i].mac[3],
                             ip_mac_list.sta[i].mac[4], ip_mac_list.sta[i].mac[5]);
                    return ESP_OK;
                }
            }
        }
    }

    ip4_addr_t *entry_ip = NULL;
    struct netif *entry_netif = NULL;
    struct eth_addr *entry_eth = NULL;
    ssize_t i = 0;
    while (etharp_get_entry(i, &entry_ip, &entry_netif, &entry_eth) == ERR_OK) {
        if (entry_ip && entry_ip->addr == client_ip && entry_eth) {
            snprintf(mac_out, mac_out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
                     entry_eth->addr[0], entry_eth->addr[1], entry_eth->addr[2],
                     entry_eth->addr[3], entry_eth->addr[4], entry_eth->addr[5]);
            return ESP_OK;
        }
        i++;
    }
    return ESP_FAIL;
}

esp_err_t firewall_init(esp_ip4_addr_t ap_ip)
{
    s_ap_ip = ap_ip;
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0;
    ip_napt_enable(s_ap_ip.addr, 1);
    ESP_LOGI(TAG, "Firewall initialized with AP IP=" IPSTR " (NAT always on, per-client filter)", IP2STR(&s_ap_ip));
    return ESP_OK;
}

void firewall_set_mining_port(uint16_t port)
{
    s_mining_port = port;
}

void firewall_set_sandbox_mint_access(bool enabled)
{
    s_sandbox_mint_access = enabled;
}

static bool is_sandbox_allowed(struct pbuf *p)
{
    if (p->len < IP_HLEN) return false;
    struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
    uint32_t dest_ip_h = lwip_ntohl(iphdr->dest.addr);
    uint32_t ap_ip_h = lwip_ntohl(s_ap_ip.addr);

    if (dest_ip_h == ap_ip_h) {
        if (iphdr->_proto == IP_PROTO_TCP) {
            uint16_t dst_port = 0;
            if (p->len >= IP_HLEN + TCP_HLEN) {
                struct tcp_hdr *tcphdr = (struct tcp_hdr *)((uint8_t *)p->payload + IP_HLEN);
                dst_port = lwip_ntohs(tcphdr->dest);
            }
            if (dst_port == 80 || dst_port == 2121 || dst_port == s_mining_port) {
                return true;
            }
        }
        if (iphdr->_proto == IP_PROTO_UDP) {
            return true;
        }
    }

    if (s_sandbox_mint_access && iphdr->_proto == IP_PROTO_TCP) {
        return true;
    }

    return false;
}

int tollgate_ip4_canforward_filter(struct pbuf *p, u32_t dest_addr_hostorder)
{
    (void)dest_addr_hostorder;
    if (p->len < IP_HLEN) return -1;
    struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
    uint32_t src_ip_h = lwip_ntohl(iphdr->src.addr);
    uint32_t ap_subnet = lwip_ntohl(s_ap_ip.addr) & 0xFFFFFF00;
    if ((src_ip_h & 0xFFFFFF00) != ap_subnet) {
        return 1;
    }
    if (firewall_is_client_allowed(iphdr->src.addr)) {
        return 1;
    }
    if (is_sandbox_allowed(p)) {
        return 1;
    }
    return 0;
}

static fw_client_t *find_client_by_ip(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) return &s_clients[i];
    }
    return NULL;
}

static fw_client_t *find_client_by_mac(const char *mac)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].mac[0] != '\0' && strcmp(s_clients[i].mac, mac) == 0) {
            return &s_clients[i];
        }
    }
    return NULL;
}

void firewall_grant_access(uint32_t client_ip)
{
    fw_client_t *existing = find_client_by_ip(client_ip);
    if (existing) {
        existing->ip = client_ip;
        return;
    }
    if (s_client_count >= MAX_CLIENTS) {
        ESP_LOGW(TAG, "Max clients reached, cannot grant access");
        return;
    }

    fw_client_t *client = &s_clients[s_client_count];
    client->ip = client_ip;
    client->mac[0] = '\0';
    firewall_get_mac_for_ip(client_ip, client->mac, sizeof(client->mac));
    s_client_count++;

    dns_server_set_client_authenticated(client_ip, true);

    esp_ip4_addr_t ip_addr = { .addr = client_ip };
    ESP_LOGI(TAG, "Access granted to " IPSTR " mac=%s", IP2STR(&ip_addr),
             client->mac[0] ? client->mac : "unknown");
}

void firewall_revoke_access(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) {
            esp_ip4_addr_t ip_addr = { .addr = client_ip };
            ESP_LOGI(TAG, "Access revoked for " IPSTR " mac=%s", IP2STR(&ip_addr),
                     s_clients[i].mac[0] ? s_clients[i].mac : "unknown");
            s_clients[i] = s_clients[s_client_count - 1];
            s_client_count--;
            dns_server_set_client_authenticated(client_ip, false);
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
    return find_client_by_ip(client_ip) != NULL;
}

bool firewall_is_mac_allowed(const char *mac)
{
    return find_client_by_mac(mac) != NULL;
}

int firewall_client_count(void)
{
    return s_client_count;
}
