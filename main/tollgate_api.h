#ifndef TOLLGATE_API_H
#define TOLLGATE_API_H

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t tollgate_api_start(void);
void tollgate_api_stop(void);
void tls_worker_set_queue(QueueHandle_t q);

#endif
