#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "esp_wifi.h"

#define TOLLGATE_MAX_WIFI_NETWORKS 5
#define TOLLGATE_MAX_MINT_URLS     3
#define TOLLGATE_MAX_AP_SSID_LEN   32
#define TOLLGATE_MAX_AP_PASS_LEN   64

typedef struct {
    char ssid[32];
    char password[64];
} wifi_network_t;

typedef struct {
    wifi_network_t networks[TOLLGATE_MAX_WIFI_NETWORKS];
    int network_count;
    int current_network;
    int max_retry;

    char ap_ssid[TOLLGATE_MAX_AP_SSID_LEN];
    char ap_password[TOLLGATE_MAX_AP_PASS_LEN];
    uint8_t ap_channel;
    uint8_t ap_max_conn;

    char mint_url[256];
    char lnurl_url[256];
    int price_per_step;
    int step_size_ms;
} tollgate_config_t;

esp_err_t tollgate_config_init(void);
const tollgate_config_t *tollgate_config_get(void);
esp_err_t tollgate_config_get_wifi(wifi_config_t *wifi_config);
esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config);

#endif
