#ifndef LIGHTNING_PAYOUT_H
#define LIGHTNING_PAYOUT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define PAYOUT_MAX_RECIPIENTS     4
#define PAYOUT_MAX_MINTS          3
#define PAYOUT_MAX_ADDR_LEN       128

typedef struct {
    char lightning_address[PAYOUT_MAX_ADDR_LEN];
    double factor;
} payout_recipient_t;

typedef struct {
    char url[256];
    uint64_t min_balance;
    uint64_t min_payout_amount;
} payout_mint_config_t;

typedef struct {
    bool enabled;
    payout_mint_config_t mints[PAYOUT_MAX_MINTS];
    int mint_count;
    payout_recipient_t recipients[PAYOUT_MAX_RECIPIENTS];
    int recipient_count;
    uint64_t fee_tolerance_pct;
    int check_interval_s;
} payout_config_t;

esp_err_t lightning_payout_init(const payout_config_t *config);

void lightning_payout_tick(void);

#endif
