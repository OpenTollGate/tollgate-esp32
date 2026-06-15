#include "remote_miner.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "remote_miner";

static volatile bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static StackType_t *s_stack_buffer = NULL;
static StaticTask_t s_task_buffer;
static char s_gw_ip[16] = {0};
static double s_hashrate = 0.0;

typedef struct {
    uint32_t job_id;
    uint8_t prevhash[32];
    uint8_t merkle_root[32];
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    uint8_t target[32];
    int target_len;
    bool valid;
} remote_job_t;

static void sha256d(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t tmp[32];
    mbedtls_sha256(data, len, tmp, 0);
    mbedtls_sha256(tmp, 32, out, 0);
}

static void nbits_to_target(uint32_t nbits, uint8_t target[32], int *target_len)
{
    *target_len = 32;
    memset(target, 0, 32);

    int exp = (int)((nbits >> 24) & 0xFF);
    uint32_t mantissa = nbits & 0x007FFFFF;

    int pos = 32 - exp;
    if (pos < 0) pos = 0;

    for (int i = 0; i < 3 && (pos + i) < 32; i++) {
        target[pos + i] = (uint8_t)((mantissa >> (8 * (2 - i))) & 0xFF);
    }
}

static void build_header(const remote_job_t *job, uint32_t nonce, uint8_t out[80])
{
    memset(out, 0, 80);
    out[0] = (job->version >> 0) & 0xFF;
    out[1] = (job->version >> 8) & 0xFF;
    out[2] = (job->version >> 16) & 0xFF;
    out[3] = (job->version >> 24) & 0xFF;
    memcpy(out + 4, job->prevhash, 32);
    memcpy(out + 36, job->merkle_root, 32);
    out[68] = (job->ntime >> 0) & 0xFF;
    out[69] = (job->ntime >> 8) & 0xFF;
    out[70] = (job->ntime >> 16) & 0xFF;
    out[71] = (job->ntime >> 24) & 0xFF;
    out[72] = (job->nbits >> 0) & 0xFF;
    out[73] = (job->nbits >> 8) & 0xFF;
    out[74] = (job->nbits >> 16) & 0xFF;
    out[75] = (job->nbits >> 24) & 0xFF;
    out[76] = (nonce >> 0) & 0xFF;
    out[77] = (nonce >> 8) & 0xFF;
    out[78] = (nonce >> 16) & 0xFF;
    out[79] = (nonce >> 24) & 0xFF;
}

static bool check_pow(const uint8_t header[80], const uint8_t *target, int target_len)
{
    uint8_t hash[32];
    sha256d(header, 80, hash);

    uint8_t hash_be[32];
    for (int i = 0; i < 32; i++) hash_be[i] = hash[31 - i];

    for (int i = 0; i < target_len && i < 32; i++) {
        if (hash_be[i] < target[i]) return true;
        if (hash_be[i] > target[i]) return false;
    }
    return true;
}

static int hex_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len && hex[i * 2] && hex[i * 2 + 1]; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

static esp_err_t fetch_job(remote_job_t *job)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s:2121/mining/job", s_gw_ip);

    char *resp = malloc(2048);
    if (!resp) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(resp); return ESP_FAIL; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    int resp_len = esp_http_client_read(client, resp, 2047);
    esp_http_client_cleanup(client);

    if (resp_len < 0 || status != 200) {
        ESP_LOGD(TAG, "fetch_job failed: status=%d len=%d", status, resp_len);
        free(resp);
        return ESP_FAIL;
    }
    resp[resp_len] = '\0';

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_FAIL;

    memset(job, 0, sizeof(*job));

    cJSON *j_job_id = cJSON_GetObjectItem(root, "job_id");
    cJSON *j_prevhash = cJSON_GetObjectItem(root, "prevhash");
    cJSON *j_merkle = cJSON_GetObjectItem(root, "merkle_root");
    cJSON *j_version = cJSON_GetObjectItem(root, "version");
    cJSON *j_nbits = cJSON_GetObjectItem(root, "nbits");
    cJSON *j_ntime = cJSON_GetObjectItem(root, "ntime");

    if (!j_job_id || !j_prevhash || !j_merkle || !j_version || !j_nbits || !j_ntime) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    job->job_id = (uint32_t)j_job_id->valuedouble;
    hex_to_bytes(j_prevhash->valuestring, job->prevhash, 32);
    hex_to_bytes(j_merkle->valuestring, job->merkle_root, 32);
    job->version = (uint32_t)j_version->valuedouble;
    job->nbits = (uint32_t)j_nbits->valuedouble;
    job->ntime = (uint32_t)j_ntime->valuedouble;

    cJSON_Delete(root);

    nbits_to_target(job->nbits, job->target, &job->target_len);
    job->valid = true;

    return ESP_OK;
}

static esp_err_t submit_share(uint32_t job_id, uint32_t nonce, uint32_t ntime, uint32_t version)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s:2121/mining/share", s_gw_ip);

    char body[256];
    snprintf(body, sizeof(body),
             "{\"job_id\":%lu,\"nonce\":%lu,\"ntime\":%lu,\"version\":%lu}",
             (unsigned long)job_id, (unsigned long)nonce,
             (unsigned long)ntime, (unsigned long)version);

    char resp[256];

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_write(client, body, strlen(body));
    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;
    int status = esp_http_client_get_status_code(client);
    int resp_len = esp_http_client_read(client, resp, sizeof(resp) - 1);
    esp_http_client_cleanup(client);

    if (resp_len >= 0) resp[resp_len] = '\0';

    if (status == 200) {
        ESP_LOGI(TAG, "Share accepted: job=%lu nonce=%08lx",
                 (unsigned long)job_id, (unsigned long)nonce);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Share rejected: status=%d body=%s", status, resp_len > 0 ? resp : "(empty)");
    return ESP_FAIL;
}

static void remote_miner_task(void *arg)
{
    ESP_LOGI(TAG, "Remote miner started, gateway=%s", s_gw_ip);

    uint64_t hashes = 0;
    int64_t start_time = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    remote_job_t local_job = {0};
    uint32_t last_job_id = 0;

    while (s_running) {
        esp_err_t err = fetch_job(&local_job);
        if (err != ESP_OK || !local_job.valid) {
            ESP_LOGD(TAG, "No job available, waiting...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (local_job.job_id != last_job_id) {
            ESP_LOGI(TAG, "New job: id=%lu nbits=0x%08lx ntime=0x%08lx",
                     (unsigned long)local_job.job_id,
                     (unsigned long)local_job.nbits,
                     (unsigned long)local_job.ntime);
            last_job_id = local_job.job_id;
        }

        uint8_t header[80];
        uint32_t start_nonce = esp_random();
        uint32_t end_nonce = start_nonce + 1000;

        for (uint32_t nonce = start_nonce; nonce < end_nonce && s_running; nonce++) {
            build_header(&local_job, nonce, header);
            hashes++;

            if (check_pow(header, local_job.target, local_job.target_len)) {
                submit_share(local_job.job_id, nonce, local_job.ntime, local_job.version);
                break;
            }
        }

        int64_t now = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        int64_t elapsed_s = (now - start_time) / 1000;
        if (elapsed_s > 0) {
            s_hashrate = (double)hashes / (double)elapsed_s / 1e6;
        }

        taskYIELD();
    }

    vTaskDelete(NULL);
}

esp_err_t remote_miner_start(const char *gw_ip)
{
    if (s_running) {
        if (strcmp(s_gw_ip, gw_ip) == 0) return ESP_OK;
        remote_miner_stop();
    }

    strncpy(s_gw_ip, gw_ip, sizeof(s_gw_ip) - 1);
    s_gw_ip[sizeof(s_gw_ip) - 1] = '\0';
    s_running = true;
    s_hashrate = 0.0;

    if (s_stack_buffer) {
        free(s_stack_buffer);
        s_stack_buffer = NULL;
    }

    s_stack_buffer = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_INTERNAL);
    if (!s_stack_buffer) {
        ESP_LOGE(TAG, "Failed to allocate stack");
        s_running = false;
        return ESP_FAIL;
    }

    s_task_handle = xTaskCreateStatic(remote_miner_task, "remote_miner", 4096, NULL, 2,
                                       s_stack_buffer, &s_task_buffer);
    if (!s_task_handle) {
        ESP_LOGE(TAG, "Failed to create remote_miner task");
        free(s_stack_buffer);
        s_stack_buffer = NULL;
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Remote miner started for gateway %s", gw_ip);
    return ESP_OK;
}

void remote_miner_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task_handle = NULL;
    }
    if (s_stack_buffer) {
        free(s_stack_buffer);
        s_stack_buffer = NULL;
    }
    s_gw_ip[0] = '\0';
}

bool remote_miner_is_running(void)
{
    return s_running;
}

double remote_miner_get_hashrate(void)
{
    return s_hashrate;
}
