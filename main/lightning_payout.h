#ifndef LIGHTNING_PAYOUT_H
#define LIGHTNING_PAYOUT_H

#include "config.h"

esp_err_t lightning_payout_init(const payout_config_t *config);

void lightning_payout_tick(void);

#endif
