#ifndef TOLLGATE_CORE_STRATUM_PROXY_H
#define TOLLGATE_CORE_STRATUM_PROXY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TOLLGATE_STRATUM_MAX_JOB_ID_LEN 32
#define TOLLGATE_STRATUM_MAX_JOBS 4

typedef struct {
    uint32_t job_id;
    uint8_t prevhash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t version;
    uint8_t target[32];
    int target_len;
    bool valid;
} tollgate_stratum_job_t;

typedef struct {
    double hashrate_ghs;
    uint32_t nbits;
    uint64_t total_shares;
    uint64_t total_accepted;
    uint64_t total_rejected;
    double current_hashprice;
    int active_miners;
} tollgate_stratum_proxy_stats_t;

esp_err_t tollgate_core_stratum_proxy_init(uint16_t port);
void tollgate_core_stratum_proxy_set_job(const tollgate_stratum_job_t *job);
const tollgate_stratum_job_t *tollgate_core_stratum_proxy_get_current_job(void);
void tollgate_core_stratum_proxy_get_stats(tollgate_stratum_proxy_stats_t *stats);
void tollgate_core_stratum_proxy_stop(void);

#endif
