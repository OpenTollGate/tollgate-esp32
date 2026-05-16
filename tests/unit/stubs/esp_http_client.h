#ifndef STUBS_ESP_HTTP_CLIENT_H
#define STUBS_ESP_HTTP_CLIENT_H

#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
} esp_http_client_method_t;

typedef void *esp_crt_bundle_attach_fn_t(void *conf);

typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms;
    esp_crt_bundle_attach_fn_t *crt_bundle_attach;
    int cert_pem;
} esp_http_client_config_t;

typedef void *esp_http_client_handle_t;

static inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    (void)cfg;
    return (void *)malloc(1);
}
static inline esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) {
    (void)c; (void)k; (void)v; return ESP_OK;
}
static inline esp_err_t esp_http_client_open(esp_http_client_handle_t c, int len) {
    (void)c; (void)len; return ESP_OK;
}
static inline int esp_http_client_write(esp_http_client_handle_t c, const char *buf, int len) {
    (void)c; (void)buf; return len;
}
static inline int esp_http_client_fetch_headers(esp_http_client_handle_t c) {
    (void)c; return 0;
}
static inline int esp_http_client_get_status_code(esp_http_client_handle_t c) {
    (void)c; return 200;
}
static inline int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len) {
    (void)c; (void)buf; (void)len; return 0;
}
static inline esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) {
    free(c); return ESP_OK;
}

#endif
