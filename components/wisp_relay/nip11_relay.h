#ifndef NIP11_RELAY_H
#define NIP11_RELAY_H

#include "esp_http_server.h"

esp_err_t relay_nip11_handler(httpd_req_t *req);
esp_err_t relay_nip11_options_handler(httpd_req_t *req);

#endif
