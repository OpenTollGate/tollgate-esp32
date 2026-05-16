#include "lnurl_pay.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "lnurl_pay";

static esp_err_t http_get_json(const char *url, char *resp_buf, size_t resp_buf_size, int *status_out)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
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

esp_err_t lnurl_get_invoice(const char *lightning_address, uint64_t amount_sats,
                            char *bolt11_out, size_t bolt11_out_size)
{
    if (!lightning_address || !bolt11_out) return ESP_FAIL;

    const char *at = strchr(lightning_address, '@');
    if (!at) {
        ESP_LOGE(TAG, "Invalid lightning address: missing '@'");
        return ESP_FAIL;
    }

    size_t user_len = at - lightning_address;
    char username[64];
    if (user_len >= sizeof(username)) return ESP_FAIL;
    memcpy(username, lightning_address, user_len);
    username[user_len] = '\0';

    const char *domain = at + 1;

    char url[512];
    snprintf(url, sizeof(url), "https://%s/.well-known/lnurlp/%s", domain, username);

    ESP_LOGI(TAG, "LNURL-pay step 1: GET %s", url);

    char *resp_buf = malloc(4096);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    int status = 0;
    esp_err_t err = http_get_json(url, resp_buf, 4096, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "LNURL-pay step 1 failed: status=%d err=%s", status, esp_err_to_name(err));
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "LNURL-pay step 1: invalid JSON");
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON *callback = cJSON_GetObjectItemCaseSensitive(root, "callback");
    if (!callback || !cJSON_IsString(callback)) {
        ESP_LOGE(TAG, "LNURL-pay step 1: missing callback");
        cJSON_Delete(root);
        free(resp_buf);
        return ESP_FAIL;
    }

    char callback_url[512];
    strncpy(callback_url, callback->valuestring, sizeof(callback_url) - 1);

    cJSON *min_sendable = cJSON_GetObjectItemCaseSensitive(root, "minSendable");
    cJSON *max_sendable = cJSON_GetObjectItemCaseSensitive(root, "maxSendable");

    uint64_t amount_msat = amount_sats * 1000;
    if (min_sendable && cJSON_IsNumber(min_sendable) && amount_msat < (uint64_t)min_sendable->valuedouble) {
        ESP_LOGE(TAG, "Amount %llumsat below min %g", (unsigned long long)amount_msat, min_sendable->valuedouble);
        cJSON_Delete(root);
        free(resp_buf);
        return ESP_FAIL;
    }
    if (max_sendable && cJSON_IsNumber(max_sendable) && amount_msat > (uint64_t)max_sendable->valuedouble) {
        ESP_LOGE(TAG, "Amount %llumsat above max %g", (unsigned long long)amount_msat, max_sendable->valuedouble);
        cJSON_Delete(root);
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    char callback_with_amount[768];
    snprintf(callback_with_amount, sizeof(callback_with_amount), "%s%samount=%llu",
             callback_url, strchr(callback_url, '?') ? "&" : "?",
             (unsigned long long)amount_msat);

    free(resp_buf);

    ESP_LOGI(TAG, "LNURL-pay step 2: GET %s", callback_with_amount);

    resp_buf = malloc(4096);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    err = http_get_json(callback_with_amount, resp_buf, 4096, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "LNURL-pay step 2 failed: status=%d", status);
        free(resp_buf);
        return ESP_FAIL;
    }

    root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) return ESP_FAIL;

    cJSON *pr = cJSON_GetObjectItemCaseSensitive(root, "pr");
    if (!pr || !cJSON_IsString(pr)) {
        ESP_LOGE(TAG, "LNURL-pay step 2: missing 'pr' (bolt11)");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t pr_len = strlen(pr->valuestring);
    if (pr_len >= bolt11_out_size) {
        ESP_LOGE(TAG, "BOLT11 too long: %zu >= %zu", pr_len, bolt11_out_size);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memcpy(bolt11_out, pr->valuestring, pr_len + 1);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Got BOLT11 invoice (%zu bytes) for %llu sats", pr_len, (unsigned long long)amount_sats);
    return ESP_OK;
}
