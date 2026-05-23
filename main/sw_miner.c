#include "sw_miner.h"
#include "stratum_proxy.h"
#include "stratum_client.h"
#include "tollgate_core_mining.h"
#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sw_miner";
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static double s_hashrate = 0.0;

static void sha256d(const uint8_t *data, size_t len, uint8_t *hash)
{
    uint8_t tmp[32];
    mbedtls_sha256(data, len, tmp, 0);
    mbedtls_sha256(tmp, 32, hash, 0);
}

static void sw_miner_task(void *arg)
{
    ESP_LOGI(TAG, "Software miner started");

    uint64_t hashes = 0;
    int64_t start_time = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

    uint8_t header[80];
    uint8_t hash[32];

    while (s_running) {
        const stratum_job_t *job = stratum_proxy_get_current_job();
        if (!job || !job->valid) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        stratum_job_t local_job;
        memcpy(&local_job, job, sizeof(stratum_job_t));

        memcpy(header, local_job.prevhash, 32);
        memcpy(header + 32, local_job.merkle_root, 32);

        uint32_t start_nonce = esp_random();
        uint32_t end_nonce = start_nonce + 1000;

        for (uint32_t nonce = start_nonce; nonce < end_nonce && s_running; nonce++) {
            header[76] = (nonce >> 0) & 0xFF;
            header[77] = (nonce >> 8) & 0xFF;
            header[78] = (nonce >> 16) & 0xFF;
            header[79] = (nonce >> 24) & 0xFF;

            sha256d(header, 80, hash);
            hashes++;

            if (memcmp(hash, local_job.target, local_job.target_len) <= 0) {
                ESP_LOGI(TAG, "Valid share found! nonce=%08lx", (unsigned long)nonce);
                stratum_client_submit_share(local_job.job_id, nonce, local_job.ntime, local_job.version);
                tollgate_core_mining_update_hashrate(0, true);
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

esp_err_t sw_miner_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    s_hashrate = 0.0;

    BaseType_t ret = xTaskCreate(sw_miner_task, "sw_miner", 8192, NULL, 2, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sw_miner task");
        s_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void sw_miner_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task_handle = NULL;
    }
}

bool sw_miner_is_running(void)
{
    return s_running;
}

double sw_miner_get_hashrate(void)
{
    return s_hashrate;
}
