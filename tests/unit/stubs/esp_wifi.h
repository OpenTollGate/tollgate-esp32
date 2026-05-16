#ifndef STUBS_ESP_WIFI_H
#define STUBS_ESP_WIFI_H

#include <stdint.h>
#include <string.h>
#include "esp_err.h"

#define WIFI_IF_STA 0
#define WIFI_IF_AP  1

#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_OPEN     0

#define WIFI_MODE_APSTA 3

typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        uint8_t channel;
        uint8_t max_connection;
        uint8_t ssid_hidden;
        int authmode;
    } ap;
    struct {
        uint8_t ssid[32];
        uint8_t password[64];
        int threshold;
        struct {
            int authmode;
        } sta;
    } sta;
} wifi_config_t;

static inline esp_err_t esp_wifi_set_mac(int ifx, const uint8_t *mac) { (void)ifx; (void)mac; return ESP_OK; }
static inline esp_err_t esp_wifi_set_config(int ifx, const wifi_config_t *cfg) { (void)ifx; (void)cfg; return ESP_OK; }
static inline esp_err_t esp_wifi_set_mode(uint8_t mode) { (void)mode; return ESP_OK; }
static inline esp_err_t esp_wifi_start(void) { return ESP_OK; }

#endif
