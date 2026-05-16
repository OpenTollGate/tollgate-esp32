#ifndef WIFISTR_H
#define WIFISTR_H

#include "esp_err.h"

esp_err_t wifistr_publish(void);

void wifistr_start_periodic(int interval_s);

#endif
