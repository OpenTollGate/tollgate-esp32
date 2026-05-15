#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

esp_err_t dns_server_start(esp_ip4_addr_t ap_ip, esp_ip4_addr_t upstream_dns);
void dns_server_stop(void);
void dns_server_set_client_authenticated(uint32_t client_ip, bool authenticated);
bool dns_server_is_running(void);

#endif
