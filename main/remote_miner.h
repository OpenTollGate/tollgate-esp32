#ifndef REMOTE_MINER_H
#define REMOTE_MINER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t remote_miner_start(const char *gw_ip);
void remote_miner_stop(void);
bool remote_miner_is_running(void);
double remote_miner_get_hashrate(void);

#endif
