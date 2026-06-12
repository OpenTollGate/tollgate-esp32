#ifndef STUB_REMOTE_MINER_H
#define STUB_REMOTE_MINER_H

#include "esp_err.h"
#include <stdbool.h>

static inline esp_err_t remote_miner_start(const char *gw_ip) { (void)gw_ip; return ESP_OK; }
static inline void remote_miner_stop(void) {}
static inline bool remote_miner_is_running(void) { return false; }
static inline double remote_miner_get_hashrate(void) { return 0.0; }

#endif
