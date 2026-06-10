#include "faucet_client.h"
#include "config.h"
#include "tls_worker.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "faucet_client";
static bool s_running = false;
static TaskHandle_t s_task = NULL;

static char *s_response_buf = NULL;
static int s_response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_response_buf == NULL) {
            s_response_buf = malloc(4096);
            if (!s_response_buf) return ESP_FAIL;
            s_response_len = 0;
        }
        int space = 4096 - s_response_len - 1;
        if (space > 0 && evt->data_len <= space) {
            memcpy(s_response_buf + s_response_len, evt->data, evt->data_len);
            s_response_len += evt->data_len;
            s_response_buf[s_response_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static bool faucet_poll_once(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg->faucet_url[0] == '\0') return false;

    if (s_response_buf) {
        free(s_response_buf);
        s_response_buf = NULL;
    }
    s_response_len = 0;

    const char *post_body = "{\"amount\":10}";

    esp_http_client_config_t http_cfg = {
        .url = cfg->faucet_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGW(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_body, strlen(post_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGD(TAG, "Faucet request failed: status=%d err=%s", status, esp_err_to_name(err));
        if (s_response_buf) { free(s_response_buf); s_response_buf = NULL; }
        return false;
    }

    if (!s_response_buf || s_response_len == 0) {
        return false;
    }

    cJSON *root = cJSON_Parse(s_response_buf);
    free(s_response_buf);
    s_response_buf = NULL;

    if (!root) {
        ESP_LOGW(TAG, "Failed to parse faucet JSON");
        return false;
    }

    cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    cJSON *token_item = cJSON_GetObjectItemCaseSensitive(root, "token");
    cJSON *amount_item = cJSON_GetObjectItemCaseSensitive(root, "amount");

    bool result = false;
    if (success && cJSON_IsTrue(success) && token_item && cJSON_IsString(token_item)) {
        const char *token = token_item->valuestring;
        int amount = amount_item ? amount_item->valueint : 0;
        ESP_LOGI(TAG, "Received %d ehash from faucet", amount);
        tls_worker_submit(token);
        result = true;
    }

    cJSON_Delete(root);
    return result;
}

static void faucet_client_task(void *arg)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    int interval_ms = cfg->faucet_poll_interval_s > 0 ? cfg->faucet_poll_interval_s * 1000 : 120000;

    ESP_LOGI(TAG, "Faucet client started (url=%s, interval=%ds)", cfg->faucet_url, interval_ms / 1000);

    vTaskDelay(pdMS_TO_TICKS(30000));

    while (s_running) {
        faucet_poll_once();
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    vTaskDelete(NULL);
}

esp_err_t faucet_client_start(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg->faucet_url[0] == '\0') {
        ESP_LOGI(TAG, "No faucet URL configured, skipping");
        return ESP_OK;
    }
    if (s_running) return ESP_OK;
    s_running = true;
    BaseType_t ret = xTaskCreate(faucet_client_task, "faucet_cli", 6144, NULL, 3, &s_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create faucet client task");
        s_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void faucet_client_stop(void)
{
    s_running = false;
    s_task = NULL;
}
