#ifndef STUBS_ESP_WIFI_AP_GET_STA_LIST_H
#define STUBS_ESP_WIFI_AP_GET_STA_LIST_H

#include <stdint.h>
#include <string.h>
#include "esp_err.h"

#define ESP_WIFI_AP_MAX_STA 10

typedef struct {
    uint8_t mac[6];
} wifi_sta_info_t;

typedef struct {
    int num;
    wifi_sta_info_t sta[ESP_WIFI_AP_MAX_STA];
} wifi_sta_list_t;

typedef struct {
    int num;
    struct {
        uint8_t mac[6];
        esp_ip4_addr_t ip;
    } sta[ESP_WIFI_AP_MAX_STA];
} wifi_sta_mac_ip_list_t;

static inline esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta) {
    memset(sta, 0, sizeof(*sta));
    return ESP_FAIL;
}

static inline esp_err_t esp_wifi_ap_get_sta_list_with_ip(const wifi_sta_list_t *sta_in, wifi_sta_mac_ip_list_t *out) {
    (void)sta_in;
    memset(out, 0, sizeof(*out));
    return ESP_FAIL;
}

#endif
