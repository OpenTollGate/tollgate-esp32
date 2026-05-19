#ifndef TOLLGATE_PLATFORM_H
#define TOLLGATE_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t     (*get_price_sats)(void);
    int32_t      (*get_step_ms)(void);
    const char * (*get_mint_url)(void);
    const char * (*get_metric)(void);
    int32_t      (*get_step_bytes)(void);
    int64_t      (*get_time_ms)(void);
    bool         (*spend_proofs)(const char *raw_token_json);
} tollgate_platform_t;

#endif
