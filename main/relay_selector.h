#ifndef RELAY_SELECTOR_H
#define RELAY_SELECTOR_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

#define RELAY_SELECTOR_MAX_RELAYS 8
#define RELAY_SELECTOR_URL_LEN   128

typedef struct {
    char url[RELAY_SELECTOR_URL_LEN];
    char name[64];
    uint32_t latency_ms;
    bool supports_nip77;
    bool alive;
    int consecutive_failures;
    uint32_t last_probe_time;
    uint8_t supported_nips[32];
    size_t nips_count;
} relay_info_t;

typedef struct {
    relay_info_t relays[RELAY_SELECTOR_MAX_RELAYS];
    size_t count;
    int primary_idx;
    int fallback_idx;
    uint32_t last_full_probe;
    SemaphoreHandle_t lock;
} relay_selector_t;

esp_err_t relay_selector_init(relay_selector_t *sel);
void relay_selector_destroy(relay_selector_t *sel);

esp_err_t relay_selector_probe_all(relay_selector_t *sel);

const relay_info_t *relay_selector_get_primary(relay_selector_t *sel);
const relay_info_t *relay_selector_get_fallback(relay_selector_t *sel, int idx);

void relay_selector_report_disconnect(relay_selector_t *sel, const char *url);

esp_err_t relay_selector_seed_from_config(relay_selector_t *sel);

#endif
