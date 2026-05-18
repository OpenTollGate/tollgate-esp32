#include "wifi_setup.h"
#include <string.h>

void wifi_setup_init(wifi_setup_t *setup) {
    if (!setup) return;
    memset(setup, 0, sizeof(*setup));
    setup->state = SETUP_SCAN;
    setup->selected_ap = -1;
}

void wifi_setup_set_aps(wifi_setup_t *setup, const wifi_ap_info_t *aps, int count) {
    if (!setup || !aps) return;
    if (count > WIFI_SETUP_MAX_APS) count = WIFI_SETUP_MAX_APS;
    memcpy(setup->aps, aps, count * sizeof(wifi_ap_info_t));
    setup->ap_count = count;
    setup->list_scroll = 0;
    setup->state = SETUP_LIST;
}

int wifi_setup_visible_count(const wifi_setup_t *setup) {
    if (!setup) return 0;
    int remaining = setup->ap_count - setup->list_scroll;
    if (remaining > WIFI_SETUP_MAX_VISIBLE) remaining = WIFI_SETUP_MAX_VISIBLE;
    return remaining < 0 ? 0 : remaining;
}

const wifi_ap_info_t *wifi_setup_get_visible(const wifi_setup_t *setup, int idx) {
    if (!setup || idx < 0 || idx >= wifi_setup_visible_count(setup)) return NULL;
    return &setup->aps[setup->list_scroll + idx];
}

setup_state_t wifi_setup_handle_select(wifi_setup_t *setup, int list_idx) {
    if (!setup || setup->state != SETUP_LIST) return setup ? setup->state : SETUP_CANCELLED;
    if (list_idx < 0 || list_idx >= wifi_setup_visible_count(setup)) return setup->state;

    int real_idx = setup->list_scroll + list_idx;
    setup->selected_ap = real_idx;
    strncpy(setup->selected_ssid, setup->aps[real_idx].ssid, WIFI_SETUP_SSID_LEN - 1);
    setup->selected_ssid[WIFI_SETUP_SSID_LEN - 1] = '\0';
    setup->state = SETUP_PASSWORD;
    return setup->state;
}

setup_state_t wifi_setup_handle_connect(wifi_setup_t *setup) {
    if (!setup || setup->state != SETUP_PASSWORD) return setup ? setup->state : SETUP_CANCELLED;
    setup->state = SETUP_CONNECTING;
    return setup->state;
}

setup_state_t wifi_setup_handle_connect_result(wifi_setup_t *setup, bool success, const char *ip) {
    if (!setup) return SETUP_CANCELLED;
    if (setup->state != SETUP_CONNECTING) return setup->state;

    if (success) {
        setup->state = SETUP_SUCCESS;
        if (ip) {
            strncpy(setup->connect_ip, ip, sizeof(setup->connect_ip) - 1);
            setup->connect_ip[sizeof(setup->connect_ip) - 1] = '\0';
        }
        setup->connect_failed_auth = false;
    } else {
        setup->state = SETUP_FAILED;
        setup->connect_failed_auth = true;
        setup->connect_ip[0] = '\0';
    }
    return setup->state;
}

setup_state_t wifi_setup_handle_cancel(wifi_setup_t *setup) {
    if (!setup) return SETUP_CANCELLED;
    setup->state = SETUP_CANCELLED;
    return setup->state;
}

setup_state_t wifi_setup_handle_retry(wifi_setup_t *setup) {
    if (!setup) return SETUP_CANCELLED;
    if (setup->state != SETUP_FAILED) return setup->state;
    setup->state = SETUP_PASSWORD;
    setup->connect_failed_auth = false;
    return setup->state;
}

setup_state_t wifi_setup_handle_change_network(wifi_setup_t *setup) {
    if (!setup) return SETUP_CANCELLED;
    if (setup->state != SETUP_FAILED) return setup->state;
    setup->state = SETUP_LIST;
    setup->connect_failed_auth = false;
    return setup->state;
}
