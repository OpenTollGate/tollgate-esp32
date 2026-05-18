#include "local_relay.h"
#include "storage_engine.h"
#include "sub_manager.h"
#include "rate_limiter.h"
#include "ws_server.h"
#include "relay_core.h"
#include "router.h"
#include "handlers.h"
#include "broadcaster.h"
#include "flash_monitor.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "local_relay";

#define LOCAL_RELAY_PORT 4869
#define LOCAL_RELAY_TTL_SEC (21 * 24 * 3600)

static relay_ctx_t s_relay_ctx;
static storage_engine_t s_storage;
static sub_manager_t s_sub_mgr;
static rate_limiter_t s_rate_limiter;
static bool s_initialized = false;

relay_ctx_t g_relay_ctx;

static void on_ws_message(int fd, const char *data, size_t len)
{
    router_dispatch(&g_relay_ctx, fd, data, len);
}

static void on_ws_disconnect(int fd)
{
    if (g_relay_ctx.sub_manager) {
        sub_manager_remove_all(g_relay_ctx.sub_manager, fd);
    }
}

esp_err_t local_relay_init(void)
{
    memset(&s_relay_ctx, 0, sizeof(s_relay_ctx));
    memset(&s_storage, 0, sizeof(s_storage));
    memset(&s_sub_mgr, 0, sizeof(s_sub_mgr));
    memset(&s_rate_limiter, 0, sizeof(s_rate_limiter));

    esp_err_t ret = storage_init(&s_storage, LOCAL_RELAY_TTL_SEC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sub_manager_init(&s_sub_mgr);
    if (ret != ESP_OK) {
        storage_destroy(&s_storage);
        return ret;
    }

    rate_config_t rl_cfg = {
        .events_per_minute = 60,
        .reqs_per_minute = 30,
    };
    rate_limiter_init(&s_rate_limiter, &rl_cfg);

    s_relay_ctx.storage = &s_storage;
    s_relay_ctx.sub_manager = &s_sub_mgr;
    s_relay_ctx.rate_limiter = &s_rate_limiter;
    s_relay_ctx.config.port = LOCAL_RELAY_PORT;
    s_relay_ctx.config.max_event_age_sec = LOCAL_RELAY_TTL_SEC;
    s_relay_ctx.config.max_subs_per_conn = 8;
    s_relay_ctx.config.max_filters_per_sub = 4;
    s_relay_ctx.config.max_future_sec = 600;

    memcpy(&g_relay_ctx, &s_relay_ctx, sizeof(relay_ctx_t));

    storage_start_cleanup_task(&s_storage);

    s_initialized = true;
    ESP_LOGI(TAG, "Local relay initialized (port=%d, TTL=%ds)", LOCAL_RELAY_PORT, LOCAL_RELAY_TTL_SEC);
    return ESP_OK;
}

void local_relay_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }

    esp_err_t ret = ws_server_init(&s_relay_ctx.ws_server, LOCAL_RELAY_PORT, on_ws_message);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WS server: %s", esp_err_to_name(ret));
        return;
    }

    ws_server_set_disconnect_cb(on_ws_disconnect);
    memcpy(&g_relay_ctx, &s_relay_ctx, sizeof(relay_ctx_t));

    ESP_LOGI(TAG, "Local relay listening on port %d", LOCAL_RELAY_PORT);
}

void local_relay_stop(void)
{
    if (!s_initialized) return;
    ws_server_stop(&s_relay_ctx.ws_server);
    ESP_LOGI(TAG, "Local relay stopped");
}

esp_err_t local_relay_publish(const char *event_json, size_t event_len)
{
    if (!s_initialized || !event_json) return ESP_ERR_INVALID_STATE;

    storage_error_t err = storage_save_event_json(s_relay_ctx.storage, event_json, event_len);
    if (err == STORAGE_ERR_DUPLICATE) {
        ESP_LOGD(TAG, "Duplicate event, skipping broadcast");
        return ESP_OK;
    }
    if (err != STORAGE_OK) {
        ESP_LOGW(TAG, "Failed to save event: %d", err);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_ParseWithLength(event_json, event_len);
    if (!obj) return ESP_OK;

    cJSON *pk = cJSON_GetObjectItem(obj, "pubkey");
    cJSON *kind = cJSON_GetObjectItem(obj, "kind");
    cJSON *ca = cJSON_GetObjectItem(obj, "created_at");

    if (pk && kind && ca) {
        broadcaster_fanout_json(&s_relay_ctx, event_json, event_len,
                                kind->valueint, pk->valuestring,
                                (uint64_t)ca->valuedouble);
    }
    cJSON_Delete(obj);

    return ESP_OK;
}
