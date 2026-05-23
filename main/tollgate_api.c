#include "tollgate_api.h"
#include "tollgate_core_cashu.h"
#include "tollgate_core_session.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core_mining.h"
#include "tollgate_core.h"
#include "config.h"
#include "identity.h"
#include "captive_portal.h"
#include "lwip/dns.h"
#include "esp_heap_caps.h"
#include "nucula_wallet.h"
#include "mint_health.h"
#include "market.h"

#include "stratum_proxy.h"
#include "stratum_client.h"
#include "tls_worker.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "tollgate_api";
static httpd_handle_t s_api_server = NULL;

static esp_err_t get_client_ip(httpd_req_t *req, uint32_t *ip_out)
{
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
        *ip_out = addr.sin_addr.s_addr;
        return ESP_OK;
    }
    return ESP_FAIL;
}

static cJSON *create_notice(const char *level, const char *code, const char *content)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "kind", 21023);
    cJSON_AddStringToObject(root, "pubkey", identity_get()->npub_hex);
    cJSON *tags = cJSON_CreateArray();
    cJSON *level_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(level_tag, cJSON_CreateString("level"));
    cJSON_AddItemToArray(level_tag, cJSON_CreateString(level));
    cJSON_AddItemToArray(tags, level_tag);
    cJSON *code_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(code_tag, cJSON_CreateString("code"));
    cJSON_AddItemToArray(code_tag, cJSON_CreateString(code));
    cJSON_AddItemToArray(tags, code_tag);
    cJSON_AddItemToObject(root, "tags", tags);
    cJSON_AddStringToObject(root, "content", content);
    return root;
}

static cJSON *create_session_event(uint32_t client_ip, uint64_t allotment_ms)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "kind", 1022);
    cJSON_AddStringToObject(root, "pubkey", identity_get()->npub_hex);

    cJSON *tags = cJSON_CreateArray();

    cJSON *p_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(p_tag, cJSON_CreateString("p"));
    cJSON_AddItemToArray(p_tag, cJSON_CreateString("unknown"));
    cJSON_AddItemToArray(tags, p_tag);

    esp_ip4_addr_t ip = { .addr = client_ip };
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
    cJSON *dev_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(dev_tag, cJSON_CreateString("device-identifier"));
    cJSON_AddItemToArray(dev_tag, cJSON_CreateString("mac"));
    cJSON_AddItemToArray(dev_tag, cJSON_CreateString(ip_str));
    cJSON_AddItemToArray(tags, dev_tag);

    cJSON *allotment_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(allotment_tag, cJSON_CreateString("allotment"));
    char allotment_str[32];
    snprintf(allotment_str, sizeof(allotment_str), "%llu", (unsigned long long)allotment_ms);
    cJSON_AddItemToArray(allotment_tag, cJSON_CreateString(allotment_str));
    cJSON_AddItemToArray(tags, allotment_tag);

    cJSON *metric_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(metric_tag, cJSON_CreateString("metric"));
    const tollgate_config_t *mcfg = tollgate_config_get();
    cJSON_AddItemToArray(metric_tag, cJSON_CreateString(mcfg->metric[0] ? mcfg->metric : "milliseconds"));
    cJSON_AddItemToArray(tags, metric_tag);

    cJSON_AddItemToObject(root, "tags", tags);
    cJSON_AddStringToObject(root, "content", "");
    return root;
}

static esp_err_t api_get_discovery(httpd_req_t *req)
{
    const tollgate_config_t *cfg = tollgate_config_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "kind", 10021);
    cJSON_AddStringToObject(root, "pubkey", identity_get()->npub_hex);

    cJSON *tags = cJSON_CreateArray();

    cJSON *metric_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(metric_tag, cJSON_CreateString("metric"));
    cJSON_AddItemToArray(metric_tag, cJSON_CreateString(cfg->metric[0] ? cfg->metric : "milliseconds"));
    cJSON_AddItemToArray(tags, metric_tag);

    cJSON *step_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(step_tag, cJSON_CreateString("step_size"));
    char step_str[32];
    bool is_bytes = (strcmp(cfg->metric, "bytes") == 0);
    snprintf(step_str, sizeof(step_str), "%d", is_bytes ? cfg->step_size_bytes : cfg->step_size_ms);
    cJSON_AddItemToArray(step_tag, cJSON_CreateString(step_str));
    cJSON_AddItemToArray(tags, step_tag);

    char price_str[32];
    snprintf(price_str, sizeof(price_str), "%d", cfg->price_per_step);

    int mint_count = 0;
    const mint_status_t *mints = mint_health_get_all(&mint_count);
    bool any_reachable = false;

    for (int i = 0; i < mint_count; i++) {
        if (!mints[i].reachable) continue;
        any_reachable = true;
        cJSON *price_tag = cJSON_CreateArray();
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("price_per_step"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("cashu"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString(price_str));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("sat"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString(mints[i].url));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("1"));
        cJSON_AddItemToArray(tags, price_tag);
    }

    if (!any_reachable) {
        cJSON *price_tag = cJSON_CreateArray();
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("price_per_step"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("cashu"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString(price_str));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("sat"));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString(cfg->mint_url));
        cJSON_AddItemToArray(price_tag, cJSON_CreateString("1"));
        cJSON_AddItemToArray(tags, price_tag);
    }

    cJSON *tips_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("tips"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("1"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("2"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("5"));
    cJSON_AddItemToArray(tags, tips_tag);

    if (cfg->mining_enabled) {
        cJSON *mining_tag = cJSON_CreateArray();
        cJSON_AddItemToArray(mining_tag, cJSON_CreateString("price_per_step"));
        cJSON_AddItemToArray(mining_tag, cJSON_CreateString("mining"));
        char mining_port_str[16];
        snprintf(mining_port_str, sizeof(mining_port_str), "%d", cfg->mining_port);
        cJSON_AddItemToArray(mining_tag, cJSON_CreateString(mining_port_str));
        cJSON_AddItemToArray(mining_tag, cJSON_CreateString("GH/s"));
        cJSON_AddItemToArray(mining_tag, cJSON_CreateString("sv1"));
        cJSON_AddItemToArray(tags, mining_tag);
    }

    cJSON_AddItemToObject(root, "tags", tags);
    cJSON_AddStringToObject(root, "content", "");

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_post_payment(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    get_client_ip(req, &client_ip);

    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 16384) {
        cJSON *notice = create_notice("error", "payment-error-invalid", "Invalid request body");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    char *body = malloc(content_len + 1);
    if (!body) {
        cJSON *notice = create_notice("error", "session-error", "Out of memory");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }
    int received = 0;
    int total = 0;
    while (total < content_len) {
        received = httpd_req_recv(req, body + total, content_len - total);
        if (received <= 0) {
            free(body);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, "bad request", 11);
            return ESP_OK;
        }
        total += received;
    }
    body[total] = '\0';

    ESP_LOGI(TAG, "Payment received: %d bytes", total);

    tg_cashu_token_t *token = malloc(sizeof(tg_cashu_token_t));
    if (!token) {
        cJSON *notice = create_notice("error", "session-error", "Out of memory");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }
    esp_err_t err = tollgate_core_cashu_decode_token(body, token);
    char *body_copy = strdup(body);
    free(body);

    if (err != ESP_OK) {
        free(token);
        cJSON *notice = create_notice("error", "payment-error-invalid", "Failed to decode Cashu token");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    const char *mint_url = token->mint_url[0] ? token->mint_url : tollgate_config_get()->mint_url;
    bool mint_accepted = false;
    {
        const tollgate_config_t *_cfg = tollgate_config_get();
        for (int i = 0; i < _cfg->accepted_mint_count; i++) {
            if (tollgate_core_cashu_is_mint_accepted(mint_url, _cfg->accepted_mints[i])) {
                mint_accepted = true;
                break;
            }
        }
    }
    if (!mint_accepted) {
        free(token);
        cJSON *notice = create_notice("error", "payment-error-mint-not-accepted", "Mint not accepted");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "402 Payment Required");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    tg_cashu_proof_state_t *states = malloc(TG_CASHU_MAX_PROOFS * sizeof(tg_cashu_proof_state_t));
    if (!states) {
        free(token);
        cJSON *notice = create_notice("error", "session-error", "Out of memory");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }
    int state_count = 0;
    err = tollgate_core_cashu_check_proof_states(mint_url, token, states, &state_count);
    ESP_LOGI(TAG, "Stack HWM after checkstate: %u", uxTaskGetStackHighWaterMark(NULL));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Checkstate failed, proceeding without spend check (wallet swap will verify)");
        state_count = 0;
    }

    for (int i = 0; i < state_count; i++) {
        if (states[i].spent) {
            free(states);
            free(token);
            cJSON *notice = create_notice("error", "payment-error-token-spent", "Token already spent");
            char *json = cJSON_PrintUnformatted(notice);
            httpd_resp_set_status(req, "402 Payment Required");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json, strlen(json));
            cJSON_free(json);
            cJSON_Delete(notice);
            return ESP_OK;
        }
    }

    const tollgate_config_t *cfg = tollgate_config_get();
    bool is_bytes = (strcmp(cfg->metric, "bytes") == 0);
    uint64_t step_size = is_bytes ? (uint64_t)cfg->step_size_bytes : (uint64_t)cfg->step_size_ms;
    uint64_t allotment = tollgate_core_cashu_calculate_allotment(token->total_amount, cfg->price_per_step,
                                                    step_size);
    if (allotment == 0) {
        free(states);
        free(token);
        cJSON *notice = create_notice("error", "payment-error-insufficient", "Token value too low");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "402 Payment Required");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    tg_session_t *session;
    if (is_bytes) {
        session = tollgate_core_session_create_bytes(client_ip, allotment);
    } else {
        session = tollgate_core_session_create(client_ip, allotment);
    }
    if (!session) {
        free(states);
        free(token);
        cJSON *notice = create_notice("error", "session-error", "Failed to create session");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    cJSON *session_event = create_session_event(client_ip, allotment);
    char *json = cJSON_PrintUnformatted(session_event);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(session_event);

    tls_worker_submit(body_copy);

    free(states);
    free(token);
    return ESP_OK;
}

static esp_err_t api_get_usage(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    get_client_ip(req, &client_ip);

    tg_session_t *session = tollgate_core_session_find_by_ip(client_ip);
    if (!session || !session->active) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "-1/-1", 5);
        return ESP_OK;
    }

    const tollgate_config_t *cfg = tollgate_config_get();
    bool is_bytes = (strcmp(cfg->metric, "bytes") == 0);

    char resp[64];
    if (is_bytes) {
        int64_t remaining = (int64_t)session->allotment_bytes - (int64_t)session->bytes_consumed;
        if (remaining < 0) remaining = 0;
        snprintf(resp, sizeof(resp), "%lld/%llu", (long long)remaining, (unsigned long long)session->allotment_bytes);
    } else {
        int64_t elapsed = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS - session->start_time_ms;
        int64_t remaining = session->allotment_ms - elapsed;
        if (remaining < 0) remaining = 0;
        snprintf(resp, sizeof(resp), "%lld/%llu", (long long)remaining, (unsigned long long)session->allotment_ms);
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t api_get_whoami(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    char resp[96];
    if (get_client_ip(req, &client_ip) == ESP_OK) {
        char mac[18] = {0};
        esp_ip4_addr_t ip = { .addr = client_ip };
        if (tollgate_core_fw_get_mac_for_ip(client_ip, mac, sizeof(mac)) == ESP_OK) {
            snprintf(resp, sizeof(resp), "ip=" IPSTR " mac=%s", IP2STR(&ip), mac);
        } else {
            snprintf(resp, sizeof(resp), "ip=" IPSTR " mac=unknown", IP2STR(&ip));
        }
    } else {
        snprintf(resp, sizeof(resp), "ip=unknown mac=unknown");
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t api_get_wallet(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "balance", (double)nucula_wallet_balance());
    cJSON_AddNumberToObject(root, "proof_count", nucula_wallet_proof_count());

    char *proofs_json = nucula_wallet_proofs_json();
    if (proofs_json) {
        cJSON *proofs = cJSON_Parse(proofs_json);
        free(proofs_json);
        cJSON_AddItemToObject(root, "proofs", proofs);
    } else {
        cJSON_AddItemToObject(root, "proofs", cJSON_CreateArray());
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_post_wallet_swap(httpd_req_t *req)
{
    if (nucula_wallet_balance() == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"no proofs to swap\"}", 27);
        return ESP_OK;
    }

    nucula_wallet_print_status();

    esp_err_t err = nucula_wallet_swap_all();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "502 Bad Gateway");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"swap failed\"}", 21);
        return ESP_OK;
    }

    nucula_wallet_print_status();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "balance", (double)nucula_wallet_balance());
    cJSON_AddNumberToObject(root, "proof_count", nucula_wallet_proof_count());
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_post_wallet_send(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 32) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "invalid amount", 14);
        return ESP_OK;
    }

    char body[32];
    int total = 0;
    while (total < content_len) {
        int r = httpd_req_recv(req, body + total, content_len - total);
        if (r <= 0) { httpd_resp_send_500(req); return ESP_OK; }
        total += r;
    }
    body[total] = '\0';

    uint64_t amount = strtoull(body, NULL, 10);
    if (amount == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "invalid amount", 14);
        return ESP_OK;
    }

    char token[4096];
    esp_err_t err = nucula_wallet_send(amount, token, sizeof(token));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "402 Payment Required");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "insufficient balance", 20);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, token, strlen(token));
    return ESP_OK;
}

static esp_err_t api_get_mints(httpd_req_t *req)
{
    int mint_count = 0;
    const mint_status_t *mints = mint_health_get_all(&mint_count);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < mint_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "url", mints[i].url);
        cJSON_AddBoolToObject(obj, "reachable", mints[i].reachable);
        cJSON_AddNumberToObject(obj, "status", mints[i].last_http_status);
        if (mints[i].last_err) {
            char errbuf[16];
            snprintf(errbuf, sizeof(errbuf), "0x%x", mints[i].last_err);
            cJSON_AddStringToObject(obj, "last_err", errbuf);
        }
        cJSON_AddItemToArray(arr, obj);
    }
    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t api_get_mining_job(httpd_req_t *req)
{
    const stratum_job_t *job = stratum_proxy_get_current_job();
    if (!job || !job->valid) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"no job\"}", 15);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "job_id", job->job_id);

    char prevhash_hex[65];
    for (int i = 0; i < 32; i++) snprintf(prevhash_hex + i * 2, 3, "%02x", job->prevhash[i]);
    cJSON_AddStringToObject(root, "prevhash", prevhash_hex);

    char merkle_hex[65];
    for (int i = 0; i < 32; i++) snprintf(merkle_hex + i * 2, 3, "%02x", job->merkle_root[i]);
    cJSON_AddStringToObject(root, "merkle_root", merkle_hex);

    cJSON_AddNumberToObject(root, "version", job->version);
    cJSON_AddNumberToObject(root, "nbits", job->nbits);
    cJSON_AddNumberToObject(root, "ntime", job->ntime);
    cJSON_AddNumberToObject(root, "hashprice", tollgate_core_mining_get_current_hashprice());

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_post_mining_share(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    get_client_ip(req, &client_ip);

    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 512) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"invalid body\"}", 21);
        return ESP_OK;
    }

    char body[512];
    int total = 0;
    while (total < content_len) {
        int r = httpd_req_recv(req, body + total, content_len - total);
        if (r <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, "bad request", 11);
            return ESP_OK;
        }
        total += r;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"invalid json\"}", 21);
        return ESP_OK;
    }

    cJSON *j_job_id = cJSON_GetObjectItem(root, "job_id");
    cJSON *j_nonce = cJSON_GetObjectItem(root, "nonce");
    cJSON *j_ntime = cJSON_GetObjectItem(root, "ntime");
    cJSON *j_version = cJSON_GetObjectItem(root, "version");
    if (!j_job_id || !j_nonce || !j_ntime || !j_version) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"missing fields\"}", 22);
        return ESP_OK;
    }

    uint32_t job_id = (uint32_t)j_job_id->valuedouble;
    uint32_t nonce = (uint32_t)j_nonce->valuedouble;
    uint32_t ntime = (uint32_t)j_ntime->valuedouble;
    uint32_t version = (uint32_t)j_version->valuedouble;
    cJSON_Delete(root);

    const stratum_job_t *job = stratum_proxy_get_current_job();
    if (!job || !job->valid || job->job_id != job_id) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"stale job\"}", 19);
        return ESP_OK;
    }

    esp_err_t share_err = stratum_client_submit_share(job_id, nonce, ntime, version);
    bool accepted = (share_err == ESP_OK);

    tollgate_core_mining_update_hashrate(client_ip, accepted);
    tollgate_mining_client_stats_t *stats = tollgate_core_mining_get_or_create_client(client_ip);

    if (accepted) {
        const tollgate_config_t *cfg = tollgate_config_get();
        double hashprice = tollgate_core_mining_get_current_hashprice();
        uint64_t allotment_ms = tollgate_core_mining_shares_to_allotment_ms(
            stats->hashrate_ghs, hashprice, cfg->price_per_step, cfg->step_size_ms);

        tg_session_t *session = tollgate_core_session_find_by_ip(client_ip);
        if (!session || !session->active || session->payment_method != TG_PAYMENT_MINING) {
            session = tollgate_core_session_create(client_ip, allotment_ms);
            if (session) session->payment_method = TG_PAYMENT_MINING;
        } else {
            tollgate_core_session_extend(session, allotment_ms);
        }
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "accepted", accepted);
    cJSON_AddNumberToObject(resp, "hashrate_ghs", stats ? stats->hashrate_ghs : 0.0);
    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(resp);
    return ESP_OK;
}

static esp_err_t api_get_mining_stats(httpd_req_t *req)
{
    stratum_proxy_stats_t proxy_stats;
    stratum_proxy_get_stats(&proxy_stats);

    const stratum_client_state_t *client_state = stratum_client_get_state();

    cJSON *root = cJSON_CreateObject();

    cJSON *proxy = cJSON_CreateObject();
    cJSON_AddNumberToObject(proxy, "hashrate_ghs", proxy_stats.hashrate_ghs);
    cJSON_AddNumberToObject(proxy, "total_shares", (double)proxy_stats.total_shares);
    cJSON_AddNumberToObject(proxy, "total_accepted", (double)proxy_stats.total_accepted);
    cJSON_AddNumberToObject(proxy, "total_rejected", (double)proxy_stats.total_rejected);
    cJSON_AddNumberToObject(proxy, "hashprice", proxy_stats.current_hashprice);
    cJSON_AddNumberToObject(proxy, "active_miners", proxy_stats.active_miners);
    cJSON_AddItemToObject(root, "proxy", proxy);

    cJSON *upstream = cJSON_CreateObject();
    cJSON_AddBoolToObject(upstream, "connected", client_state->connected);
    cJSON_AddStringToObject(upstream, "pool_host", client_state->pool_host);
    cJSON_AddNumberToObject(upstream, "pool_port", client_state->pool_port);
    cJSON_AddNumberToObject(upstream, "difficulty", (double)client_state->difficulty);
    cJSON_AddNumberToObject(upstream, "shares_accepted", (double)client_state->shares_accepted);
    cJSON_AddNumberToObject(upstream, "shares_rejected", (double)client_state->shares_rejected);
    cJSON_AddItemToObject(root, "upstream", upstream);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

extern bool s_start_services_called;
extern bool s_start_ap_services_called;
extern bool s_sta_got_ip;
extern bool s_ap_started;
extern esp_ip4_addr_t s_sta_ip;
extern esp_ip4_addr_t s_sta_gw;

static esp_err_t api_get_debug(httpd_req_t *req)
{
    httpd_handle_t portal = captive_portal_get_server();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "portal_running", portal != NULL);
    cJSON_AddBoolToObject(root, "portal_start_called", captive_portal_was_start_called());
    cJSON_AddNumberToObject(root, "portal_start_result", captive_portal_get_start_result());
    cJSON_AddBoolToObject(root, "start_services_called", s_start_services_called);
    cJSON_AddBoolToObject(root, "start_ap_services_called", s_start_ap_services_called);
    cJSON_AddBoolToObject(root, "sta_got_ip", s_sta_got_ip);
    cJSON_AddBoolToObject(root, "ap_started", s_ap_started);
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", (double)esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "free_internal", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "largest_internal", (double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "free_spiram", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    char dns0[16], dns1[16], dns2[16];
    const ip_addr_t *d0 = dns_getserver(0);
    const ip_addr_t *d1 = dns_getserver(1);
    const ip_addr_t *d2 = dns_getserver(2);
    snprintf(dns0, sizeof(dns0), IPSTR, IP2STR(&(esp_ip4_addr_t){.addr=d0->addr}));
    snprintf(dns1, sizeof(dns1), IPSTR, IP2STR(&(esp_ip4_addr_t){.addr=d1->addr}));
    snprintf(dns2, sizeof(dns2), IPSTR, IP2STR(&(esp_ip4_addr_t){.addr=d2->addr}));
    cJSON_AddStringToObject(root, "dns0", dns0);
    cJSON_AddStringToObject(root, "dns1", dns1);
    cJSON_AddStringToObject(root, "dns2", dns2);

    char sta_ip_str[16], sta_gw_str[16];
    snprintf(sta_ip_str, sizeof(sta_ip_str), IPSTR, IP2STR(&s_sta_ip));
    snprintf(sta_gw_str, sizeof(sta_gw_str), IPSTR, IP2STR(&s_sta_gw));
    cJSON_AddStringToObject(root, "sta_ip", sta_ip_str);
    cJSON_AddStringToObject(root, "sta_gw", sta_gw_str);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_discovery = { .uri = "/", .method = HTTP_GET, .handler = api_get_discovery };
static const httpd_uri_t uri_payment = { .uri = "/", .method = HTTP_POST, .handler = api_post_payment };
static const httpd_uri_t uri_mints = { .uri = "/mints", .method = HTTP_GET, .handler = api_get_mints };
static const httpd_uri_t uri_usage = { .uri = "/usage", .method = HTTP_GET, .handler = api_get_usage };
static const httpd_uri_t uri_whoami = { .uri = "/whoami", .method = HTTP_GET, .handler = api_get_whoami };
static const httpd_uri_t uri_wallet = { .uri = "/wallet", .method = HTTP_GET, .handler = api_get_wallet };
static const httpd_uri_t uri_wallet_swap = { .uri = "/wallet/swap", .method = HTTP_POST, .handler = api_post_wallet_swap };
static const httpd_uri_t uri_wallet_send = { .uri = "/wallet/send", .method = HTTP_POST, .handler = api_post_wallet_send };
static const httpd_uri_t uri_mining_job = { .uri = "/mining/job", .method = HTTP_GET, .handler = api_get_mining_job };
static const httpd_uri_t uri_mining_share = { .uri = "/mining/share", .method = HTTP_POST, .handler = api_post_mining_share };
static const httpd_uri_t uri_mining_stats = { .uri = "/mining/stats", .method = HTTP_GET, .handler = api_get_mining_stats };

static esp_err_t api_get_market(httpd_req_t *req)
{
    const market_t *mkt = market_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "count", mkt->count);
    cJSON_AddNumberToObject(root, "last_scan_s", (double)(mkt->last_scan_ms / 1000));

    cJSON *entries = cJSON_CreateArray();
    for (int i = 0; i < MARKET_MAX_ENTRIES; i++) {
        if (!mkt->entries[i].valid) continue;
        const market_entry_t *e = &mkt->entries[i];

        cJSON *entry = cJSON_CreateObject();
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 e->bssid[0], e->bssid[1], e->bssid[2],
                 e->bssid[3], e->bssid[4], e->bssid[5]);
        cJSON_AddStringToObject(entry, "bssid", bssid_str);
        cJSON_AddStringToObject(entry, "ssid", e->ssid[0] ? e->ssid : "unknown");
        cJSON_AddNumberToObject(entry, "rssi", e->rssi);
        cJSON_AddNumberToObject(entry, "price_per_step", e->price_per_step);
        cJSON_AddNumberToObject(entry, "step_size", (double)e->step_size);
        cJSON_AddStringToObject(entry, "metric", e->metric ? "bytes" : "milliseconds");
        if (e->geohash[0]) cJSON_AddStringToObject(entry, "geohash", e->geohash);
        cJSON_AddItemToArray(entries, entry);
    }
    cJSON_AddItemToObject(root, "entries", entries);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_market = { .uri = "/market", .method = HTTP_GET, .handler = api_get_market };
static const httpd_uri_t uri_debug = { .uri = "/debug", .method = HTTP_GET, .handler = api_get_debug };

esp_err_t tollgate_api_start(void)
{
    if (s_api_server) return ESP_OK;

     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
     config.server_port = 2121;
     config.ctrl_port = 32769;
     config.max_uri_handlers = 16;
     config.stack_size = 16384;
     config.core_id = 0;

    esp_err_t ret = httpd_start(&s_api_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start API server: %s (heap: %lu)", esp_err_to_name(ret), (unsigned long)esp_get_free_heap_size());
        s_api_server = NULL;
        return ret;
    }

    httpd_register_uri_handler(s_api_server, &uri_discovery);
    httpd_register_uri_handler(s_api_server, &uri_debug);
    httpd_register_uri_handler(s_api_server, &uri_payment);
    httpd_register_uri_handler(s_api_server, &uri_mints);
    httpd_register_uri_handler(s_api_server, &uri_usage);
    httpd_register_uri_handler(s_api_server, &uri_whoami);
    httpd_register_uri_handler(s_api_server, &uri_wallet);
    httpd_register_uri_handler(s_api_server, &uri_wallet_swap);
    httpd_register_uri_handler(s_api_server, &uri_wallet_send);
    httpd_register_uri_handler(s_api_server, &uri_market);

    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg->mining_enabled) {
        httpd_register_uri_handler(s_api_server, &uri_mining_job);
        httpd_register_uri_handler(s_api_server, &uri_mining_share);
        httpd_register_uri_handler(s_api_server, &uri_mining_stats);
    }

    ESP_LOGI(TAG, "TollGate API started on port 2121");

    return ESP_OK;
}

void tollgate_api_stop(void)
{
    if (s_api_server) {
        httpd_stop(s_api_server);
        s_api_server = NULL;
    }
}
