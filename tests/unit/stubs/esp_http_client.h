#ifndef STUBS_ESP_HTTP_CLIENT_H
#define STUBS_ESP_HTTP_CLIENT_H

#include "esp_err.h"

typedef void *esp_http_client_handle_t;

typedef struct {
    int cert_pem;
} esp_http_client_config_t;

#endif
