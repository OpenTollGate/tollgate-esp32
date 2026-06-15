#include "tollgate_client.h"
#include "config.h"
#include "nucula_wallet.h"
#include "market.h"
#include "remote_miner.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tg_client";

static tollgate_client_state_t s_state = TG_CLIENT_IDLE;
static tollgate_discovery_t s_discovery = {0};
static char s_gw_ip[TG_CLIENT_MAX_GW_IP_LEN] = {0};
static int64_t s_allotment_ms = 0;
static int64_t s_remaining_ms = -1;
static int64_t s_last_pay_time_ms = 0;
static int s_retry_count = 0;

static int64_t get_time_ms(void) {
    return (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static esp_err_t http_get(const char *url, char *resp_buf, size_t resp_buf_size, int *status_out)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;
    int status = esp_http_client_get_status_code(client);
    if (status_out) *status_out = status;

    int resp_len = esp_http_client_read(client, resp_buf, resp_buf_size - 1);
    esp_http_client_cleanup(client);

    if (resp_len < 0) return ESP_FAIL;
    resp_buf[resp_len] = '\0';
    return ESP_OK;
}

static esp_err_t http_post_text(const char *url, const char *body, char *resp_buf, size_t resp_buf_size, int *status_out)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_write(client, body, strlen(body));

    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;
    int status = esp_http_client_get_status_code(client);
    if (status_out) *status_out = status;

    int resp_len = esp_http_client_read(client, resp_buf, resp_buf_size - 1);
    esp_http_client_cleanup(client);

    if (resp_len < 0) return ESP_FAIL;
    resp_buf[resp_len] = '\0';
    return ESP_OK;
}

static bool parse_discovery_response(const char *json_str, tollgate_discovery_t *out)
{
    return tollgate_core_client_parse_discovery(json_str, out);
}

static bool parse_session_response(const char *json_str, int64_t *allotment_ms_out)
{
    return tollgate_core_client_parse_session(json_str, allotment_ms_out);
}

static bool parse_usage_response(const char *resp, int64_t *remaining_out, int64_t *total_out)
{
    return tollgate_core_client_parse_usage(resp, remaining_out, total_out);
}

esp_err_t tollgate_client_detect(const char *gw_ip, tollgate_discovery_t *discovery)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:2121/", gw_ip);

    char *resp_buf = malloc(4096);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    int status = 0;
    esp_err_t err = http_get(url, resp_buf, 4096, &status);

    if (err != ESP_OK || status != 200) {
        ESP_LOGI(TAG, "detect: no TollGate at %s (status=%d, err=%s)", gw_ip, status, esp_err_to_name(err));
        free(resp_buf);
        return ESP_ERR_NOT_FOUND;
    }

    bool found = parse_discovery_response(resp_buf, discovery);
    free(resp_buf);

    if (found && discovery->is_tollgate) {
        ESP_LOGI(TAG, "TollGate detected at %s: price=%d sats, step=%dms, mint=%s, metric=%s",
                 gw_ip, discovery->price_per_step, discovery->step_size_ms,
                 discovery->mint_url, discovery->metric);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "detect: response at %s not a TollGate", gw_ip);
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t tollgate_client_pay(const char *gw_ip, int amount_sats, int64_t *allotment_ms_out)
{
    uint64_t balance = nucula_wallet_balance();
    if (balance < (uint64_t)amount_sats) {
        ESP_LOGW(TAG, "insufficient balance: %llu < %d", (unsigned long long)balance, amount_sats);
        return ESP_ERR_INVALID_STATE;
    }

    char token_buf[8192];
    esp_err_t err = nucula_wallet_send((uint64_t)amount_sats, token_buf, sizeof(token_buf));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wallet send failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "created token (%d sats), posting to %s:2121", amount_sats, gw_ip);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:2121/", gw_ip);

    char *resp_buf = malloc(8192);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    int status = 0;
    err = http_post_text(url, token_buf, resp_buf, 8192, &status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "payment POST failed: %s", esp_err_to_name(err));
        free(resp_buf);
        return err;
    }

    ESP_LOGI(TAG, "payment response: status=%d, body=%s", status, resp_buf);

    int64_t allotment = 0;
    if (status == 200 && parse_session_response(resp_buf, &allotment)) {
        *allotment_ms_out = allotment;
        ESP_LOGI(TAG, "payment accepted: allotment=%lldms", (long long)allotment);
        free(resp_buf);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "payment rejected: status=%d", status);
    free(resp_buf);
    return ESP_FAIL;
}

static esp_err_t tollgate_client_query_usage(const char *gw_ip, int64_t *remaining_ms, int64_t *total_ms)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:2121/usage", gw_ip);

    char resp_buf[256];
    int status = 0;
    esp_err_t err = http_get(url, resp_buf, sizeof(resp_buf), &status);
    if (err != ESP_OK || status != 200) {
        return ESP_FAIL;
    }

    return parse_usage_response(resp_buf, remaining_ms, total_ms) ? ESP_OK : ESP_FAIL;
}

esp_err_t tollgate_client_init(void)
{
    s_state = TG_CLIENT_IDLE;
    memset(&s_discovery, 0, sizeof(s_discovery));
    memset(s_gw_ip, 0, sizeof(s_gw_ip));
    s_allotment_ms = 0;
    s_remaining_ms = -1;
    s_last_pay_time_ms = 0;
    s_retry_count = 0;
    return ESP_OK;
}

esp_err_t tollgate_client_on_sta_connected(const char *gw_ip_str)
{
    const tollgate_config_t *cfg = tollgate_config_get();

    if (!cfg->client_enabled) {
        ESP_LOGI(TAG, "client disabled, skipping upstream detection");
        return ESP_OK;
    }

    strncpy(s_gw_ip, gw_ip_str, sizeof(s_gw_ip) - 1);
    s_state = TG_CLIENT_DETECTING;
    s_retry_count = 0;

    ESP_LOGI(TAG, "detecting upstream TollGate at %s", gw_ip_str);

    esp_err_t err = tollgate_client_detect(gw_ip_str, &s_discovery);
    if (err != ESP_OK) {
        s_state = TG_CLIENT_NO_TOLLGATE;
        ESP_LOGI(TAG, "no upstream TollGate detected");
        return ESP_OK;
    }

    remote_miner_start(gw_ip_str);

    s_state = TG_CLIENT_NEEDS_PAY;

    int steps = cfg->client_steps_to_buy;
    if (steps <= 0) steps = 1;
    int amount_sats = steps * s_discovery.price_per_step;

    s_state = TG_CLIENT_PAYING;
    int64_t allotment = 0;
    err = tollgate_client_pay(gw_ip_str, amount_sats, &allotment);
    if (err != ESP_OK) {
        s_state = TG_CLIENT_ERROR;
        ESP_LOGE(TAG, "upstream payment failed");
        return err;
    }

    s_allotment_ms = allotment;
    s_remaining_ms = allotment;
    s_last_pay_time_ms = get_time_ms();
    s_state = TG_CLIENT_PAID;

    ESP_LOGI(TAG, "upstream TollGate paid: %lldms allotment", (long long)allotment);

    const market_t *mkt = market_get();
    int cheapest = market_find_cheapest();
    if (cheapest >= 0 && mkt->entries[cheapest].valid && mkt->entries[cheapest].ssid[0] != '\0') {
        int upstream_eff = tollgate_core_client_calc_price_per_min(s_discovery.price_per_step, s_discovery.step_size_ms);
        int cheap_eff = tollgate_core_client_calc_price_per_min(mkt->entries[cheapest].price_per_step, mkt->entries[cheapest].step_size);
        if (cheap_eff < upstream_eff) {
            ESP_LOGW(TAG, "CHEAPER TOLLGATE AVAILABLE: %s at %d sats/min vs upstream %d sats/min",
                     mkt->entries[cheapest].ssid, cheap_eff, upstream_eff);
        }
    }
    return ESP_OK;
}

void tollgate_client_on_sta_disconnected(void)
{
    ESP_LOGI(TAG, "STA disconnected, resetting client state");
    remote_miner_stop();
    s_state = TG_CLIENT_IDLE;
    memset(&s_discovery, 0, sizeof(s_discovery));
    memset(s_gw_ip, 0, sizeof(s_gw_ip));
    s_allotment_ms = 0;
    s_remaining_ms = -1;
    s_last_pay_time_ms = 0;
    s_retry_count = 0;
}

void tollgate_client_tick(void)
{
    if (s_state != TG_CLIENT_PAID && s_state != TG_CLIENT_RENEWING && s_state != TG_CLIENT_ERROR) {
        return;
    }

    if (s_state == TG_CLIENT_ERROR) {
        const tollgate_config_t *cfg = tollgate_config_get();
        int64_t now = get_time_ms();
        int64_t elapsed = now - s_last_pay_time_ms;
        if (elapsed < cfg->client_retry_interval_ms) return;

        if (s_gw_ip[0] == '\0') return;

        s_state = TG_CLIENT_PAYING;
        int steps = cfg->client_steps_to_buy;
        if (steps <= 0) steps = 1;
        int amount_sats = steps * s_discovery.price_per_step;

        int64_t allotment = 0;
        esp_err_t err = tollgate_client_pay(s_gw_ip, amount_sats, &allotment);
        if (err == ESP_OK) {
            s_allotment_ms = allotment;
            s_remaining_ms = allotment;
            s_last_pay_time_ms = get_time_ms();
            s_state = TG_CLIENT_PAID;
            s_retry_count = 0;
            ESP_LOGI(TAG, "retry payment succeeded: %lldms", (long long)allotment);
        } else {
            s_last_pay_time_ms = get_time_ms();
            s_retry_count++;
            s_state = TG_CLIENT_ERROR;
            ESP_LOGW(TAG, "retry payment failed (attempt %d)", s_retry_count);
        }
        return;
    }

    if (s_gw_ip[0] == '\0') return;

    int64_t remaining = 0, total = 0;
    esp_err_t err = tollgate_client_query_usage(s_gw_ip, &remaining, &total);
    if (err == ESP_OK) {
        s_remaining_ms = remaining;
        s_allotment_ms = total;
    }

    const tollgate_config_t *cfg = tollgate_config_get();
    int threshold_pct = cfg->client_renewal_threshold_pct;

    if (tollgate_core_client_should_renew(s_remaining_ms, s_allotment_ms, threshold_pct)) {
        ESP_LOGI(TAG, "session nearing expiry (%lld/%lldms, %d%%), renewing",
                 (long long)s_remaining_ms, (long long)s_allotment_ms,
                 (int)((s_remaining_ms * 100) / s_allotment_ms));

        s_state = TG_CLIENT_RENEWING;
        int steps = cfg->client_steps_to_buy;
        if (steps <= 0) steps = 1;
        int amount_sats = steps * s_discovery.price_per_step;

        int64_t allotment = 0;
        err = tollgate_client_pay(s_gw_ip, amount_sats, &allotment);
        if (err == ESP_OK) {
            s_allotment_ms = allotment;
            s_remaining_ms = allotment;
            s_last_pay_time_ms = get_time_ms();
            s_state = TG_CLIENT_PAID;
            ESP_LOGI(TAG, "renewal succeeded: %lldms", (long long)allotment);
        } else {
            s_state = TG_CLIENT_ERROR;
            s_last_pay_time_ms = get_time_ms();
            ESP_LOGE(TAG, "renewal payment failed");
        }
    }
}

tollgate_client_state_t tollgate_client_get_state(void)
{
    return s_state;
}

const tollgate_discovery_t *tollgate_client_get_discovery(void)
{
    return &s_discovery;
}

int64_t tollgate_client_get_remaining_ms(void)
{
    return s_remaining_ms;
}

int64_t tollgate_client_get_allotment_ms(void)
{
    return s_allotment_ms;
}
