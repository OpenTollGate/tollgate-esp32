#include "relay_selector.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "relay_sel";
static const int MAX_REDIRECTS = 3;
static const int PROBE_TIMEOUT_MS = 5000;
static const int MAX_FAILURES = 3;

static int compare_relays(const void *a, const void *b)
{
    const relay_info_t *ra = (const relay_info_t *)a;
    const relay_info_t *rb = (const relay_info_t *)b;

    if (ra->alive && !rb->alive) return -1;
    if (!ra->alive && rb->alive) return 1;

    int score_a = (ra->supports_nip77 ? 1000 : 0) - ra->consecutive_failures * 100;
    int score_b = (rb->supports_nip77 ? 1000 : 0) - rb->consecutive_failures * 100;
    if (score_a != score_b) return score_b - score_a;

    return (int)ra->latency_ms - (int)rb->latency_ms;
}

esp_err_t relay_selector_init(relay_selector_t *sel)
{
    memset(sel, 0, sizeof(relay_selector_t));
    sel->primary_idx = -1;
    sel->fallback_idx = -1;
    sel->lock = xSemaphoreCreateMutex();
    if (!sel->lock) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void relay_selector_destroy(relay_selector_t *sel)
{
    if (sel->lock) { vSemaphoreDelete(sel->lock); sel->lock = NULL; }
}

static esp_err_t probe_nip11(const char *wss_url, relay_info_t *info)
{
    char http_url[192];
    const char *host_start = wss_url;
    if (strncmp(wss_url, "wss://", 6) == 0) host_start = wss_url + 6;
    else if (strncmp(wss_url, "ws://", 5) == 0) host_start = wss_url + 5;

    snprintf(http_url, sizeof(http_url), "https://%s/", host_start);

    char response[4096];
    int total_len = 0;

    esp_http_client_config_t http_cfg = {
        .url = http_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = PROBE_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .max_redirection_count = MAX_REDIRECTS,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Accept", "application/nostr+json");

    int64_t start_time = esp_timer_get_time();
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        info->alive = false;
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        info->alive = (status > 0);
        return ESP_FAIL;
    }

    int max_read = content_length > 0 ? content_length : (int)sizeof(response) - 1;
    if (max_read > (int)sizeof(response) - 1) max_read = (int)sizeof(response) - 1;

    while (total_len < max_read) {
        int read_len = esp_http_client_read(client, response + total_len,
                                            max_read - total_len);
        if (read_len <= 0) break;
        total_len += read_len;
    }
    response[total_len] = '\0';

    int64_t end_time = esp_timer_get_time();
    info->latency_ms = (uint32_t)((end_time - start_time) / 1000);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    info->alive = true;
    info->consecutive_failures = 0;

    cJSON *root = cJSON_Parse(response);
    if (!root) return ESP_OK;

    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
        strncpy(info->name, name->valuestring, sizeof(info->name) - 1);

    cJSON *nips = cJSON_GetObjectItem(root, "supported_nips");
    if (nips && cJSON_IsArray(nips)) {
        info->nips_count = cJSON_GetArraySize(nips);
        if (info->nips_count > 32) info->nips_count = 32;
        info->supports_nip77 = false;
        for (size_t i = 0; i < info->nips_count; i++) {
            cJSON *nip = cJSON_GetArrayItem(nips, i);
            if (nip) {
                info->supported_nips[i] = (uint8_t)nip->valueint;
                if (nip->valueint == 77) info->supports_nip77 = true;
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static void select_primary_fallback(relay_selector_t *sel)
{
    relay_info_t sorted[RELAY_SELECTOR_MAX_RELAYS];
    size_t sorted_count = 0;

    for (size_t i = 0; i < sel->count; i++) {
        if (sel->relays[i].alive) {
            sorted[sorted_count++] = sel->relays[i];
        }
    }

    if (sorted_count == 0) {
        sel->primary_idx = -1;
        sel->fallback_idx = -1;
        return;
    }

    qsort(sorted, sorted_count, sizeof(relay_info_t), compare_relays);

    for (size_t i = 0; i < sel->count; i++) {
        if (strcmp(sel->relays[i].url, sorted[0].url) == 0) {
            sel->primary_idx = (int)i;
            break;
        }
    }

    if (sorted_count > 1) {
        for (size_t i = 0; i < sel->count; i++) {
            if (strcmp(sel->relays[i].url, sorted[1].url) == 0) {
                sel->fallback_idx = (int)i;
                break;
            }
        }
    } else {
        sel->fallback_idx = -1;
    }

    ESP_LOGI(TAG, "Primary: %s (latency=%lums, NIP-77=%s)",
             sel->primary_idx >= 0 ? sel->relays[sel->primary_idx].url : "none",
             sel->primary_idx >= 0 ? (unsigned long)sel->relays[sel->primary_idx].latency_ms : 0,
             sel->primary_idx >= 0 && sel->relays[sel->primary_idx].supports_nip77 ? "yes" : "no");
}

esp_err_t relay_selector_probe_all(relay_selector_t *sel)
{
    xSemaphoreTake(sel->lock, portMAX_DELAY);

    ESP_LOGI(TAG, "Probing %zu relays via NIP-11...", sel->count);

    for (size_t i = 0; i < sel->count; i++) {
        ESP_LOGI(TAG, "Probing %s...", sel->relays[i].url);
        esp_err_t err = probe_nip11(sel->relays[i].url, &sel->relays[i]);
        if (err != ESP_OK) {
            sel->relays[i].consecutive_failures++;
            ESP_LOGW(TAG, "Probe failed for %s (failures=%d)",
                     sel->relays[i].url, sel->relays[i].consecutive_failures);
            if (sel->relays[i].consecutive_failures >= MAX_FAILURES) {
                sel->relays[i].alive = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    select_primary_fallback(sel);

    int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    sel->last_full_probe = (uint32_t)now;

    xSemaphoreGive(sel->lock);
    return ESP_OK;
}

const relay_info_t *relay_selector_get_primary(relay_selector_t *sel)
{
    if (sel->primary_idx < 0 || sel->primary_idx >= (int)sel->count) return NULL;
    return &sel->relays[sel->primary_idx];
}

const relay_info_t *relay_selector_get_fallback(relay_selector_t *sel, int idx)
{
    if (idx == 0) {
        if (sel->fallback_idx < 0) return NULL;
        return &sel->relays[sel->fallback_idx];
    }
    for (size_t i = 0; i < sel->count; i++) {
        if ((int)i != sel->primary_idx && (int)i != sel->fallback_idx) {
            if (sel->relays[i].alive) {
                if (idx <= 0) return &sel->relays[i];
                idx--;
            }
        }
    }
    return NULL;
}

void relay_selector_report_disconnect(relay_selector_t *sel, const char *url)
{
    xSemaphoreTake(sel->lock, portMAX_DELAY);
    for (size_t i = 0; i < sel->count; i++) {
        if (strcmp(sel->relays[i].url, url) == 0) {
            sel->relays[i].consecutive_failures++;
            ESP_LOGW(TAG, "Disconnect reported for %s (failures=%d)",
                     url, sel->relays[i].consecutive_failures);
            if (sel->relays[i].consecutive_failures >= MAX_FAILURES) {
                sel->relays[i].alive = false;
                ESP_LOGW(TAG, "Relay %s marked dead, triggering re-probe", url);
                select_primary_fallback(sel);
            }
            break;
        }
    }
    xSemaphoreGive(sel->lock);
}

esp_err_t relay_selector_seed_from_config(relay_selector_t *sel)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    xSemaphoreTake(sel->lock, portMAX_DELAY);

    sel->count = 0;
    for (int i = 0; i < cfg->nostr_seed_relay_count && sel->count < RELAY_SELECTOR_MAX_RELAYS; i++) {
        if (cfg->nostr_seed_relays[i][0] != '\0') {
            strncpy(sel->relays[sel->count].url, cfg->nostr_seed_relays[i],
                    RELAY_SELECTOR_URL_LEN - 1);
            sel->relays[sel->count].alive = true;
            sel->count++;
        }
    }

    xSemaphoreGive(sel->lock);
    ESP_LOGI(TAG, "Seeded %zu relays from config", sel->count);
    return ESP_OK;
}
