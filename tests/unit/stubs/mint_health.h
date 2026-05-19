#ifndef MINT_HEALTH_H
#define MINT_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define MINT_HEALTH_MAX             8
#define MINT_HEALTH_PROBE_INTERVAL_S  300
#define MINT_HEALTH_PROBE_TIMEOUT_MS  15000
#define MINT_HEALTH_RECOVERY_THRESHOLD 3

typedef struct {
    char url[256];
    bool reachable;
    uint8_t consecutive_successes;
    int64_t last_probe_ms;
    int last_http_status;
} mint_status_t;

typedef void (*mint_health_changed_cb)(void);

static inline bool mint_health_is_reachable(const char *url) {
    (void)url;
    return true;
}

static inline void mint_health_mark_unreachable(const char *url) {
    (void)url;
}

static inline esp_err_t mint_health_init(const char urls[][256], int count) {
    (void)urls; (void)count; return 0;
}

static inline void mint_health_start(void) {}
static inline void mint_health_stop(void) {}
static inline const mint_status_t *mint_health_get_all(int *out_count) {
    *out_count = 0; return NULL;
}
static inline void mint_health_register_callback(mint_health_changed_cb cb) {
    (void)cb;
}

#endif
