#ifndef FIREWALL_H
#define FIREWALL_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

struct pbuf;

#define FW_MAX_MAC_LEN 18

esp_err_t firewall_init(esp_ip4_addr_t ap_ip);
void firewall_grant_access(uint32_t client_ip);
void firewall_revoke_access(uint32_t client_ip);
void firewall_revoke_all(void);
bool firewall_is_client_allowed(uint32_t client_ip);
bool firewall_is_mac_allowed(const char *mac);
int firewall_client_count(void);

esp_err_t firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, size_t mac_out_size);

int tollgate_ip4_canforward_filter(struct pbuf *p, uint32_t dest_addr_hostorder);

#endif
