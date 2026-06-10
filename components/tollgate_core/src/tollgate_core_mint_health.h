#ifndef TOLLGATE_CORE_MINT_HEALTH_H
#define TOLLGATE_CORE_MINT_HEALTH_H

#include <stdint.h>
#include <stdbool.h>

#define TG_MINT_HEALTH_MAX             8
#define TG_MINT_HEALTH_RECOVERY_THRESHOLD 3

typedef struct {
    char url[256];
    bool reachable;
    uint8_t consecutive_successes;
    int64_t last_probe_ms;
    int last_http_status;
    int last_err;
} tollgate_mint_status_t;

typedef struct {
    tollgate_mint_status_t mints[TG_MINT_HEALTH_MAX];
    int count;
} tollgate_mint_health_t;

typedef void (*tollgate_mint_health_changed_fn)(int old_reachable, int new_reachable);

void tollgate_core_mint_health_init(const char urls[][256], int count);

void tollgate_core_mint_health_update(tollgate_mint_health_t *state,
                                       int mint_index,
                                       bool probe_ok,
                                       int http_status,
                                       int err_code,
                                       int64_t probe_time_ms);

void tollgate_core_mint_health_update_initial(tollgate_mint_health_t *state,
                                               int mint_index,
                                               bool probe_ok,
                                               int http_status,
                                               int err_code,
                                               int64_t probe_time_ms);

bool tollgate_core_mint_health_is_reachable(const tollgate_mint_health_t *state,
                                              const char *url);

void tollgate_core_mint_health_mark_unreachable(tollgate_mint_health_t *state,
                                                  const char *url);

int tollgate_core_mint_health_count_reachable(const tollgate_mint_health_t *state);

#endif
