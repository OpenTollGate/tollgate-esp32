#include "tollgate_firewall.h"
#include "tollgate_core.h"
#include "tollgate_platform.h"
#include <string.h>
#include <stdio.h>

#define FW_MAX_CLIENTS 10

static const char *TAG = "tg_fw";
static uint32_t s_ap_ip;
static uint16_t s_mining_port;
static bool s_sandbox_mint;

typedef struct {
    uint32_t ip;
    char mac[TG_FW_MAX_MAC_LEN];
} fw_client_t;

static fw_client_t s_clients[FW_MAX_CLIENTS];
static int s_client_count = 0;

static void log_fw(const char *verb, uint32_t client_ip, const char *mac)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->log_info) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 (int)((client_ip >> 0) & 0xFF), (int)((client_ip >> 8) & 0xFF),
                 (int)((client_ip >> 16) & 0xFF), (int)((client_ip >> 24) & 0xFF));
        p->log_info(TAG, "%s %s mac=%s", verb, ip_str, mac ? mac : "unknown");
    }
}

int tg_firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, int mac_out_size)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (!p) return -1;

    if (p->mac_for_ip) {
        if (p->mac_for_ip(client_ip, mac_out, mac_out_size)) {
            return 0;
        }
    }
    return -1;
}

int tg_firewall_init(uint32_t ap_ip)
{
    s_ap_ip = ap_ip;
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0;
    s_mining_port = 0;
    s_sandbox_mint = false;

    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->napt_enable) p->napt_enable(ap_ip, true);

    if (p && p->log_info) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 (int)((ap_ip >> 0) & 0xFF), (int)((ap_ip >> 8) & 0xFF),
                 (int)((ap_ip >> 16) & 0xFF), (int)((ap_ip >> 24) & 0xFF));
        p->log_info(TAG, "Firewall initialized AP=%s NAT on, per-client filter", ip_str);
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

void tg_firewall_grant(uint32_t client_ip)
{
    fw_client_t *existing = find_client_by_ip(client_ip);
    if (existing) return;

    if (s_client_count >= FW_MAX_CLIENTS) {
        const tollgate_platform_t *p = tollgate_core_get_platform();
        if (p && p->log_warn) p->log_warn(TAG, "Max clients, cannot grant");
        return;
    }

    fw_client_t *c = &s_clients[s_client_count];
    c->ip = client_ip;
    c->mac[0] = '\0';
    tg_firewall_get_mac_for_ip(client_ip, c->mac, sizeof(c->mac));
    s_client_count++;

    log_fw("granted", client_ip, c->mac[0] ? c->mac : "unknown");
}

void tg_firewall_revoke(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) {
            log_fw("revoked", client_ip, s_clients[i].mac[0] ? s_clients[i].mac : "unknown");
            s_clients[i] = s_clients[s_client_count - 1];
            s_client_count--;
            return;
        }
    }
}

int tg_firewall_revoke_all(void)
{
    s_client_count = 0;
    memset(s_clients, 0, sizeof(s_clients));
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->log_info) p->log_info(TAG, "All clients revoked");
    return 0;
}

bool tg_firewall_is_allowed(uint32_t client_ip)
{
    return find_client_by_ip(client_ip) != NULL;
}

bool tg_firewall_is_mac_allowed(const char *mac)
{
    return find_client_by_mac(mac) != NULL;
}

int tg_firewall_client_count(void)
{
    return s_client_count;
}

void tg_firewall_set_mining_port(uint16_t port)
{
    s_mining_port = port;
}

void tg_firewall_set_sandbox_mint_access(bool enable)
{
    s_sandbox_mint = enable;
}

int tg_firewall_filter_packet(const uint8_t *payload, int payload_len)
{
    if (payload_len < 20) return -1;

    uint32_t src_ip = (uint32_t)payload[12] | ((uint32_t)payload[13] << 8) |
                      ((uint32_t)payload[14] << 16) | ((uint32_t)payload[15] << 24);

    uint32_t ap_subnet = s_ap_ip & 0x00FFFFFF;
    uint32_t src_subnet = src_ip & 0x00FFFFFF;
    if (src_subnet != ap_subnet) return 1;

    if (tg_firewall_is_allowed(src_ip)) return 1;

    return 0;
}
