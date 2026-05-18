#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DISPLAY_BOOT,
    DISPLAY_READY,
    DISPLAY_PAYMENT_RECEIVED,
    DISPLAY_ERROR,
    DISPLAY_WIFI_SETUP
} display_state_t;

typedef enum {
    DISPLAY_QR_WIFI,
    DISPLAY_QR_PORTAL
} display_qr_mode_t;

esp_err_t display_init(void);
void display_set_state(display_state_t state);
void display_update(const char *ap_ssid, int active_clients,
                    uint64_t wallet_balance, const char *portal_url,
                    const char *mint_url, int price_per_step,
                    const char *wifi_status);
void display_notify_payment(int amount_sats, int64_t allotment_ms);
void display_notify_wifi_connected(const char *ip);
void display_notify_wifi_disconnected(void);
void display_enter_wifi_setup(void);
void display_render_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
void display_render_qr(const char *text);

#endif
