#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "tollgate_platform.h"
#include <stdbool.h>

#include "lightning_payout.h"

#define TOLLGATE_MAX_WIFI_NETWORKS 5
#define TOLLGATE_MAX_MINT_URLS     3
#define TOLLGATE_MAX_AP_SSID_LEN   32
#define TOLLGATE_MAX_AP_PASS_LEN   64
#define TOLLGATE_MAX_RELAYS        4

typedef struct {
    char ssid[32];
    char password[64];
} wifi_network_t;

typedef struct {
    wifi_network_t networks[TOLLGATE_MAX_WIFI_NETWORKS];
    int network_count;
    int current_network;
    int max_retry;

    char nsec[65];
    char npub[65];

    char ap_ssid[TOLLGATE_MAX_AP_SSID_LEN];
    char ap_password[TOLLGATE_MAX_AP_PASS_LEN];
    uint8_t ap_channel;
    uint8_t ap_max_conn;

    uint8_t sta_mac[6];
    uint8_t ap_mac[6];

    esp_ip4_addr_t ap_ip;
    char ap_ip_str[16];

    char mint_url[256];
    char lnurl_url[256];
    int price_per_step;
    int step_size_ms;
    int step_size_bytes;
    char metric[16];
    uint64_t persist_threshold_sats;

    char nostr_geohash[16];
    char nostr_relays[TOLLGATE_MAX_RELAYS][128];
    int nostr_relay_count;
    int nostr_publish_interval_s;

    bool identity_initialized;

    bool client_enabled;
    int client_steps_to_buy;
    int client_renewal_threshold_pct;
    int client_retry_interval_ms;

    payout_config_t payout;

    wifi_auth_mode_t wifi_auth_threshold;

    bool cvm_enabled;
    char cvm_relays[256];
} tollgate_config_t;

void tollgate_config_derive_unique(tollgate_config_t *cfg);

esp_err_t tollgate_config_init(void);
const tollgate_config_t *tollgate_config_get(void);
esp_err_t tollgate_config_get_wifi(wifi_config_t *wifi_config);
esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config);

const tollgate_platform_t *tollgate_get_platform(void);

#endif
