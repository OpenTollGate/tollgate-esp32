#ifndef TOLLGATE_CORE_FIREWALL_H
#define TOLLGATE_CORE_FIREWALL_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

struct pbuf;

#define TG_FW_MAX_MAC_LEN 18

esp_err_t tollgate_core_fw_init(esp_ip4_addr_t ap_ip);
void tollgate_core_fw_set_sandbox_ports(uint16_t mining_port);
void tollgate_core_fw_set_sandbox_mint_access(bool enabled);
void tollgate_core_fw_grant(uint32_t client_ip);
void tollgate_core_fw_revoke(uint32_t client_ip);
void tollgate_core_fw_revoke_all(void);
bool tollgate_core_fw_is_allowed(uint32_t client_ip);
bool tollgate_core_fw_is_mac_allowed(const char *mac);
int tollgate_core_fw_client_count(void);

esp_err_t tollgate_core_fw_get_mac_for_ip(uint32_t client_ip, char *mac_out, size_t mac_out_size);

int tollgate_core_ip4_canforward_filter(struct pbuf *p, uint32_t dest_addr_hostorder);

#endif
