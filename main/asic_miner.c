#include "asic_miner.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "asic_miner";
static bool s_present = false;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static double s_hashrate = 0.0;

static void asic_miner_task(void *arg)
{
    ESP_LOGI(TAG, "ASIC miner task started (stub)");
    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

esp_err_t asic_miner_init(void)
{
    s_present = false;
    ESP_LOGI(TAG, "ASIC miner initialized - no ASIC detected (software fallback)");
    return ESP_OK;
}

bool asic_miner_is_present(void)
{
    return s_present;
}

esp_err_t asic_miner_start(void)
{
    if (!s_present) {
        ESP_LOGW(TAG, "No ASIC present, cannot start");
        return ESP_FAIL;
    }

    s_running = true;
    BaseType_t ret = xTaskCreate(asic_miner_task, "asic_miner", 4096, NULL, 3, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ASIC task");
        s_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void asic_miner_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task_handle = NULL;
    }
}

double asic_miner_get_hashrate(void)
{
    return s_hashrate;
}
