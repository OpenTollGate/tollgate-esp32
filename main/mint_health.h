#ifndef MINT_HEALTH_H
#define MINT_HEALTH_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define MINT_HEALTH_MAX             8
#define MINT_HEALTH_PROBE_INTERVAL_S  30
#define MINT_HEALTH_PROBE_TIMEOUT_MS  15000
#define MINT_HEALTH_RECOVERY_THRESHOLD 3

typedef struct {
    char url[256];
    bool reachable;
    uint8_t consecutive_successes;
    int64_t last_probe_ms;
    int last_http_status;
} mint_status_t;

typedef void (*mint_health_changed_cb)(void);

esp_err_t mint_health_init(const char urls[][256], int count);
void mint_health_start(void);
void mint_health_stop(void);
const mint_status_t *mint_health_get_all(int *out_count);
bool mint_health_is_reachable(const char *url);
void mint_health_mark_unreachable(const char *url);
void mint_health_register_callback(mint_health_changed_cb cb);

#endif
