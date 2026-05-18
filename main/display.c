#include "display.h"
#include "axs15231b.h"
#include "qrcoded.h"
#include "font.h"
#include "nucula_wallet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "display";

#define QR_CYCLE_MS 5000
#define RENDER_INTERVAL_MS 2000

#define COLOR_BG       0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_CYAN     0x07FF
#define COLOR_YELLOW   0xFFE0
#define COLOR_GREEN    0x07E0
#define COLOR_ORANGE   0xFD20
#define COLOR_RED      0xF800
#define COLOR_DIM      0x8410

static volatile display_state_t s_state = DISPLAY_BOOT;
static char s_ap_ssid[32] = "";
static char s_portal_url[256] = "";
static char s_mint_url[256] = "";
static char s_wifi_status[32] = "starting...";
static int s_active_clients = 0;
static uint64_t s_wallet_balance = 0;
static int s_price_per_step = 0;
static bool s_initialized = false;
static int64_t s_last_qr_switch = 0;
static display_qr_mode_t s_qr_mode = DISPLAY_QR_WIFI;
static int s_last_payment_sats = 0;
static int64_t s_last_allotment_ms = 0;

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

static void extract_domain(const char *url, char *domain, int domain_size) {
    const char *start = url;
    if (strncmp(url, "https://", 8) == 0) start = url + 8;
    else if (strncmp(url, "http://", 7) == 0) start = url + 7;
    strncpy(domain, start, domain_size - 1);
    domain[domain_size - 1] = '\0';
    char *slash = strchr(domain, '/');
    if (slash) *slash = '\0';
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

static uint16_t wallet_color(void) {
    if (s_wallet_balance == 0) return COLOR_RED;
    if (s_wallet_balance < 100) return COLOR_YELLOW;
    return COLOR_GREEN;
}

static void render_boot_screen(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    const char *title = "TollGate";
    int title_w = strlen(title) * 8 * 2;
    display_render_text((screen_w - title_w) / 2, 200, title, COLOR_CYAN, COLOR_BG, 2);

    int status_w = strlen(s_wifi_status) * 8;
    display_render_text((screen_w - status_w) / 2, 228, s_wifi_status, COLOR_YELLOW, COLOR_BG, 1);

    axs15231b_flush();
}

static void render_ready_screen(void) {
    int screen_w = axs15231b_get_width();
    int text_area_y = 330;
    axs15231b_fill_screen(COLOR_BG);

    char qr_text[320];
    if (s_qr_mode == DISPLAY_QR_WIFI) {
        build_wifi_qr_string(qr_text, sizeof(qr_text));
    } else {
        strncpy(qr_text, s_portal_url, sizeof(qr_text) - 1);
        qr_text[sizeof(qr_text) - 1] = '\0';
    }

    render_qr_at(qr_text, 0, 5, screen_w, text_area_y - 10);

    int y = text_area_y;
    char line[48];

    if (s_qr_mode == DISPLAY_QR_WIFI) {
        snprintf(line, sizeof(line), "Scan to connect");
        display_render_text(10, y, line, COLOR_CYAN, COLOR_BG, 1);
        y += 16;

        snprintf(line, sizeof(line), "SSID: %s", s_ap_ssid);
        display_render_text(10, y, line, COLOR_WHITE, COLOR_BG, 1);
        y += 16;
    } else {
        snprintf(line, sizeof(line), "Portal URL");
        display_render_text(10, y, line, COLOR_CYAN, COLOR_BG, 1);
        y += 16;

        char domain[48];
        extract_domain(s_mint_url, domain, sizeof(domain));
        snprintf(line, sizeof(line), "Mint: %.30s", domain);
        display_render_text(10, y, line, COLOR_ORANGE, COLOR_BG, 1);
        y += 16;
    }

    snprintf(line, sizeof(line), "%d sats/min", s_price_per_step);
    display_render_text(10, y, line, COLOR_ORANGE, COLOR_BG, 1);
    y += 16;

    snprintf(line, sizeof(line), "Wallet: %llu sats", (unsigned long long)s_wallet_balance);
    display_render_text(10, y, line, wallet_color(), COLOR_BG, 1);
    y += 16;

    if (s_active_clients > 0) {
        snprintf(line, sizeof(line), "Clients: %d", s_active_clients);
        display_render_text(10, y, line, COLOR_GREEN, COLOR_BG, 1);
    }

    axs15231b_flush();
}

static void render_payment_screen(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    axs15231b_fill_rect(0, 190, screen_w, 50, COLOR_GREEN);
    const char *msg = "ACCESS GRANTED";
    int msg_w = strlen(msg) * 8 * 2;
    display_render_text((screen_w - msg_w) / 2, 202, msg, COLOR_WHITE, COLOR_GREEN, 2);

    char line[48];

    snprintf(line, sizeof(line), "Paid: %d sats", s_last_payment_sats);
    int lw = strlen(line) * 8;
    display_render_text((screen_w - lw) / 2, 270, line, COLOR_WHITE, COLOR_BG, 1);

    int64_t secs = s_last_allotment_ms / 1000;
    if (secs >= 60) {
        snprintf(line, sizeof(line), "Time: %lld min", (long long)(secs / 60));
    } else {
        snprintf(line, sizeof(line), "Time: %lld sec", (long long)secs);
    }
    lw = strlen(line) * 8;
    display_render_text((screen_w - lw) / 2, 290, line, COLOR_WHITE, COLOR_BG, 1);

    snprintf(line, sizeof(line), "Wallet: %llu sats", (unsigned long long)s_wallet_balance);
    lw = strlen(line) * 8;
    display_render_text((screen_w - lw) / 2, 320, line, wallet_color(), COLOR_BG, 1);

    axs15231b_flush();
}

static void render_error_screen(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    axs15231b_fill_rect(0, 190, screen_w, 50, COLOR_RED);
    const char *msg = "NO UPSTREAM";
    int msg_w = strlen(msg) * 8 * 2;
    display_render_text((screen_w - msg_w) / 2, 202, msg, COLOR_WHITE, COLOR_RED, 2);

    char line[48];
    int lw;

    const char *l1 = "Internet unavailable";
    lw = strlen(l1) * 8;
    display_render_text((screen_w - lw) / 2, 270, l1, COLOR_WHITE, COLOR_BG, 1);

    const char *l2 = "Check WiFi config";
    lw = strlen(l2) * 8;
    display_render_text((screen_w - lw) / 2, 290, l2, COLOR_YELLOW, COLOR_BG, 1);

    const char *l3 = "AP still active";
    lw = strlen(l3) * 8;
    display_render_text((screen_w - lw) / 2, 320, l3, COLOR_GREEN, COLOR_BG, 1);

    snprintf(line, sizeof(line), "SSID: %s", s_ap_ssid);
    lw = strlen(line) * 8;
    display_render_text((screen_w - lw) / 2, 340, line, COLOR_DIM, COLOR_BG, 1);

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
                vTaskDelay(pdMS_TO_TICKS(3000));
                s_state = DISPLAY_READY;
                break;
            case DISPLAY_ERROR:
                render_error_screen();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(RENDER_INTERVAL_MS));
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
}

void display_update(const char *ap_ssid, int active_clients,
                    uint64_t wallet_balance, const char *portal_url,
                    const char *mint_url, int price_per_step,
                    const char *wifi_status) {
    if (ap_ssid) {
        strncpy(s_ap_ssid, ap_ssid, sizeof(s_ap_ssid) - 1);
        s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';
    }
    if (portal_url) {
        strncpy(s_portal_url, portal_url, sizeof(s_portal_url) - 1);
        s_portal_url[sizeof(s_portal_url) - 1] = '\0';
    }
    if (mint_url) {
        strncpy(s_mint_url, mint_url, sizeof(s_mint_url) - 1);
        s_mint_url[sizeof(s_mint_url) - 1] = '\0';
    }
    if (wifi_status) {
        strncpy(s_wifi_status, wifi_status, sizeof(s_wifi_status) - 1);
        s_wifi_status[sizeof(s_wifi_status) - 1] = '\0';
    }
    if (price_per_step > 0) s_price_per_step = price_per_step;
    s_active_clients = active_clients;
    s_wallet_balance = wallet_balance;
}

void display_notify_payment(int amount_sats, int64_t allotment_ms) {
    s_last_payment_sats = amount_sats;
    s_last_allotment_ms = allotment_ms;
    s_wallet_balance = nucula_wallet_balance();
    display_set_state(DISPLAY_PAYMENT_RECEIVED);
}
