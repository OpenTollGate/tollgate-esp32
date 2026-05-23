#ifndef TOLLGATE_CLIENT_H
#define TOLLGATE_CLIENT_H

#include "tollgate_core_client.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t tollgate_client_init(void);

esp_err_t tollgate_client_on_sta_connected(const char *gw_ip_str);

void tollgate_client_on_sta_disconnected(void);

void tollgate_client_tick(void);

tollgate_client_state_t tollgate_client_get_state(void);

const tollgate_discovery_t *tollgate_client_get_discovery(void);

int64_t tollgate_client_get_remaining_ms(void);
int64_t tollgate_client_get_allotment_ms(void);

#endif
