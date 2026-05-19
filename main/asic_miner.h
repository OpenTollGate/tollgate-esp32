#ifndef ASIC_MINER_H
#define ASIC_MINER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t asic_miner_init(void);
bool asic_miner_is_present(void);
esp_err_t asic_miner_start(void);
void asic_miner_stop(void);
double asic_miner_get_hashrate(void);

#endif
