#ifndef STUBS_ESP_WIFI_H
#define STUBS_ESP_WIFI_H

#include <stdint.h>
#include <stdbool.h>
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

#define WIFI_VENDOR_IE_ELEMENT_ID 0xDD

typedef enum {
    WIFI_VND_IE_TYPE_BEACON,
    WIFI_VND_IE_TYPE_PROBE_REQ,
    WIFI_VND_IE_TYPE_PROBE_RESP,
    WIFI_VND_IE_TYPE_ASSOC_REQ,
    WIFI_VND_IE_TYPE_ASSOC_RESP,
} wifi_vendor_ie_type_t;

typedef enum {
    WIFI_VND_IE_ID_0,
    WIFI_VND_IE_ID_1,
} wifi_vendor_ie_id_t;

typedef struct {
    uint8_t element_id;
    uint8_t length;
    uint8_t vendor_oui[3];
    uint8_t vendor_oui_type;
    uint8_t payload[0];
} vendor_ie_data_t;

typedef void (*esp_vendor_ie_cb_t)(void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi);

static inline esp_err_t esp_wifi_set_vendor_ie(bool enable, wifi_vendor_ie_type_t type, wifi_vendor_ie_id_t idx, const void *vnd_ie) { (void)enable; (void)type; (void)idx; (void)vnd_ie; return ESP_OK; }
static inline esp_err_t esp_wifi_set_vendor_ie_cb(esp_vendor_ie_cb_t cb, void *ctx) { (void)cb; (void)ctx; return ESP_OK; }

#define WIFI_SCAN_TYPE_PASSIVE 0

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    int second;
    int8_t rssi;
    int authmode;
} wifi_ap_record_t;

typedef struct {
    uint8_t *ssid;
    uint8_t *bssid;
    uint8_t channel;
    bool show_hidden;
    int scan_type;
    union {
        struct { int min; int max; } active;
        int passive;
    } scan_time;
} wifi_scan_config_t;

static inline esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *cfg, bool block) { (void)cfg; (void)block; return ESP_OK; }
static inline esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { *n = 0; return ESP_OK; }
static inline esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *records) { (void)records; *n = 0; return ESP_OK; }

#define WIFI_EVENT_SCAN_DONE 3

typedef void *esp_event_handler_instance_t;
typedef const char *esp_event_base_t;
#define WIFI_EVENT "WIFI_EVENT"

static inline esp_err_t esp_event_handler_instance_register(esp_event_base_t a, int32_t b, void *c, void *d, esp_event_handler_instance_t *e) { (void)a; (void)b; (void)c; (void)d; (void)e; return ESP_OK; }

#endif
