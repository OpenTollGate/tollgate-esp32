#include "tollgate_core_mint_health.h"
#include <string.h>

void tollgate_core_mint_health_init(const char urls[][256], int count)
{
}

void tollgate_core_mint_health_update(tollgate_mint_health_t *state,
                                       int mint_index,
                                       bool probe_ok,
                                       int http_status,
                                       int err_code,
                                       int64_t probe_time_ms)
{
    if (!state || mint_index < 0 || mint_index >= state->count) return;

    tollgate_mint_status_t *m = &state->mints[mint_index];
    m->last_probe_ms = probe_time_ms;
    m->last_http_status = probe_ok ? http_status : 0;
    m->last_err = probe_ok ? 0 : err_code;

    if (probe_ok) {
        m->consecutive_successes++;
        if (m->consecutive_successes >= TG_MINT_HEALTH_RECOVERY_THRESHOLD) {
            m->reachable = true;
        }
    } else {
        m->reachable = false;
        m->consecutive_successes = 0;
    }
}

void tollgate_core_mint_health_update_initial(tollgate_mint_health_t *state,
                                               int mint_index,
                                               bool probe_ok,
                                               int http_status,
                                               int err_code,
                                               int64_t probe_time_ms)
{
    if (!state || mint_index < 0 || mint_index >= state->count) return;

    tollgate_mint_status_t *m = &state->mints[mint_index];
    m->last_probe_ms = probe_time_ms;
    m->last_http_status = probe_ok ? http_status : 0;
    m->last_err = probe_ok ? 0 : err_code;

    if (probe_ok) {
        m->consecutive_successes = TG_MINT_HEALTH_RECOVERY_THRESHOLD;
        m->reachable = true;
    } else {
        m->consecutive_successes = 0;
        m->reachable = false;
    }
}

bool tollgate_core_mint_health_is_reachable(const tollgate_mint_health_t *state,
                                              const char *url)
{
    if (!state || !url) return false;
    for (int i = 0; i < state->count; i++) {
        if (strcmp(state->mints[i].url, url) == 0 || strstr(url, state->mints[i].url) != NULL) {
            return state->mints[i].reachable;
        }
    }
    return false;
}

void tollgate_core_mint_health_mark_unreachable(tollgate_mint_health_t *state,
                                                  const char *url)
{
    if (!state || !url) return;
    for (int i = 0; i < state->count; i++) {
        if (strcmp(state->mints[i].url, url) == 0 || strstr(url, state->mints[i].url) != NULL) {
            if (state->mints[i].reachable) {
                state->mints[i].reachable = false;
                state->mints[i].consecutive_successes = 0;
            }
            break;
        }
    }
}

int tollgate_core_mint_health_count_reachable(const tollgate_mint_health_t *state)
{
    if (!state) return 0;
    int count = 0;
    for (int i = 0; i < state->count; i++) {
        if (state->mints[i].reachable) count++;
    }
    return count;
}
