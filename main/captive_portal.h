#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "esp_http_server.h"
#include "esp_err.h"

esp_err_t captive_portal_start(const char *ap_ip_str);
void captive_portal_stop(void);
httpd_handle_t captive_portal_get_server(void);

#endif
