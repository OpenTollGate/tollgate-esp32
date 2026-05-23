#include "stratum_client.h"
#include "stratum_proxy.h"
#include "tollgate_core_mining.h"
#include "config.h"
#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "stratum_client";
static stratum_client_state_t s_state = {0};
static esp_transport_handle_t s_transport = NULL;
static bool s_running = false;
static uint32_t s_req_id = 1;
static TaskHandle_t s_task_handle = NULL;

static int read_line(char *buf, int max_len)
{
    int total = 0;
    while (total < max_len - 1) {
        int r = esp_transport_read(s_transport, buf + total, 1, 5000);
        if (r <= 0) return -1;
        if (buf[total] == '\n') {
            buf[total + 1] = '\0';
            return total + 1;
        }
        total++;
    }
    buf[total] = '\0';
    return total;
}

static esp_err_t stratum_connect(const char *host, uint16_t port)
{
    if (s_transport) {
        esp_transport_close(s_transport);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
    }

    s_transport = esp_transport_tcp_init();
    if (!s_transport) {
        ESP_LOGE(TAG, "Failed to init TCP transport");
        return ESP_FAIL;
    }

    esp_err_t err = esp_transport_connect(s_transport, host, port, 10000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to %s:%u", host, (unsigned)port);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
        return ESP_FAIL;
    }

    strncpy(s_state.pool_host, host, sizeof(s_state.pool_host) - 1);
    s_state.pool_port = port;
    s_state.connected = true;
    ESP_LOGI(TAG, "Connected to %s:%u", host, (unsigned)port);
    return ESP_OK;
}

static void send_subscribe(void)
{
    char subscribe[256];
    snprintf(subscribe, sizeof(subscribe),
             "{\"id\":%lu,\"method\":\"mining.subscribe\",\"params\":[\"TollGate/1.0\"]}\n",
             (unsigned long)s_req_id++);
    esp_transport_write(s_transport, subscribe, strlen(subscribe), 5000);
    ESP_LOGI(TAG, "Sent mining.subscribe");
}

static void send_authorize(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    char authorize[512];
    snprintf(authorize, sizeof(authorize),
             "{\"id\":%lu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
             (unsigned long)s_req_id++, cfg->stratum_user, cfg->stratum_pass);
    esp_transport_write(s_transport, authorize, strlen(authorize), 5000);
    ESP_LOGI(TAG, "Sent mining.authorize for user=%s", cfg->stratum_user);
}

static void hex_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len && hex[i * 2] && hex[i * 2 + 1]; i++) {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        out[i] = (uint8_t)strtoul(byte, NULL, 16);
    }
}

static void handle_mining_notify(cJSON *params)
{
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 6) return;

    cJSON *p_job_id = cJSON_GetArrayItem(params, 0);
    cJSON *p_prevhash = cJSON_GetArrayItem(params, 1);
    cJSON *p_version = cJSON_GetArrayItem(params, 5);
    cJSON *p_nbits = cJSON_GetArrayItem(params, 6);
    cJSON *p_ntime = cJSON_GetArrayItem(params, 7);

    if (!p_job_id || !p_prevhash || !p_nbits) return;

    stratum_job_t job = {0};
    job.job_id = (uint32_t)atoi(p_job_id->valuestring);
    job.valid = true;

    hex_to_bytes(p_prevhash->valuestring, job.prevhash, 32);

    if (p_version && cJSON_IsString(p_version)) {
        job.version = (uint32_t)strtoul(p_version->valuestring, NULL, 16);
    }
    if (p_nbits && cJSON_IsString(p_nbits)) {
        job.nbits = (uint32_t)strtoul(p_nbits->valuestring, NULL, 16);
        s_state.nbits = job.nbits;
    }
    if (p_ntime && cJSON_IsString(p_ntime)) {
        job.ntime = (uint32_t)strtoul(p_ntime->valuestring, NULL, 16);
    }

    memset(job.target, 0xFF, 32);
    job.target_len = 32;

    tollgate_core_mining_set_current_nbits(job.nbits);
    stratum_proxy_set_job(&job);

    ESP_LOGI(TAG, "New mining job: id=%lu, nbits=0x%08lx", (unsigned long)job.job_id, (unsigned long)job.nbits);
}

static void handle_mining_set_difficulty(cJSON *params)
{
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) return;
    cJSON *diff = cJSON_GetArrayItem(params, 0);
    if (diff && cJSON_IsNumber(diff)) {
        s_state.difficulty = (uint64_t)diff->valuedouble;
        ESP_LOGI(TAG, "Pool set difficulty: %llu", (unsigned long long)s_state.difficulty);
    }
}

static void stratum_client_task(void *arg)
{
    const tollgate_config_t *cfg = tollgate_config_get();

    while (s_running) {
        if (!s_state.connected) {
            esp_err_t err = stratum_connect(cfg->stratum_host, cfg->stratum_port);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Connection failed, retrying in 10s...");
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
            send_subscribe();
            send_authorize();
        }

        char recv_buf[2048];
        int len = read_line(recv_buf, sizeof(recv_buf));
        if (len <= 0) {
            ESP_LOGW(TAG, "Connection lost");
            s_state.connected = false;
            if (s_transport) {
                esp_transport_close(s_transport);
                esp_transport_destroy(s_transport);
                s_transport = NULL;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        cJSON *root = cJSON_Parse(recv_buf);
        if (!root) continue;

        cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
        if (method && cJSON_IsString(method)) {
            cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

            if (strcmp(method->valuestring, "mining.notify") == 0) {
                handle_mining_notify(params);
            } else if (strcmp(method->valuestring, "mining.set_difficulty") == 0) {
                handle_mining_set_difficulty(params);
            }
        }

        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
        cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");

        if (id && result) {
            if (cJSON_IsFalse(result) || (error && !cJSON_IsNull(error))) {
                ESP_LOGW(TAG, "Request %d rejected", id->valueint);
            }
        }

        cJSON_Delete(root);
    }

    if (s_transport) {
        esp_transport_close(s_transport);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
    }
    s_state.connected = false;
    vTaskDelete(NULL);
}

esp_err_t stratum_client_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_req_id = 1;
    return ESP_OK;
}

esp_err_t stratum_client_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    BaseType_t ret = xTaskCreate(stratum_client_task, "stratum_cli", 8192, NULL, 4, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stratum client task");
        s_running = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Stratum client started");
    return ESP_OK;
}

void stratum_client_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_task_handle = NULL;
    }
}

esp_err_t stratum_client_submit_share(uint32_t job_id, uint32_t nonce, uint32_t ntime, uint32_t version)
{
    if (!s_state.connected || !s_transport) return ESP_FAIL;

    const tollgate_config_t *cfg = tollgate_config_get();

    char submit[512];
    snprintf(submit, sizeof(submit),
             "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%lu\",\"%08lx\",\"%08lx\",\"%08lx\"]}\n",
             (unsigned long)s_req_id++, cfg->stratum_user,
             (unsigned long)job_id, (unsigned long)ntime, (unsigned long)nonce, (unsigned long)version);

    int written = esp_transport_write(s_transport, submit, strlen(submit), 5000);
    if (written < 0) {
        ESP_LOGW(TAG, "Failed to submit share");
        s_state.shares_rejected++;
        return ESP_FAIL;
    }

    s_state.shares_accepted++;
    ESP_LOGI(TAG, "Share submitted: job=%lu nonce=%08lx", (unsigned long)job_id, (unsigned long)nonce);
    return ESP_OK;
}

const stratum_client_state_t *stratum_client_get_state(void)
{
    return &s_state;
}

void stratum_client_tick(void)
{
}
