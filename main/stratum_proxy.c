#include "stratum_proxy.h"
#include "mining_payment.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "stratum_proxy";
static uint16_t s_port = 3333;
static bool s_running = false;
static TaskHandle_t s_task_handle = NULL;
static int s_server_fd = -1;

static stratum_job_t s_current_job = {0};
static stratum_proxy_stats_t s_stats = {0};

static void proxy_client_handler(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &addr_len);
    uint32_t client_ip = client_addr.sin_addr.s_addr;

    ESP_LOGI(TAG, "Miner connected from 0x%08lx", (unsigned long)client_ip);

    if (s_current_job.valid) {
        char job_json[512];
        snprintf(job_json, sizeof(job_json),
                 "{\"id\":1,\"method\":\"mining.notify\",\"params\":[\"%lu\",\"%08lx%08lx%08lx%08lx%08lx%08lx%08lx%08lx\",\"\",\"\",\"\",\"%08lx\",\"%08lx\",\"%08lx\",true]}\n",
                 (unsigned long)s_current_job.job_id,
                 (unsigned long)0, (unsigned long)0, (unsigned long)0, (unsigned long)0,
                 (unsigned long)0, (unsigned long)0, (unsigned long)0, (unsigned long)0,
                 (unsigned long)s_current_job.nbits, (unsigned long)s_current_job.ntime,
                 (unsigned long)s_current_job.version);
        send(client_fd, job_json, strlen(job_json), 0);
    }

    char buf[1024];
    while (s_running) {
        int len = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        ESP_LOGI(TAG, "Received from miner: %s", buf);
        s_stats.total_shares++;
        s_stats.total_accepted++;
    }

    ESP_LOGI(TAG, "Miner disconnected from 0x%08lx", (unsigned long)client_ip);
    close(client_fd);
    vTaskDelete(NULL);
}

static void proxy_server_task(void *arg)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(s_port);

    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(s_server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind to port %u", (unsigned)s_port);
        close(s_server_fd);
        s_server_fd = -1;
        vTaskDelete(NULL);
        return;
    }

    if (listen(s_server_fd, 5) != 0) {
        ESP_LOGE(TAG, "Failed to listen");
        close(s_server_fd);
        s_server_fd = -1;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Stratum proxy listening on port %u", (unsigned)s_port);

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        s_stats.active_miners++;
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "miner_%d", client_fd);
        xTaskCreate(proxy_client_handler, task_name, 4096, (void *)(intptr_t)client_fd, 3, NULL);
    }

    close(s_server_fd);
    s_server_fd = -1;
    vTaskDelete(NULL);
}

esp_err_t stratum_proxy_init(uint16_t port)
{
    s_port = port;
    memset(&s_current_job, 0, sizeof(s_current_job));
    memset(&s_stats, 0, sizeof(s_stats));
    s_running = true;

    BaseType_t ret = xTaskCreate(proxy_server_task, "stratum_proxy", 4096, NULL, 4, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create proxy task");
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stratum proxy initialized on port %u", (unsigned)port);
    return ESP_OK;
}

void stratum_proxy_set_job(const stratum_job_t *job)
{
    if (job) {
        memcpy(&s_current_job, job, sizeof(stratum_job_t));
        s_stats.nbits = job->nbits;
        s_stats.current_hashprice = mining_get_current_hashprice();
    }
}

const stratum_job_t *stratum_proxy_get_current_job(void)
{
    return &s_current_job;
}

void stratum_proxy_get_stats(stratum_proxy_stats_t *stats)
{
    if (stats) {
        *stats = s_stats;
        stats->current_hashprice = mining_get_current_hashprice();
    }
}

void stratum_proxy_stop(void)
{
    s_running = false;
    if (s_server_fd >= 0) {
        close(s_server_fd);
        s_server_fd = -1;
    }
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(500));
        s_task_handle = NULL;
    }
}
