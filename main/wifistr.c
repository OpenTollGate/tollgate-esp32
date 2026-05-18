#include "wifistr.h"
#include "identity.h"
#include "nostr_event.h"
#include "config.h"
#include "local_relay.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "wifistr";
static TimerHandle_t s_publish_timer = NULL;

static esp_err_t ws_send_to_relay(const char *relay_url, const char *event_json)
{
    char host[128] = {0};
    int port = 443;
    char path[128] = "/";

    if (strncmp(relay_url, "wss://", 6) != 0) {
        ESP_LOGW(TAG, "Unsupported relay URL: %s", relay_url);
        return ESP_ERR_INVALID_ARG;
    }

    const char *url_start = relay_url + 6;
    const char *path_ptr = strchr(url_start, '/');
    if (path_ptr) {
        size_t host_len = path_ptr - url_start;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        memcpy(host, url_start, host_len);
        host[host_len] = '\0';
        strncpy(path, path_ptr, sizeof(path) - 1);
    } else {
        strncpy(host, url_start, sizeof(host) - 1);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    ESP_LOGI(TAG, "Connecting to %s:%d%s", host, port, path);

    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to allocate TLS handle");
        return ESP_ERR_NO_MEM;
    }

    int ret = esp_tls_conn_new_sync(host, strlen(host), port, &tls_cfg, tls);
    if (ret < 0) {
        ESP_LOGE(TAG, "TLS connect failed to %s", host);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char upgrade[512];
    snprintf(upgrade, sizeof(upgrade),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             path, host);

    int written = esp_tls_conn_write(tls, (const unsigned char *)upgrade, strlen(upgrade));
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send upgrade request");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char resp[1024];
    int rlen = esp_tls_conn_read(tls, (unsigned char *)resp, sizeof(resp) - 1);
    if (rlen <= 0 || !strstr(resp, "101")) {
        ESP_LOGE(TAG, "WebSocket upgrade failed (read %d bytes)", rlen);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("EVENT"));
    cJSON_AddItemToArray(arr, cJSON_Parse(event_json));
    char *msg = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    size_t msg_len = strlen(msg);
    uint8_t ws_header[10];
    int header_len = 0;
    ws_header[0] = 0x81;
    if (msg_len <= 125) {
        ws_header[1] = (uint8_t)msg_len;
        header_len = 2;
    } else if (msg_len <= 65535) {
        ws_header[1] = 126;
        ws_header[2] = (uint8_t)((msg_len >> 8) & 0xff);
        ws_header[3] = (uint8_t)(msg_len & 0xff);
        header_len = 4;
    } else {
        ws_header[1] = 127;
        for (int i = 0; i < 8; i++)
            ws_header[2 + i] = (uint8_t)((msg_len >> (56 - i * 8)) & 0xff);
        header_len = 10;
    }

    esp_tls_conn_write(tls, ws_header, header_len);
    esp_tls_conn_write(tls, (const unsigned char *)msg, msg_len);

    free(msg);

    uint8_t resp_buf[256];
    int resp_len = esp_tls_conn_read(tls, resp_buf, sizeof(resp_buf) - 1);
    if (resp_len > 0) {
        resp_buf[resp_len] = '\0';
        int mask_len = (resp_buf[1] & 0x80) ? 4 : 0;
        int payload_offset = 2 + mask_len;
        if (resp_len > payload_offset) {
            ESP_LOGI(TAG, "Relay response: %.*s", resp_len - payload_offset,
                     (char *)resp_buf + payload_offset);
        }
    }

    uint8_t close_frame[2] = {0x88, 0x00};
    esp_tls_conn_write(tls, close_frame, 2);
    esp_tls_conn_destroy(tls);

    ESP_LOGI(TAG, "Published to %s", host);
    return ESP_OK;
}

static char *build_wifistr_event(void)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Identity not initialized");
        return NULL;
    }

    const tollgate_config_t *cfg = tollgate_config_get();

    cJSON *tags = cJSON_CreateArray();

    cJSON *d_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(d_tag, cJSON_CreateString("d"));
    cJSON_AddItemToArray(d_tag, cJSON_CreateString(id->npub_hex));
    cJSON_AddItemToArray(tags, d_tag);

    cJSON *ssid_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(ssid_tag, cJSON_CreateString("ssid"));
    cJSON_AddItemToArray(ssid_tag, cJSON_CreateString(id->ap_ssid));
    cJSON_AddItemToArray(tags, ssid_tag);

    cJSON *h_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(h_tag, cJSON_CreateString("h"));
    cJSON_AddItemToArray(h_tag, cJSON_CreateString("cashu-testnut"));
    cJSON_AddItemToArray(tags, h_tag);

    cJSON *sec_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(sec_tag, cJSON_CreateString("security"));
    cJSON_AddItemToArray(sec_tag, cJSON_CreateString("open"));
    cJSON_AddItemToArray(tags, sec_tag);

    cJSON *g_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(g_tag, cJSON_CreateString("g"));
    cJSON_AddItemToArray(g_tag, cJSON_CreateString(cfg->nostr_geohash));
    cJSON_AddItemToArray(tags, g_tag);

    cJSON *c_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(c_tag, cJSON_CreateString("c"));
    cJSON_AddItemToArray(c_tag, cJSON_CreateString("cashu"));
    cJSON_AddItemToArray(tags, c_tag);

    char content[512];
    snprintf(content, sizeof(content),
             "TollGate WiFi hotspot: %s | Price: %d sats/%dms | Mint: %s",
             id->ap_ssid, cfg->price_per_step, cfg->step_size_ms, cfg->mint_url);

    char *tags_str = cJSON_PrintUnformatted(tags);
    cJSON_Delete(tags);

    nostr_event_t event;
    nostr_event_init(&event, id->npub_hex, 38787, tags_str, content);
    nostr_event_sign(&event, id->nsec);
    free(tags_str);

    char *event_json = malloc(2048);
    if (!event_json) return NULL;

    esp_err_t ret = nostr_event_to_json(&event, event_json, 2048);
    if (ret != ESP_OK) {
        free(event_json);
        return NULL;
    }

    return event_json;
}

esp_err_t wifistr_publish(void)
{
    char *event_json = build_wifistr_event();
    if (!event_json) {
        ESP_LOGE(TAG, "Failed to build wifistr event");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wifistr event: %s", event_json);

    esp_err_t local_ret = local_relay_publish(event_json, strlen(event_json));
    if (local_ret == ESP_OK) {
        ESP_LOGI(TAG, "Published to local relay");
    }

    const tollgate_config_t *cfg = tollgate_config_get();
    esp_err_t last_err = local_ret;

    for (int i = 0; i < cfg->nostr_relay_count; i++) {
        esp_err_t err = ws_send_to_relay(cfg->nostr_relays[i], event_json);
        if (err == ESP_OK) last_err = ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    free(event_json);
    return last_err;
}

static void publish_task(void *pvParameters)
{
    wifistr_publish();
    vTaskDelete(NULL);
}

static void timer_callback(TimerHandle_t timer)
{
    xTaskCreate(publish_task, "wifistr_pub", 16384, NULL, 3, NULL);
}

void wifistr_start_periodic(int interval_s)
{
    if (s_publish_timer) return;
    s_publish_timer = xTimerCreate("wifistr", pdMS_TO_TICKS(interval_s * 1000),
                                   pdTRUE, NULL, timer_callback);
    if (s_publish_timer) {
        xTimerStart(s_publish_timer, 0);
        ESP_LOGI(TAG, "Periodic publish every %ds", interval_s);
    }
}
