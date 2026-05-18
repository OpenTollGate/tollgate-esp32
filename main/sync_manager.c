#include "sync_manager.h"
#include "local_relay.h"
#include "storage_engine.h"
#include "relay_core.h"
#include "config.h"
#include "nostr_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sync_mgr";

static const uint8_t WS_FIN_TEXT = 0x81;
static const uint8_t WS_FIN_CLOSE = 0x88;

static esp_err_t ws_connect(const char *wss_url, esp_tls_t **out_tls)
{
    char host[128] = {0};
    int port = 443;
    char path[128] = "/";

    const char *url_start = wss_url;
    if (strncmp(wss_url, "wss://", 6) == 0) url_start = wss_url + 6;

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
    if (colon) { *colon = '\0'; port = atoi(colon + 1); }

    esp_tls_cfg_t tls_cfg = { .crt_bundle_attach = esp_crt_bundle_attach };
    esp_tls_t *tls = esp_tls_init();
    if (!tls) return ESP_ERR_NO_MEM;

    int ret = esp_tls_conn_new_sync(host, strlen(host), port, &tls_cfg, tls);
    if (ret < 0) {
        esp_tls_conn_destroy(tls);
        ESP_LOGW(TAG, "TLS connect failed to %s", host);
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
             "\r\n", path, host);

    esp_tls_conn_write(tls, (const unsigned char *)upgrade, strlen(upgrade));

    char resp[1024];
    int rlen = esp_tls_conn_read(tls, (unsigned char *)resp, sizeof(resp) - 1);
    if (rlen <= 0 || !strstr(resp, "101")) {
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    *out_tls = tls;
    return ESP_OK;
}

static void ws_send_text(esp_tls_t *tls, const char *data, size_t len)
{
    uint8_t header[10];
    int hlen = 0;
    header[0] = WS_FIN_TEXT;
    if (len <= 125) { header[1] = (uint8_t)len; hlen = 2; }
    else if (len <= 65535) {
        header[1] = 126;
        header[2] = (uint8_t)((len >> 8) & 0xff);
        header[3] = (uint8_t)(len & 0xff);
        hlen = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++)
            header[2 + i] = (uint8_t)((len >> (56 - i * 8)) & 0xff);
        hlen = 10;
    }
    esp_tls_conn_write(tls, header, hlen);
    esp_tls_conn_write(tls, (const unsigned char *)data, len);
}

static void ws_send_close(esp_tls_t *tls)
{
    uint8_t close_frame[2] = {WS_FIN_CLOSE, 0x00};
    esp_tls_conn_write(tls, close_frame, 2);
}

static int ws_read_text(esp_tls_t *tls, char *buf, size_t buf_len)
{
    uint8_t header[2];
    int rlen = esp_tls_conn_read(tls, header, 2);
    if (rlen < 2) return -1;

    if ((header[0] & 0x0f) == 0x08) return -1;

    int payload_len = header[1] & 0x7f;
    if (payload_len == 126) {
        uint8_t ext[2];
        esp_tls_conn_read(tls, ext, 2);
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        esp_tls_conn_read(tls, ext, 8);
        payload_len = 0;
        for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | ext[i];
    }

    int mask_len = (header[1] & 0x80) ? 4 : 0;
    uint8_t mask[4] = {0};
    if (mask_len) esp_tls_conn_read(tls, mask, 4);

    if (payload_len > (int)buf_len - 1) payload_len = (int)buf_len - 1;
    esp_tls_conn_read(tls, (unsigned char *)buf, payload_len);
    for (int i = 0; i < payload_len; i++) buf[i] ^= mask[i % 4];
    buf[payload_len] = '\0';
    return payload_len;
}

static void get_event_ids_from_storage(char ***ids_out, uint16_t *count_out)
{
    extern relay_ctx_t g_relay_ctx;
    if (!g_relay_ctx.storage) { *ids_out = NULL; *count_out = 0; return; }

    char **results = NULL;
    uint16_t count = 0;
    storage_query_events_json(g_relay_ctx.storage, -1, NULL, 5000, &results, &count);

    char **ids = calloc(count, sizeof(char *));
    uint16_t id_count = 0;

    for (uint16_t i = 0; i < count; i++) {
        cJSON *obj = cJSON_Parse(results[i]);
        if (!obj) continue;
        cJSON *id = cJSON_GetObjectItem(obj, "id");
        if (id && cJSON_IsString(id)) {
            ids[id_count++] = strdup(id->valuestring);
        }
        cJSON_Delete(obj);
    }

    storage_free_query_results(results, count);
    *ids_out = ids;
    *count_out = id_count;
}

static void free_event_ids(char **ids, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) free(ids[i]);
    free(ids);
}

esp_err_t sync_manager_init(sync_manager_t *mgr, relay_selector_t *selector)
{
    memset(mgr, 0, sizeof(sync_manager_t));
    mgr->selector = selector;
    mgr->lock = xSemaphoreCreateMutex();
    if (!mgr->lock) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static void sync_task(void *arg);

void sync_manager_start(sync_manager_t *mgr)
{
    mgr->running = true;
    xTaskCreate(sync_task, "sync_mgr", 16384, mgr, 3, NULL);
    ESP_LOGI(TAG, "Sync manager started");
}

void sync_manager_stop(sync_manager_t *mgr)
{
    mgr->running = false;
}

esp_err_t sync_manager_do_negentropy_sync(sync_manager_t *mgr)
{
    if (!mgr->selector) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->sync_in_progress = true;
    xSemaphoreGive(mgr->lock);

    const relay_info_t *primary = relay_selector_get_primary(mgr->selector);
    if (!primary || !primary->alive) {
        ESP_LOGW(TAG, "No primary relay for negentropy sync");
        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->sync_in_progress = false;
        xSemaphoreGive(mgr->lock);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Starting REQ-diff sync with primary: %s", primary->url);

    char **local_ids = NULL;
    uint16_t local_count = 0;
    get_event_ids_from_storage(&local_ids, &local_count);

    if (local_count == 0) {
        ESP_LOGI(TAG, "No local events to sync");
        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->sync_in_progress = false;
        xSemaphoreGive(mgr->lock);
        return ESP_OK;
    }

    esp_tls_t *tls = NULL;
    esp_err_t err = ws_connect(primary->url, &tls);
    if (err != ESP_OK) {
        free_event_ids(local_ids, local_count);
        relay_selector_report_disconnect(mgr->selector, primary->url);
        xSemaphoreTake(mgr->lock, portMAX_DELAY);
        mgr->sync_in_progress = false;
        xSemaphoreGive(mgr->lock);
        return err;
    }

    cJSON *filters = cJSON_CreateObject();
    cJSON *ids_arr = cJSON_CreateArray();
    for (uint16_t i = 0; i < local_count; i++) {
        cJSON_AddItemToArray(ids_arr, cJSON_CreateString(local_ids[i]));
    }
    cJSON_AddItemToObject(filters, "ids", ids_arr);
    char *filters_json = cJSON_PrintUnformatted(filters);
    cJSON_Delete(filters);

    char sub_msg[256];
    snprintf(sub_msg, sizeof(sub_msg), "[\"REQ\",\"sync_diff\",%s]", filters_json);
    free(filters_json);

    ws_send_text(tls, sub_msg, strlen(sub_msg));

    char resp[8192];
    int resp_len = ws_read_text(tls, resp, sizeof(resp));
    (void)resp_len;

    ws_send_close(tls);
    esp_tls_conn_destroy(tls);

    free_event_ids(local_ids, local_count);

    int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->last_negentropy_sync = (uint32_t)now;
    mgr->sync_in_progress = false;
    xSemaphoreGive(mgr->lock);

    ESP_LOGI(TAG, "Negentropy sync completed");
    return ESP_OK;
}

esp_err_t sync_manager_do_reqdiff_sync(sync_manager_t *mgr)
{
    if (!mgr->selector) return ESP_ERR_INVALID_STATE;

    const relay_info_t *fallback = relay_selector_get_fallback(mgr->selector, 0);
    if (!fallback || !fallback->alive) {
        ESP_LOGW(TAG, "No fallback relay for REQ-diff sync");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Starting REQ-diff fallback sync with: %s", fallback->url);

    const tollgate_config_t *cfg = tollgate_config_get();

    esp_tls_t *tls = NULL;
    esp_err_t err = ws_connect(fallback->url, &tls);
    if (err != ESP_OK) {
        relay_selector_report_disconnect(mgr->selector, fallback->url);
        return err;
    }

    char sub_msg[512];
    snprintf(sub_msg, sizeof(sub_msg),
             "[\"REQ\",\"sync_fallback\",{\"authors\":[\"%s\"],\"limit\":500}]",
             cfg->npub);
    ws_send_text(tls, sub_msg, strlen(sub_msg));

    char **local_ids = NULL;
    uint16_t local_count = 0;
    get_event_ids_from_storage(&local_ids, &local_count);

    char resp[8192];
    int events_received = 0;
    int events_stored = 0;

    while (true) {
        int rlen = ws_read_text(tls, resp, sizeof(resp));
        if (rlen < 0) break;

        cJSON *arr = cJSON_Parse(resp);
        if (!arr) continue;

        cJSON *cmd = cJSON_GetArrayItem(arr, 0);
        if (cmd && cJSON_IsString(cmd)) {
            if (strcmp(cmd->valuestring, "EVENT") == 0) {
                cJSON *event_obj = cJSON_GetArrayItem(arr, 1);
                if (event_obj) {
                    events_received++;
                    char *event_json = cJSON_PrintUnformatted(event_obj);
                    cJSON *id_item = cJSON_GetObjectItem(event_obj, "id");

                    bool is_local = false;
                    if (id_item) {
                        for (uint16_t i = 0; i < local_count; i++) {
                            if (strcmp(local_ids[i], id_item->valuestring) == 0) {
                                is_local = true;
                                break;
                            }
                        }
                    }

                    if (!is_local && event_json) {
                        local_relay_publish(event_json, strlen(event_json));
                        events_stored++;
                    }
                    cJSON_free(event_json);
                }
            } else if (strcmp(cmd->valuestring, "EOSE") == 0) {
                cJSON_Delete(arr);
                break;
            }
        }
        cJSON_Delete(arr);
    }

    ws_send_close(tls);
    esp_tls_conn_destroy(tls);
    free_event_ids(local_ids, local_count);

    int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    xSemaphoreTake(mgr->lock, portMAX_DELAY);
    mgr->last_reqdiff_sync = (uint32_t)now;
    xSemaphoreGive(mgr->lock);

    ESP_LOGI(TAG, "REQ-diff sync: received=%d, stored=%d", events_received, events_stored);
    return ESP_OK;
}

static void sync_task(void *arg)
{
    sync_manager_t *mgr = (sync_manager_t *)arg;

    vTaskDelay(pdMS_TO_TICKS(10000));

    relay_selector_probe_all(mgr->selector);

    sync_manager_do_negentropy_sync(mgr);

    const tollgate_config_t *cfg = tollgate_config_get();
    int negentropy_interval = cfg->nostr_sync_interval_s > 0 ? cfg->nostr_sync_interval_s : 1800;
    int reqdiff_interval = cfg->nostr_fallback_sync_interval_s > 0 ?
                           cfg->nostr_fallback_sync_interval_s : 21600;
    int reprobe_interval = 21600;

    int64_t last_negentropy = 0;
    int64_t last_reqdiff = 0;
    int64_t last_reprobe = xTaskGetTickCount() / configTICK_RATE_HZ;

    while (mgr->running) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);

        if ((now - last_reprobe) >= reprobe_interval) {
            relay_selector_probe_all(mgr->selector);
            last_reprobe = now;
        }

        if ((now - last_negentropy) >= negentropy_interval) {
            esp_err_t err = sync_manager_do_negentropy_sync(mgr);
            if (err == ESP_OK) last_negentropy = now;
        }

        if ((now - last_reqdiff) >= reqdiff_interval) {
            esp_err_t err = sync_manager_do_reqdiff_sync(mgr);
            if (err == ESP_OK) last_reqdiff = now;
        }
    }

    vTaskDelete(NULL);
}
