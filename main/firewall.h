#ifndef FIREWALL_H
#define FIREWALL_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t firewall_init(esp_ip4_addr_t ap_ip);
void firewall_enable_nat(void);
void firewall_disable_nat(void);
void firewall_grant_access(uint32_t client_ip);
void firewall_revoke_access(uint32_t client_ip);
void firewall_revoke_all(void);
bool firewall_is_client_allowed(uint32_t client_ip);
int firewall_client_count(void);

#endif
