#include "display.h"
#include "axs15231b.h"
#include "qrcoded.h"
#include "font.h"
#include "nucula_wallet.h"
#include "touch.h"
#include "keyboard.h"
#include "wifi_setup.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
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

#define COLOR_GRAY     0x8410
#define COLOR_DARKGRAY 0x4208
#define COLOR_LIGHTBLUE 0xA5FF

static wifi_setup_t s_wifi_setup;
static kb_state_t s_kb_state;
static bool s_wifi_setup_active = false;
static bool s_touch_initialized = false;
static bool s_wifi_scan_pending = false;

#define SETUP_BTN_X 240
#define SETUP_BTN_Y 440
#define SETUP_BTN_W 72
#define SETUP_BTN_H 30

static void render_setup_button(int x, int y, int w, int h) {
    axs15231b_fill_rect(x, y, w, h, COLOR_DARKGRAY);
    const char *label = "Setup";
    int lw = strlen(label) * 8;
    display_render_text(x + (w - lw) / 2, y + (h - 8) / 2, label, COLOR_WHITE, COLOR_DARKGRAY, 1);
}

static bool touch_in_rect(uint16_t tx, uint16_t ty, int x, int y, int w, int h) {
    return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

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

    render_setup_button(SETUP_BTN_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H);

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

    render_setup_button(SETUP_BTN_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H);

    axs15231b_flush();
}

static void render_wifi_setup_scanning(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    const char *title = "WiFi Setup";
    int tw = strlen(title) * 8;
    display_render_text((screen_w - tw) / 2, 180, title, COLOR_CYAN, COLOR_BG, 1);

    const char *msg = "Scanning...";
    int mw = strlen(msg) * 8;
    display_render_text((screen_w - mw) / 2, 220, msg, COLOR_WHITE, COLOR_BG, 1);

    axs15231b_flush();
}

static void render_wifi_setup_list(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    const char *title = "Select Network";
    int tw = strlen(title) * 8;
    display_render_text((screen_w - tw) / 2, 5, title, COLOR_CYAN, COLOR_BG, 1);

    int y = 25;
    int visible = wifi_setup_visible_count(&s_wifi_setup);

    for (int i = 0; i < visible && y < 295; i++) {
        const wifi_ap_info_t *ap = wifi_setup_get_visible(&s_wifi_setup, i);
        if (!ap) break;

        char line[40];
        int rssi_bars = 0;
        if (ap->rssi >= -30) rssi_bars = 4;
        else if (ap->rssi >= -50) rssi_bars = 3;
        else if (ap->rssi >= -70) rssi_bars = 2;
        else rssi_bars = 1;

        snprintf(line, sizeof(line), "%s", ap->ssid);
        axs15231b_fill_rect(5, y, 260, 26, COLOR_DARKGRAY);
        display_render_text(10, y + 4, line, COLOR_WHITE, COLOR_DARKGRAY, 1);

        for (int b = 0; b < rssi_bars; b++) {
            axs15231b_fill_rect(270 + b * 8, y + 16 - (b + 1) * 4, 6, (b + 1) * 4, COLOR_GREEN);
        }

        if (ap->secured) {
            const char *lock = "*";
            display_render_text(254, y + 4, lock, COLOR_YELLOW, COLOR_DARKGRAY, 1);
        }

        y += 30;
    }

    render_setup_button(5, 440, 50, 28);
    display_render_text(10, 444, "X", COLOR_WHITE, COLOR_DARKGRAY, 1);

    axs15231b_flush();
}

static void render_wifi_setup_password(void) {
    axs15231b_fill_screen(COLOR_BG);

    display_render_text(10, 5, s_wifi_setup.selected_ssid, COLOR_CYAN, COLOR_BG, 1);
    display_render_text(10, 25, "Password:", COLOR_DIM, COLOR_BG, 1);

    axs15231b_fill_rect(10, 40, 220, 20, COLOR_DARKGRAY);

    char masked[KB_INPUT_MAX + 1];
    if (s_kb_state.reveal) {
        strncpy(masked, s_kb_state.input, sizeof(masked) - 1);
        masked[sizeof(masked) - 1] = '\0';
    } else {
        int len = s_kb_state.cursor;
        if (len > 27) len = 27;
        for (int i = 0; i < len; i++) masked[i] = '*';
        masked[len] = '\0';
    }
    display_render_text(14, 43, masked, COLOR_WHITE, COLOR_DARKGRAY, 1);

    const char *eye_label = s_kb_state.reveal ? "H" : "S";
    axs15231b_fill_rect(235, 40, 24, 20, COLOR_GRAY);
    display_render_text(241, 43, eye_label, COLOR_WHITE, COLOR_GRAY, 1);

    int kb_y = 70;
    for (int row = 0; row < KB_ROW_COUNT; row++) {
        const char *row_str;
        int key_count = kb_get_row_keys(row, s_kb_state.layer, &row_str);
        int x_off = (row == 0) ? 5 : (row == 1) ? 14 : (row == 2) ? 23 : 5;

        int cx = x_off;
        for (int col = 0; col < key_count; col++) {
            char c = row_str[col];
            int kw = 28;
            if (row == 3) {
                if (col == 0) kw = 42;
                else if (col == key_count - 1) kw = 50;
                else kw = 168;
            }

            uint16_t bg = COLOR_DARKGRAY;
            uint16_t fg = COLOR_WHITE;
            char label[2] = {0, 0};

            if (c == '\001') {
                label[0] = s_kb_state.layer == KB_ALPHA_UPPER ? 'A' : 'a';
                bg = COLOR_GRAY;
            } else if (c == '\002') {
                label[0] = s_kb_state.layer == KB_NUMSYM ? 'a' : '#';
                bg = COLOR_GRAY;
            } else if (c == '\003') {
                label[0] = '_';
            } else if (c == '\004') {
                label[0] = '>';
                fg = COLOR_GREEN;
            } else if (c == '\b') {
                label[0] = '<';
                bg = COLOR_GRAY;
            } else {
                label[0] = c;
            }

            axs15231b_fill_rect(cx, kb_y, kw, 30, bg);
            int lw = 8;
            display_render_text(cx + (kw - lw) / 2, kb_y + 8, label, fg, bg, 1);
            cx += kw + 2;
        }
        kb_y += 34;
    }

    axs15231b_flush();
}

static void render_wifi_setup_connecting(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    const char *msg1 = "Connecting to";
    int w1 = strlen(msg1) * 8;
    display_render_text((screen_w - w1) / 2, 200, msg1, COLOR_WHITE, COLOR_BG, 1);

    int w2 = strlen(s_wifi_setup.selected_ssid) * 8;
    display_render_text((screen_w - w2) / 2, 225, s_wifi_setup.selected_ssid, COLOR_CYAN, COLOR_BG, 1);

    const char *dots = "...";
    int dw = strlen(dots) * 8;
    display_render_text((screen_w - dw) / 2, 255, dots, COLOR_DIM, COLOR_BG, 1);

    axs15231b_flush();
}

static void render_wifi_setup_result(void) {
    int screen_w = axs15231b_get_width();
    axs15231b_fill_screen(COLOR_BG);

    if (s_wifi_setup.state == SETUP_SUCCESS) {
        const char *msg = "Connected!";
        int mw = strlen(msg) * 8 * 2;
        display_render_text((screen_w - mw) / 2, 180, msg, COLOR_WHITE, COLOR_GREEN, 2);

        if (s_wifi_setup.connect_ip[0]) {
            char ip_line[32];
            snprintf(ip_line, sizeof(ip_line), "IP: %s", s_wifi_setup.connect_ip);
            int iw = strlen(ip_line) * 8;
            display_render_text((screen_w - iw) / 2, 240, ip_line, COLOR_WHITE, COLOR_BG, 1);
        }
    } else {
        const char *msg = "Connection failed";
        int mw = strlen(msg) * 8 * 2;
        display_render_text((screen_w - mw) / 2, 180, msg, COLOR_WHITE, COLOR_RED, 2);

        const char *hint = "Wrong password?";
        int hw = strlen(hint) * 8;
        display_render_text((screen_w - hw) / 2, 240, hint, COLOR_YELLOW, COLOR_BG, 1);

        axs15231b_fill_rect(30, 280, 120, 30, COLOR_DARKGRAY);
        display_render_text(50, 288, "Retry", COLOR_WHITE, COLOR_DARKGRAY, 1);
        axs15231b_fill_rect(170, 280, 120, 30, COLOR_DARKGRAY);
        display_render_text(180, 288, "Change", COLOR_WHITE, COLOR_DARKGRAY, 1);
    }

    axs15231b_flush();
}

static void handle_wifi_setup_touch(uint16_t tx, uint16_t ty) {
    switch (s_wifi_setup.state) {
        case SETUP_SCAN:
            break;

        case SETUP_LIST: {
            if (touch_in_rect(tx, ty, 5, 440, 50, 28)) {
                wifi_setup_handle_cancel(&s_wifi_setup);
                s_wifi_setup_active = false;
                s_state = DISPLAY_ERROR;
                return;
            }
            int y = 25;
            int visible = wifi_setup_visible_count(&s_wifi_setup);
            for (int i = 0; i < visible; i++) {
                if (touch_in_rect(tx, ty, 5, y, 300, 26)) {
                    wifi_setup_handle_select(&s_wifi_setup, i);
                    kb_state_init(&s_kb_state);
                    return;
                }
                y += 30;
            }
            break;
        }

        case SETUP_PASSWORD: {
            if (touch_in_rect(tx, ty, 235, 40, 24, 20)) {
                s_kb_state.reveal = !s_kb_state.reveal;
                return;
            }
            kb_result_t r = kb_hit_test(tx, ty, s_kb_state.layer);
            if (r.action != KB_ACTION_NONE) {
                if (r.action == KB_ACTION_DONE && s_kb_state.cursor > 0) {
                    wifi_setup_handle_connect(&s_wifi_setup);
                    wifi_config_t wifi_cfg = {0};
                    strncpy((char *)wifi_cfg.sta.ssid, s_wifi_setup.selected_ssid,
                            sizeof(wifi_cfg.sta.ssid) - 1);
                    strncpy((char *)wifi_cfg.sta.password, s_kb_state.input,
                            sizeof(wifi_cfg.sta.password) - 1);
                    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                    esp_wifi_connect();
                    return;
                }
                kb_apply(&s_kb_state, r);
            }
            break;
        }

        case SETUP_CONNECTING:
            break;

        case SETUP_SUCCESS: {
            wifi_setup_handle_cancel(&s_wifi_setup);
            s_wifi_setup_active = false;
            s_state = DISPLAY_READY;
            return;
        }

        case SETUP_FAILED: {
            if (touch_in_rect(tx, ty, 30, 280, 120, 30)) {
                wifi_setup_handle_retry(&s_wifi_setup);
                kb_state_init(&s_kb_state);
            } else if (touch_in_rect(tx, ty, 170, 280, 120, 30)) {
                wifi_setup_handle_change_network(&s_wifi_setup);
            }
            break;
        }

        default:
            break;
    }
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

        touch_point_t tp;
        if (s_touch_initialized && touch_read(&tp) && tp.touched) {
            if (state == DISPLAY_WIFI_SETUP) {
                handle_wifi_setup_touch(tp.x, tp.y);
                state = s_state;
            } else if (state == DISPLAY_ERROR || state == DISPLAY_READY) {
                if (touch_in_rect(tp.x, tp.y, SETUP_BTN_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H)) {
                    s_wifi_setup_active = true;
                    wifi_setup_init(&s_wifi_setup);
                    kb_state_init(&s_kb_state);
                    s_wifi_scan_pending = true;
                    s_state = DISPLAY_WIFI_SETUP;
                    state = DISPLAY_WIFI_SETUP;
                }
            }
        }

        if (s_wifi_scan_pending && s_wifi_setup.state == SETUP_SCAN) {
            s_wifi_scan_pending = false;
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_scan_config_t scan_cfg = {0};
            scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            scan_cfg.scan_time.active.min = 100;
            scan_cfg.scan_time.active.max = 300;
            esp_err_t scan_ret = esp_wifi_scan_start(&scan_cfg, true);
            if (scan_ret != ESP_OK) {
                ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(scan_ret));
                wifi_setup_set_aps(&s_wifi_setup, NULL, 0);
            } else {
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);
                if (ap_count > WIFI_SETUP_MAX_APS) ap_count = WIFI_SETUP_MAX_APS;
                wifi_ap_record_t ap_records[WIFI_SETUP_MAX_APS];
                esp_wifi_scan_get_ap_records(&ap_count, ap_records);

                wifi_ap_info_t aps[WIFI_SETUP_MAX_APS];
                for (int i = 0; i < (int)ap_count; i++) {
                    strncpy(aps[i].ssid, (const char *)ap_records[i].ssid, WIFI_SETUP_SSID_LEN - 1);
                    aps[i].ssid[WIFI_SETUP_SSID_LEN - 1] = '\0';
                    aps[i].rssi = ap_records[i].rssi;
                    aps[i].secured = (ap_records[i].authmode != WIFI_AUTH_OPEN);
                }

                for (int i = 0; i < (int)ap_count - 1; i++) {
                    for (int j = i + 1; j < (int)ap_count; j++) {
                        if (aps[j].rssi > aps[i].rssi) {
                            wifi_ap_info_t tmp = aps[i];
                            aps[i] = aps[j];
                            aps[j] = tmp;
                        }
                    }
                }

                wifi_setup_set_aps(&s_wifi_setup, aps, (int)ap_count);
            }
        }

        if (s_wifi_setup_active && s_wifi_setup.state == SETUP_CONNECTING) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                wifi_setup_handle_connect_result(&s_wifi_setup, true, NULL);
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
            case DISPLAY_WIFI_SETUP:
                switch (s_wifi_setup.state) {
                    case SETUP_SCAN:
                        render_wifi_setup_scanning();
                        break;
                    case SETUP_LIST:
                        render_wifi_setup_list();
                        break;
                    case SETUP_PASSWORD:
                        render_wifi_setup_password();
                        break;
                    case SETUP_CONNECTING:
                        render_wifi_setup_connecting();
                        break;
                    case SETUP_SUCCESS:
                        render_wifi_setup_result();
                        vTaskDelay(pdMS_TO_TICKS(3000));
                        wifi_setup_handle_cancel(&s_wifi_setup);
                        s_wifi_setup_active = false;
                        s_state = DISPLAY_READY;
                        break;
                    case SETUP_FAILED:
                        render_wifi_setup_result();
                        break;
                    default:
                        break;
                }
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

    esp_err_t touch_ret = touch_init();
    if (touch_ret == ESP_OK) {
        s_touch_initialized = true;
        ESP_LOGI(TAG, "Touch controller initialized");
    } else {
        ESP_LOGW(TAG, "Touch init failed (non-fatal): %s", esp_err_to_name(touch_ret));
    }

    xTaskCreatePinnedToCore(display_task, "display", 24576, NULL, 2, NULL, 1);

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

void display_notify_wifi_connected(const char *ip) {
    if (s_wifi_setup_active && s_wifi_setup.state == SETUP_CONNECTING) {
        wifi_setup_handle_connect_result(&s_wifi_setup, true, ip);
        if (s_kb_state.cursor > 0) {
            tollgate_config_add_wifi(s_wifi_setup.selected_ssid, s_kb_state.input);
        }
    }
}

void display_notify_wifi_disconnected(void) {
    if (s_wifi_setup_active && s_wifi_setup.state == SETUP_CONNECTING) {
        wifi_setup_handle_connect_result(&s_wifi_setup, false, NULL);
    }
}

void display_enter_wifi_setup(void) {
    if (!s_initialized) return;
    s_wifi_setup_active = true;
    wifi_setup_init(&s_wifi_setup);
    kb_state_init(&s_kb_state);
    s_wifi_scan_pending = true;
    s_state = DISPLAY_WIFI_SETUP;
}
