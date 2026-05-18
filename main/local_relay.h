#ifndef LOCAL_RELAY_H
#define LOCAL_RELAY_H

#include "esp_err.h"
#include <stddef.h>

esp_err_t local_relay_init(void);
void local_relay_start(void);
void local_relay_stop(void);

esp_err_t local_relay_publish(const char *event_json, size_t event_len);

#endif
