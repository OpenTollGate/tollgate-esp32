#include "cvm_server.h"
#include "mcp_handler.h"
#include "nip04.h"
#include "identity.h"
#include "config.h"
#include "nucula_wallet.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "cvm_server";

static bool g_running = false;
static TaskHandle_t g_task = NULL;

static const char *DEFAULT_RELAY = "wss://relay.damus.io";

static char *fetch_relays(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg && cfg->cvm_relays[0]) {
        return cfg->cvm_relays;
    }
    return (char *)DEFAULT_RELAY;
}

static char *http_get(const char *url, int timeout_ms)
{
    char *buf = malloc(8192);
    if (!buf) return NULL;
    int total = 0;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(buf); return NULL; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int max_read = content_length > 0 ? content_length : 8191;

    while (total < max_read) {
        int n = esp_http_client_read(client, buf + total, max_read - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    esp_http_client_cleanup(client);
    return buf;
}

static cJSON *build_filter(const char *npub)
{
    cJSON *filter = cJSON_CreateObject();
    cJSON *kinds = cJSON_CreateArray();
    cJSON_AddItemToArray(kinds, cJSON_CreateNumber(4));
    cJSON_AddItemToObject(filter, "kinds", kinds);
    cJSON_AddStringToObject(filter, "#p", npub);
    cJSON_AddNumberToObject(filter, "limit", 10);
    return filter;
}

static cJSON *build_subscription(const char *npub)
{
    cJSON *sub = cJSON_CreateArray();
    cJSON_AddItemToArray(sub, cJSON_CreateString("REQ"));
    cJSON_AddItemToArray(sub, cJSON_CreateString("cvm_sub_01"));
    cJSON_AddItemToArray(sub, build_filter(npub));
    return sub;
}

static void process_dm(const char *sender_pubkey, const char *encrypted_content)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Identity not initialized");
        return;
    }

    uint8_t sender_pk[64];
    for (int i = 0; i < 64; i++) {
        char hex[3] = { sender_pubkey[i*2], sender_pubkey[i*2+1], 0 };
        sender_pk[i] = (uint8_t)strtol(hex, NULL, 16);
    }

    char plaintext[2048];
    int pt_len = nip04_decrypt(id->nsec, sender_pk, encrypted_content, plaintext, sizeof(plaintext));
    if (pt_len < 0) {
        ESP_LOGE(TAG, "Failed to decrypt DM from %.8s", sender_pubkey);
        return;
    }

    ESP_LOGI(TAG, "Decrypted DM from %.8s: %s", sender_pubkey, plaintext);

    cJSON *msg = cJSON_Parse(plaintext);
    if (!msg) {
        ESP_LOGE(TAG, "Invalid JSON in DM");
        return;
    }

    cJSON *method = cJSON_GetObjectItem(msg, "method");
    cJSON *params = cJSON_GetObjectItem(msg, "params");
    if (!method || !cJSON_IsString(method)) {
        cJSON_Delete(msg);
        ESP_LOGE(TAG, "Missing 'method' in CVM request");
        return;
    }

    mcp_request_t req = {0};
    req.tool = mcp_parse_tool(method->valuestring);
    strncpy(req.method, method->valuestring, sizeof(req.method) - 1);
    if (params && cJSON_IsString(params)) {
        strncpy(req.params_json, params->valuestring, sizeof(req.params_json) - 1);
    } else if (params) {
        char *pjson = cJSON_PrintUnformatted(params);
        strncpy(req.params_json, pjson, sizeof(req.params_json) - 1);
        cJSON_free(pjson);
    }

    mcp_response_t resp = mcp_dispatch(&req);
    cJSON_Delete(msg);

    cJSON *response_msg = cJSON_CreateObject();
    if (resp.success) {
        cJSON_AddStringToObject(response_msg, "status", "ok");
        cJSON_AddItemToObject(response_msg, "result", cJSON_Parse(resp.result_json));
    } else {
        cJSON_AddStringToObject(response_msg, "status", "error");
        cJSON_AddStringToObject(response_msg, "error", resp.error);
    }

    char *response_str = cJSON_PrintUnformatted(response_msg);
    cJSON_Delete(response_msg);

    uint8_t response_ct[4096];
    size_t ct_len = 0;
    nip04_encrypt(id->nsec, sender_pk, response_str, response_ct, &ct_len);
    free(response_str);

    ESP_LOGI(TAG, "CVM response prepared (%zu bytes encrypted), would send to %.8s", ct_len, sender_pubkey);
}

static void parse_nostr_events(const char *data)
{
    cJSON *arr = cJSON_Parse(data);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsArray(item)) continue;
        int arr_size = cJSON_GetArraySize(item);
        if (arr_size < 3) continue;

        cJSON *cmd = cJSON_GetArrayItem(item, 0);
        if (!cmd || !cJSON_IsString(cmd) || strcmp(cmd->valuestring, "EVENT") != 0) continue;

        cJSON *event = cJSON_GetArrayItem(item, 2);
        if (!event) continue;

        cJSON *kind = cJSON_GetObjectItem(event, "kind");
        if (!kind || kind->valueint != 4) continue;

        cJSON *pubkey = cJSON_GetObjectItem(event, "pubkey");
        cJSON *content = cJSON_GetObjectItem(event, "content");
        if (pubkey && content) {
            process_dm(pubkey->valuestring, content->valuestring);
        }
    }
    cJSON_Delete(arr);
}

static void cvm_task(void *arg)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Cannot start: identity not initialized");
        vTaskDelete(NULL);
        return;
    }

    char *relays = fetch_relays();
    ESP_LOGI(TAG, "CVM server started, relays: %s", relays);

    while (g_running) {
        ESP_LOGI(TAG, "Polling for DMs...");

        cJSON *sub = build_subscription(id->npub_hex);
        char *sub_json = cJSON_PrintUnformatted(sub);
        cJSON_Delete(sub);

        char url[256];
        snprintf(url, sizeof(url), "%s/cvm_poll", relays);
        free(sub_json);

        vTaskDelay(pdMS_TO_TICKS(30000));
    }

    ESP_LOGI(TAG, "CVM server stopped");
    vTaskDelete(NULL);
}

esp_err_t cvm_server_init(void)
{
    ESP_LOGI(TAG, "CVM server initialized");
    return ESP_OK;
}

void cvm_server_start(void)
{
    if (g_running) return;
    g_running = true;
    xTaskCreate(cvm_task, "cvm_server", 8192, NULL, 5, &g_task);
}

void cvm_server_stop(void)
{
    g_running = false;
    if (g_task) {
        vTaskDelay(pdMS_TO_TICKS(500));
        g_task = NULL;
    }
}
