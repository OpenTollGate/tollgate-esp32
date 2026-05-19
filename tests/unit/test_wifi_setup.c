#include "test_framework.h"
#include "../../main/wifi_setup.h"
#include <string.h>

int main(void)
{
    printf("=== test_wifi_setup ===\n");

    wifi_setup_t setup;
    wifi_setup_init(&setup);

    ASSERT_EQ_INT(SETUP_SCAN, (int)setup.state, "Init state = SCAN");
    ASSERT_EQ_INT(0, setup.ap_count, "Init ap_count = 0");
    ASSERT_EQ_INT(-1, setup.selected_ap, "Init selected_ap = -1");

    wifi_ap_info_t test_aps[3] = {
        {"FastNet",     -30, true},
        {"SlowNet",     -70, true},
        {"OpenNet",     -50, false},
    };
    wifi_setup_set_aps(&setup, test_aps, 3);

    ASSERT_EQ_INT(SETUP_LIST, (int)setup.state, "After set_aps: state = LIST");
    ASSERT_EQ_INT(3, setup.ap_count, "After set_aps: ap_count = 3");
    ASSERT_EQ_INT(3, wifi_setup_visible_count(&setup), "Visible count = 3");

    {
        const wifi_ap_info_t *ap = wifi_setup_get_visible(&setup, 0);
        ASSERT(ap != NULL, "Visible AP 0 is not NULL");
        ASSERT_EQ_STR("FastNet", ap->ssid, "AP 0 = FastNet");
        ASSERT_EQ_INT(-30, ap->rssi, "AP 0 RSSI = -30");
        ASSERT(ap->secured, "AP 0 is secured");
    }

    {
        const wifi_ap_info_t *ap = wifi_setup_get_visible(&setup, 2);
        ASSERT(ap != NULL, "Visible AP 2 is not NULL");
        ASSERT_EQ_STR("OpenNet", ap->ssid, "AP 2 = OpenNet");
        ASSERT(!ap->secured, "AP 2 is open");
    }

    {
        const wifi_ap_info_t *ap = wifi_setup_get_visible(&setup, 3);
        ASSERT(ap == NULL, "Out of range returns NULL");
    }

    setup_state_t s = wifi_setup_handle_select(&setup, 0);
    ASSERT_EQ_INT(SETUP_PASSWORD, (int)s, "Select AP 0: state = PASSWORD");
    ASSERT_EQ_INT(0, setup.selected_ap, "Selected AP index = 0");
    ASSERT_EQ_STR("FastNet", setup.selected_ssid, "Selected SSID = FastNet");

    wifi_setup_handle_connect(&setup);
    ASSERT_EQ_INT(SETUP_CONNECTING, (int)setup.state, "Connect: state = CONNECTING");

    s = wifi_setup_handle_connect_result(&setup, true, "192.168.1.42");
    ASSERT_EQ_INT(SETUP_SUCCESS, (int)s, "Connect success: state = SUCCESS");
    ASSERT_EQ_STR("192.168.1.42", setup.connect_ip, "Connect IP stored");

    wifi_setup_init(&setup);
    wifi_setup_set_aps(&setup, test_aps, 3);
    wifi_setup_handle_select(&setup, 1);
    wifi_setup_handle_connect(&setup);

    s = wifi_setup_handle_connect_result(&setup, false, NULL);
    ASSERT_EQ_INT(SETUP_FAILED, (int)s, "Connect fail: state = FAILED");
    ASSERT(setup.connect_failed_auth, "Failed auth flag set");

    s = wifi_setup_handle_retry(&setup);
    ASSERT_EQ_INT(SETUP_PASSWORD, (int)s, "Retry: state = PASSWORD");
    ASSERT(!setup.connect_failed_auth, "Retry clears auth flag");

    wifi_setup_init(&setup);
    wifi_setup_set_aps(&setup, test_aps, 3);
    wifi_setup_handle_select(&setup, 0);
    wifi_setup_handle_connect(&setup);
    wifi_setup_handle_connect_result(&setup, false, NULL);

    s = wifi_setup_handle_change_network(&setup);
    ASSERT_EQ_INT(SETUP_LIST, (int)s, "Change network: state = LIST");

    wifi_setup_init(&setup);
    s = wifi_setup_handle_cancel(&setup);
    ASSERT_EQ_INT(SETUP_CANCELLED, (int)s, "Cancel: state = CANCELLED");

    wifi_setup_init(&setup);
    wifi_ap_info_t many_aps[12];
    for (int i = 0; i < 12; i++) {
        snprintf(many_aps[i].ssid, sizeof(many_aps[i].ssid), "Net%d", i);
        many_aps[i].rssi = -30 - i * 5;
        many_aps[i].secured = true;
    }
    wifi_setup_set_aps(&setup, many_aps, 12);
    ASSERT_EQ_INT(12, setup.ap_count, "12 APs stored");
    ASSERT_EQ_INT(8, wifi_setup_visible_count(&setup), "Only 8 visible");

    {
        const wifi_ap_info_t *ap = wifi_setup_get_visible(&setup, 7);
        ASSERT(ap != NULL, "8th visible AP exists");
        ASSERT_EQ_STR("Net7", ap->ssid, "8th visible = Net7");
    }

    wifi_setup_init(&setup);
    s = wifi_setup_handle_select(&setup, 0);
    ASSERT_EQ_INT(SETUP_SCAN, (int)s, "Select in SCAN state = no change");

    s = wifi_setup_handle_select(&setup, -1);
    ASSERT_EQ_INT(SETUP_SCAN, (int)s, "Select invalid idx = no change");

    wifi_setup_init(&setup);
    wifi_setup_set_aps(&setup, test_aps, 3);
    s = wifi_setup_handle_retry(&setup);
    ASSERT_EQ_INT(SETUP_LIST, (int)s, "Retry in LIST state = no change");

    s = wifi_setup_handle_change_network(&setup);
    ASSERT_EQ_INT(SETUP_LIST, (int)s, "Change network in LIST state = no change");

    ASSERT_EQ_INT(0, wifi_setup_visible_count(NULL), "NULL setup returns 0");
    ASSERT(NULL == wifi_setup_get_visible(NULL, 0), "NULL setup returns NULL AP");

    TEST_SUMMARY();
}
