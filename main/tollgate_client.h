#ifndef TOLLGATE_CLIENT_H
#define TOLLGATE_CLIENT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TG_CLIENT_MAX_GW_IP_LEN       16
#define TG_CLIENT_MAX_MINT_URL        256
#define TG_CLIENT_MAX_METRIC          32

typedef enum {
    TG_CLIENT_IDLE,
    TG_CLIENT_DETECTING,
    TG_CLIENT_NO_TOLLGATE,
    TG_CLIENT_NEEDS_PAY,
    TG_CLIENT_PAYING,
    TG_CLIENT_PAID,
    TG_CLIENT_RENEWING,
    TG_CLIENT_ERROR
} tollgate_client_state_t;

typedef struct {
    bool is_tollgate;
    int price_per_step;
    int step_size_ms;
    char mint_url[TG_CLIENT_MAX_MINT_URL];
    char metric[TG_CLIENT_MAX_METRIC];
} tollgate_discovery_t;

esp_err_t tollgate_client_init(void);

esp_err_t tollgate_client_on_sta_connected(const char *gw_ip_str);

void tollgate_client_on_sta_disconnected(void);

void tollgate_client_tick(void);

tollgate_client_state_t tollgate_client_get_state(void);

const tollgate_discovery_t *tollgate_client_get_discovery(void);

int64_t tollgate_client_get_remaining_ms(void);
int64_t tollgate_client_get_allotment_ms(void);

#endif
