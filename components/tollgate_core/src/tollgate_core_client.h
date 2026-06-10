#ifndef TOLLGATE_CORE_CLIENT_H
#define TOLLGATE_CORE_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#define TG_CLIENT_MAX_MINT_URL        256
#define TG_CLIENT_MAX_METRIC          32
#define TG_CLIENT_MAX_GW_IP_LEN      16

typedef enum {
    TG_CLIENT_IDLE,
    TG_CLIENT_DETECTING,
    TG_CLIENT_NO_TOLLGATE,
    TG_CLIENT_NEEDS_PAY,
    TG_CLIENT_PAYING,
    TG_CLIENT_PAID,
    TG_CLIENT_RENEWING,
    TG_CLIENT_MINING,
    TG_CLIENT_ERROR
} tollgate_client_state_t;

typedef struct {
    bool is_tollgate;
    int price_per_step;
    int step_size_ms;
    char mint_url[TG_CLIENT_MAX_MINT_URL];
    char metric[TG_CLIENT_MAX_METRIC];
    bool mining_available;
    uint16_t mining_port;
} tollgate_discovery_t;

typedef struct {
    const char *ssid;
    int price_per_step;
    uint32_t step_size;
} tollgate_client_market_entry_t;

bool tollgate_core_client_parse_discovery(const char *json_str, tollgate_discovery_t *out);

bool tollgate_core_client_parse_session(const char *json_str, int64_t *allotment_ms_out);

bool tollgate_core_client_parse_usage(const char *resp, int64_t *remaining_out, int64_t *total_out);

bool tollgate_core_client_should_renew(int64_t remaining_ms, int64_t allotment_ms, int threshold_pct);

int tollgate_core_client_calc_price_per_min(int price_per_step, int step_size_ms);

int tollgate_core_client_calc_steps(int steps_to_buy, int price_per_step, int discovery_price);

#endif
