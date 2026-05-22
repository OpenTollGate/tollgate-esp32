#include "firewall.h"
#include "dns_server.h"
#include "tollgate_core.h"
#include "tollgate_core_firewall.h"
#include "esp_log.h"
#include "lwip/netif.h"
#include "lwip/lwip_napt.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/ip.h"
#include <string.h>

static const char *TAG = "firewall";
static esp_ip4_addr_t s_ap_ip;
static uint16_t s_mining_port = 3333;
static bool s_sandbox_mint_access = false;

esp_err_t firewall_init(esp_ip4_addr_t ap_ip)
{
    s_ap_ip = ap_ip;
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

esp_err_t firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, size_t mac_out_size)
{
    return tollgate_core_fw_get_mac_for_ip(client_ip, mac_out, mac_out_size);
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

void firewall_grant_access(uint32_t client_ip)
{
    tollgate_core_fw_grant(client_ip);
    dns_server_set_client_authenticated(client_ip, true);

    char mac[18] = {0};
    tollgate_core_fw_get_mac_for_ip(client_ip, mac, sizeof(mac));
    esp_ip4_addr_t ip_addr = { .addr = client_ip };
    ESP_LOGI(TAG, "Access granted to " IPSTR " mac=%s", IP2STR(&ip_addr),
             mac[0] ? mac : "unknown");
}

void firewall_revoke_access(uint32_t client_ip)
{
    tollgate_core_fw_revoke(client_ip);
    dns_server_set_client_authenticated(client_ip, false);

    esp_ip4_addr_t ip_addr = { .addr = client_ip };
    ESP_LOGI(TAG, "Access revoked for " IPSTR, IP2STR(&ip_addr));
}

void firewall_revoke_all(void)
{
    tollgate_core_fw_revoke_all();
    ESP_LOGI(TAG, "All client access revoked");
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
