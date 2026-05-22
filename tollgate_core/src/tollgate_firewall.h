#ifndef TOLLGATE_CORE_FIREWALL_H
#define TOLLGATE_CORE_FIREWALL_H

#include <stdint.h>
#include <stdbool.h>

#define TG_FW_MAX_MAC_LEN 18

int tg_firewall_init(uint32_t ap_ip);
void tg_firewall_grant(uint32_t client_ip);
void tg_firewall_revoke(uint32_t client_ip);
int tg_firewall_revoke_all(void);
bool tg_firewall_is_allowed(uint32_t client_ip);
bool tg_firewall_is_mac_allowed(const char *mac);
int tg_firewall_client_count(void);
int tg_firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, int mac_out_size);
void tg_firewall_set_mining_port(uint16_t port);
void tg_firewall_set_sandbox_mint_access(bool enable);
int tg_firewall_filter_packet(const uint8_t *payload, int payload_len);

#endif
