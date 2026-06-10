#ifndef TOLLGATE_CORE_H
#define TOLLGATE_CORE_H

#include "tollgate_platform.h"
#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tollgate_core_init(const tollgate_platform_t *platform, esp_ip4_addr_t ap_ip);

esp_err_t tollgate_core_dns_start(esp_ip4_addr_t upstream_dns);
void tollgate_core_dns_stop(void);

esp_err_t tollgate_core_process_payment(uint32_t client_ip, const char *token_str);

void tollgate_core_client_connected(const uint8_t *mac, uint32_t client_ip);
void tollgate_core_client_disconnected(const uint8_t *mac);

void tollgate_core_tick(void);

bool tollgate_core_is_client_allowed(uint32_t client_ip);
bool tollgate_core_is_dns_running(void);

char *tollgate_core_get_status_json(void);
char *tollgate_core_get_config_json(void);

int tollgate_core_active_session_count(void);
int tollgate_core_allowed_client_count(void);

bool tollgate_core_is_owner(uint32_t client_ip);
bool tollgate_core_is_owner_connected(void);

esp_err_t tollgate_core_stratum_proxy_init(uint16_t port, bool self_test);
void tollgate_core_stratum_proxy_stop(void);
void tollgate_core_on_share_accepted(uint32_t client_ip, double difficulty);
double tollgate_core_calc_hashprice(double hashrate_ghs);
char *tollgate_core_get_mining_status_json(void);

const tollgate_platform_t *tollgate_core_get_platform(void);

#ifdef __cplusplus
}
#endif

#endif
