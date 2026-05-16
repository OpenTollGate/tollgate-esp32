#ifndef STUBS_ESP_TLS_H
#define STUBS_ESP_TLS_H

#include "esp_err.h"

typedef struct esp_tls esp_tls_t;

typedef struct {
    void *crt_bundle_attach;
    int use_global_ca_store;
} esp_tls_cfg_t;

static inline esp_tls_t *esp_tls_init(void) { return (esp_tls_t*)1; }
static inline int esp_tls_conn_new_sync(const char *h, int hl, int port, const esp_tls_cfg_t *cfg, esp_tls_t *tls) {
    (void)h; (void)hl; (void)port; (void)cfg; (void)tls; return -1;
}
static inline int esp_tls_conn_write(esp_tls_t *tls, const void *data, size_t len) {
    (void)tls; (void)data; (void)len; return len;
}
static inline int esp_tls_conn_read(esp_tls_t *tls, void *data, size_t len) {
    (void)tls; (void)data; (void)len; return 0;
}
static inline void esp_tls_conn_destroy(esp_tls_t *tls) { (void)tls; }

#endif
