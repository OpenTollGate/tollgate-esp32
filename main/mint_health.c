#include "mint_health.h"
#include "tls_worker.h"
#include "tollgate_core_mint_health.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nucula_wallet.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mint_health";

#define WALLET_QUEUE_LEN 8
static QueueHandle_t s_wallet_queue = NULL;

static int s_last_probe_err = 0;

static tollgate_mint_health_t s_health_state;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_mutex = NULL;

#define MAX_CALLBACKS 4
static mint_health_changed_cb s_callbacks[MAX_CALLBACKS];
static int s_callback_count = 0;

static void fire_callbacks(void)
{
    for (int i = 0; i < s_callback_count; i++) {
        if (s_callbacks[i]) s_callbacks[i]();
    }
}

esp_err_t mint_health_init(const char urls[][256], int count)
{
    if (count > MINT_HEALTH_MAX) count = MINT_HEALTH_MAX;
    s_health_state.count = count;
    s_callback_count = 0;

    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

    memset(s_health_state.mints, 0, sizeof(s_health_state.mints));
    for (int i = 0; i < count; i++) {
        strncpy(s_health_state.mints[i].url, urls[i], sizeof(s_health_state.mints[i].url) - 1);
        s_health_state.mints[i].reachable = false;
        s_health_state.mints[i].consecutive_successes = 0;
        s_health_state.mints[i].last_probe_ms = 0;
        s_health_state.mints[i].last_http_status = 0;
    }

    ESP_LOGI(TAG, "Initialized with %d mints", count);
    return ESP_OK;
}

static bool probe_mint(const char *url)
{
    char probe_url[512];
    snprintf(probe_url, sizeof(probe_url), "%s/v1/info", url);

    esp_http_client_config_t config = {
        .url = probe_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = MINT_HEALTH_PROBE_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        s_last_probe_err = -1;
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "probe open failed: %s err=0x%x", probe_url, err);
        s_last_probe_err = err;
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    s_last_probe_err = 0;

    char *resp = NULL;
    if (content_length > 0 && content_length < 8192) {
        resp = malloc(content_length + 1);
        if (resp) {
            int read = esp_http_client_read(client, resp, content_length);
            if (read > 0) resp[read] = '\0';
        }
    }
    if (resp) free(resp);

    esp_http_client_cleanup(client);
    return (status >= 200 && status < 300);
}

static void run_probes(void)
{
    int old_reachable = 0;
    int new_reachable = 0;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;

    old_reachable = tollgate_core_mint_health_count_reachable(&s_health_state);

    for (int i = 0; i < s_health_state.count; i++) {
        bool ok = probe_mint(s_health_state.mints[i].url);
        int64_t probe_time = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

        tollgate_core_mint_health_update(&s_health_state, i, ok, ok ? 200 : 0,
                                          ok ? 0 : s_last_probe_err, probe_time);
    }

    new_reachable = tollgate_core_mint_health_count_reachable(&s_health_state);
    bool changed = (old_reachable != new_reachable);
    xSemaphoreGive(s_mutex);

    if (changed) {
        ESP_LOGI(TAG, "Reachable set changed: %d -> %d", old_reachable, new_reachable);
        fire_callbacks();
    }
}

static void run_initial_probes(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;

    for (int i = 0; i < s_health_state.count; i++) {
        bool ok = probe_mint(s_health_state.mints[i].url);
        int64_t probe_time = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

        tollgate_core_mint_health_update_initial(&s_health_state, i, ok, ok ? 200 : 0,
                                                   ok ? 0 : s_last_probe_err, probe_time);

        if (ok) {
            ESP_LOGI(TAG, "Initial probe OK: %s (reachable)", s_health_state.mints[i].url);
        } else {
            ESP_LOGW(TAG, "Initial probe FAIL: %s (unreachable)", s_health_state.mints[i].url);
        }
    }

    xSemaphoreGive(s_mutex);
    fire_callbacks();
}

static void process_wallet_queue(void)
{
    char *token;
    while (s_wallet_queue && xQueueReceive(s_wallet_queue, &token, 0) == pdTRUE) {
        if (!token) continue;
        ESP_LOGI(TAG, "Processing wallet receive (%zu bytes)", strlen(token));
        esp_err_t err = nucula_wallet_receive(token);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Wallet receive OK, balance=%llu",
                     (unsigned long long)nucula_wallet_balance());
        } else {
            ESP_LOGW(TAG, "Wallet receive failed");
        }
        free(token);
    }
}

static void health_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Health probe task started, waiting for DNS to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    run_initial_probes();
    process_wallet_queue();

    while (s_running) {
        TickType_t start = xTaskGetTickCount();
        while (s_running) {
            TickType_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (elapsed >= MINT_HEALTH_PROBE_INTERVAL_S * 1000) break;

            char *token = NULL;
            if (s_wallet_queue && xQueueReceive(s_wallet_queue, &token, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (token) {
                    ESP_LOGI(TAG, "Processing wallet receive (%zu bytes)", strlen(token));
                    esp_err_t err = nucula_wallet_receive(token);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "Wallet receive OK, balance=%llu",
                                 (unsigned long long)nucula_wallet_balance());
                    } else {
                        ESP_LOGW(TAG, "Wallet receive failed");
                    }
                    free(token);
                }
            }
        }
        if (!s_running) break;
        run_probes();
        process_wallet_queue();
    }

    s_task_handle = NULL;
    vTaskDelete(NULL);
}

void mint_health_start(void)
{
    if (s_running) return;
    s_running = true;

    s_wallet_queue = xQueueCreate(WALLET_QUEUE_LEN, sizeof(char *));
    tls_worker_set_queue(s_wallet_queue);

    xTaskCreate(health_task, "mint_health", 16384, NULL, 3, &s_task_handle);
}

void mint_health_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

const mint_status_t *mint_health_get_all(int *out_count)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        *out_count = 0;
        return (const mint_status_t *)s_health_state.mints;
    }
    *out_count = s_health_state.count;
    xSemaphoreGive(s_mutex);
    return (const mint_status_t *)s_health_state.mints;
}

bool mint_health_is_reachable(const char *url)
{
    if (!url) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool result = tollgate_core_mint_health_is_reachable(&s_health_state, url);
    xSemaphoreGive(s_mutex);
    return result;
}

void mint_health_mark_unreachable(const char *url)
{
    if (!url) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    tollgate_core_mint_health_mark_unreachable(&s_health_state, url);
    xSemaphoreGive(s_mutex);
}

void mint_health_register_callback(mint_health_changed_cb cb)
{
    if (s_callback_count < MAX_CALLBACKS && cb) {
        s_callbacks[s_callback_count++] = cb;
    }
}
