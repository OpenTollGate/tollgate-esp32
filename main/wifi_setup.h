#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define WIFI_SETUP_MAX_APS     20
#define WIFI_SETUP_MAX_VISIBLE  8
#define WIFI_SETUP_SSID_LEN    33
#define WIFI_SETUP_PASS_LEN    64

typedef enum {
    SETUP_SCAN,
    SETUP_LIST,
    SETUP_PASSWORD,
    SETUP_CONNECTING,
    SETUP_SUCCESS,
    SETUP_FAILED,
    SETUP_CANCELLED
} setup_state_t;

typedef struct {
    char ssid[WIFI_SETUP_SSID_LEN];
    int rssi;
    bool secured;
} wifi_ap_info_t;

typedef struct {
    setup_state_t state;
    wifi_ap_info_t aps[WIFI_SETUP_MAX_APS];
    int ap_count;
    int list_scroll;
    int selected_ap;
    char selected_ssid[WIFI_SETUP_SSID_LEN];
    char connect_ip[16];
    bool connect_failed_auth;
} wifi_setup_t;

void wifi_setup_init(wifi_setup_t *setup);
void wifi_setup_set_aps(wifi_setup_t *setup, const wifi_ap_info_t *aps, int count);
int wifi_setup_visible_count(const wifi_setup_t *setup);
const wifi_ap_info_t *wifi_setup_get_visible(const wifi_setup_t *setup, int idx);
setup_state_t wifi_setup_handle_select(wifi_setup_t *setup, int list_idx);
setup_state_t wifi_setup_handle_connect(wifi_setup_t *setup);
setup_state_t wifi_setup_handle_connect_result(wifi_setup_t *setup, bool success, const char *ip);
setup_state_t wifi_setup_handle_cancel(wifi_setup_t *setup);
setup_state_t wifi_setup_handle_retry(wifi_setup_t *setup);
setup_state_t wifi_setup_handle_change_network(wifi_setup_t *setup);

#endif
