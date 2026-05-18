#ifndef TOLLGATE_CORE_DNS_H
#define TOLLGATE_CORE_DNS_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

esp_err_t tollgate_core_dns_start_internal(esp_ip4_addr_t ap_ip, esp_ip4_addr_t upstream_dns);
void tollgate_core_dns_stop(void);
void tollgate_core_dns_set_authenticated(uint32_t client_ip, bool authenticated);
bool tollgate_core_dns_is_running(void);

#endif
