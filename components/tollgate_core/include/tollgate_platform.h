#ifndef TOLLGATE_PLATFORM_H
#define TOLLGATE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t     (*get_price_sats)(void);
    int32_t      (*get_step_ms)(void);
    const char * (*get_mint_url)(void);
    const char * (*get_metric)(void);
    int32_t      (*get_step_bytes)(void);
    int64_t      (*get_time_ms)(void);
    bool         (*spend_proofs)(const char *raw_token_json);

    const char * (*get_stratum_url)(void);
    uint16_t     (*get_stratum_port)(void);
    const char * (*get_stratum_user)(void);
    const char * (*get_stratum_pass)(void);
    const char * (*get_stratum_fallback_url)(void);
    uint16_t     (*get_stratum_fallback_port)(void);
    uint16_t     (*get_mining_port)(void);
    const char * (*get_mining_payout_mode)(void);
    uint64_t     (*get_hashprice_sats_per_ghs_day)(void);
    void         (*on_share_accepted)(double difficulty);
    double       (*get_hashrate)(void);
} tollgate_platform_t;

#ifdef __cplusplus
}
#endif

#endif
