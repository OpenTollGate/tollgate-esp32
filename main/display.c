#include "display.h"
#include "axs15231b.h"
#include "qrcoded.h"
#include "font.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "display";

#define QR_CYCLE_MS 5000

static volatile display_state_t s_state = DISPLAY_BOOT;
static char s_ap_ssid[32] = "";
static char s_portal_url[256] = "";
static int s_active_clients = 0;
static uint64_t s_wallet_balance = 0;
static bool s_initialized = false;
static int64_t s_last_qr_switch = 0;
static display_qr_mode_t s_qr_mode = DISPLAY_QR_WIFI;
static display_state_t s_rendered_state = DISPLAY_BOOT;
static display_qr_mode_t s_rendered_qr_mode = DISPLAY_QR_WIFI;
static bool s_force_render = true;

static int qr_version_from_strlen(int len) {
    if (len <= 17) return 1;
    if (len <= 32) return 2;
    if (len <= 53) return 3;
    if (len <= 78) return 4;
    if (len <= 106) return 5;
    if (len <= 134) return 6;
    if (len <= 154) return 7;
    if (len <= 192) return 8;
    if (len <= 230) return 9;
    if (len <= 271) return 10;
    return 11;
}

static int qr_pixel_size(int len) {
    if (len <= 53) return 4;
    if (len <= 134) return 3;
    return 2;
}

static int escape_wifi_field(const char *src, char *dst, int dst_size) {
    int si = 0, di = 0;
    while (src[si] && di < dst_size - 2) {
        char c = src[si];
        if (c == '\\' || c == ';' || c == ':' || c == ',' || c == '"') {
            if (di + 2 >= dst_size) break;
            dst[di++] = '\\';
            dst[di++] = c;
        } else {
            dst[di++] = c;
        }
        si++;
    }
    dst[di] = '\0';
    return di;
}

static void build_wifi_qr_string(char *out, int out_size) {
    char escaped_ssid[64];
    escape_wifi_field(s_ap_ssid, escaped_ssid, sizeof(escaped_ssid));
    snprintf(out, out_size, "WIFI:S:%s;T:nopass;;", escaped_ssid);
}

void display_render_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale) {
    int cx = x;
    int cy = y;
    int screen_w = axs15231b_get_width();
    int screen_h = axs15231b_get_height();

    while (*text) {
        uint8_t ch = (uint8_t)*text;
        if (ch >= 128) ch = '?';

        if (cx + FONT_GLYPH_W * scale > screen_w) {
            cx = x;
            cy += FONT_GLYPH_H * scale;
        }
        if (cy + FONT_GLYPH_H * scale > screen_h) break;

        const uint8_t *glyph = font8x8_basic[ch];
        for (int row = 0; row < FONT_GLYPH_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_GLYPH_W; col++) {
                uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
                int px = cx + col * scale;
                int py = cy + row * scale;
                if (px < screen_w && py < screen_h) {
                    axs15231b_fill_rect(px, py, scale, scale, color);
                }
            }
        }
        cx += FONT_GLYPH_W * scale;
        text++;
    }
}

static void render_qr_at(const char *text, int x_off, int y_off, int max_w, int max_h) {
    int len = strlen(text);
    int version = qr_version_from_strlen(len);
    int px = qr_pixel_size(len);

    uint16_t buf_size = qrcode_getBufferSize(version);
    uint8_t *qr_buf = (uint8_t *)malloc(buf_size);
    if (!qr_buf) {
        ESP_LOGE(TAG, "Failed to allocate QR buffer");
        return;
    }

    QRCode qr;
    if (qrcode_initText(&qr, qr_buf, version, ECC_LOW, text) != 0) {
        ESP_LOGE(TAG, "QR generation failed");
        free(qr_buf);
        return;
    }

    int qr_px_w = qr.size * px;
    int qr_px_h = qr.size * px;
    int cx = x_off + (max_w - qr_px_w) / 2;
    int cy = y_off + (max_h - qr_px_h) / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;

    for (int y = 0; y < qr.size; y++) {
        for (int x = 0; x < qr.size; x++) {
            bool mod = qrcode_getModule(&qr, x, y);
            uint16_t color = mod ? 0xFFFF : 0x0000;
            axs15231b_fill_rect(cx + x * px, cy + y * px, px, px, color);
        }
    }

    free(qr_buf);
}

void display_render_qr(const char *text) {
    int screen_w = axs15231b_get_width();
    int screen_h = axs15231b_get_height();
    axs15231b_fill_screen(0x0000);
    render_qr_at(text, 0, 0, screen_w, screen_h);
    axs15231b_flush();
}

static void render_boot_screen(void) {
    axs15231b_fill_screen(0x0000);
    display_render_text(96, 220, "TollGate", 0x07FF, 0x0000, 2);
    display_render_text(116, 245, "starting...", 0xFFE0, 0x0000, 1);
    axs15231b_flush();
}

static void render_ready_screen(void) {
    axs15231b_fill_screen(0x0000);

    int screen_w = axs15231b_get_width();
    int screen_h = axs15231b_get_height();
    int text_area_y = screen_h - 55;

    char qr_text[320];
    const char *label;

    if (s_qr_mode == DISPLAY_QR_WIFI) {
        build_wifi_qr_string(qr_text, sizeof(qr_text));
        label = "Scan to connect";
    } else {
        strncpy(qr_text, s_portal_url, sizeof(qr_text) - 1);
        qr_text[sizeof(qr_text) - 1] = '\0';
        label = "Portal URL";
    }

    render_qr_at(qr_text, 0, 0, screen_w, text_area_y - 5);

    display_render_text(10, text_area_y, label, 0xB5B6, 0x0000, 2);

    char line[64];
    snprintf(line, sizeof(line), "SSID: %s", s_ap_ssid);
    display_render_text(10, text_area_y + 20, line, 0xB5B6, 0x0000, 2);

    axs15231b_flush();
}

static void render_payment_screen(void) {
    axs15231b_fill_screen(0x07E0);
    display_render_text(128, 225, "Paid!", 0x0000, 0x07E0, 2);
    display_render_text(104, 245, "Access granted", 0x0000, 0x07E0, 1);
    axs15231b_flush();
}

static void render_error_screen(void) {
    axs15231b_fill_screen(0xF800);
    display_render_text(104, 225, "No upstream", 0xFFFF, 0xF800, 2);
    display_render_text(120, 245, "Check config", 0xFFFF, 0xF800, 1);
    axs15231b_flush();
}

static void display_task(void *pvParameters) {
    ESP_LOGI(TAG, "Display task started");

    while (1) {
        display_state_t state = s_state;

        if (state == DISPLAY_READY) {
            int64_t now = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((now - s_last_qr_switch) >= QR_CYCLE_MS) {
                s_qr_mode = (s_qr_mode == DISPLAY_QR_WIFI) ? DISPLAY_QR_PORTAL : DISPLAY_QR_WIFI;
                s_last_qr_switch = now;
            }
        }

        switch (state) {
            case DISPLAY_BOOT:
                render_boot_screen();
                break;
            case DISPLAY_READY:
                render_ready_screen();
                break;
            case DISPLAY_PAYMENT_RECEIVED:
                render_payment_screen();
                vTaskDelay(pdMS_TO_TICKS(2000));
                s_state = DISPLAY_READY;
                break;
            case DISPLAY_ERROR:
                render_error_screen();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t display_init(void) {
    if (s_initialized) return ESP_OK;

    esp_err_t ret = axs15231b_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display hardware init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    s_last_qr_switch = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

    xTaskCreatePinnedToCore(display_task, "display", 16384, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "Display initialized");
    return ESP_OK;
}

void display_set_state(display_state_t state) {
    s_state = state;
    s_force_render = true;
}

void display_update(const char *ap_ssid, int active_clients,
                    uint64_t wallet_balance, const char *portal_url) {
    if (ap_ssid) {
        strncpy(s_ap_ssid, ap_ssid, sizeof(s_ap_ssid) - 1);
        s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';
    }
    if (portal_url) {
        strncpy(s_portal_url, portal_url, sizeof(s_portal_url) - 1);
        s_portal_url[sizeof(s_portal_url) - 1] = '\0';
    }
    s_active_clients = active_clients;
    s_wallet_balance = wallet_balance;
}
