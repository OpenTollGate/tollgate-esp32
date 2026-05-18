#include "tollgate_api.h"
#include "tollgate_core.h"
#include "config.h"
#include "nucula_wallet.h"
#include "esp_log.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tollgate_api";
static httpd_handle_t s_api_server = NULL;

static const char *TOLLGATE_PUBKEY = "0000000000000000000000000000000000000000000000000000000000000000";

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
    cJSON_AddStringToObject(root, "pubkey", TOLLGATE_PUBKEY);
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
    cJSON_AddStringToObject(root, "pubkey", TOLLGATE_PUBKEY);

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
    cJSON_AddStringToObject(root, "pubkey", TOLLGATE_PUBKEY);

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

    cJSON *price_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("price_per_step"));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("cashu"));
    char price_str[32];
    snprintf(price_str, sizeof(price_str), "%d", cfg->price_per_step);
    cJSON_AddItemToArray(price_tag, cJSON_CreateString(price_str));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("sat"));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString(cfg->mint_url));
    cJSON_AddItemToArray(price_tag, cJSON_CreateString("1"));
    cJSON_AddItemToArray(tags, price_tag);

    cJSON *tips_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("tips"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("1"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("2"));
    cJSON_AddItemToArray(tips_tag, cJSON_CreateString("5"));
    cJSON_AddItemToArray(tags, tips_tag);

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

    esp_err_t err = tollgate_core_process_payment(client_ip, body);
    free(body);

    if (err != ESP_OK) {
        cJSON *notice = create_notice("error", "payment-error", "Payment processing failed");
        char *json = cJSON_PrintUnformatted(notice);
        httpd_resp_set_status(req, "402 Payment Required");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        cJSON_free(json);
        cJSON_Delete(notice);
        return ESP_OK;
    }

    const tollgate_config_t *cfg = tollgate_config_get();
    uint64_t allotment = 0;
    cJSON *session_event = create_session_event(client_ip, allotment);
    char *json = cJSON_PrintUnformatted(session_event);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(session_event);
    return ESP_OK;
}

static esp_err_t api_get_usage(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    get_client_ip(req, &client_ip);

    char *status_json = tollgate_core_get_status_json();
    if (status_json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, status_json, strlen(status_json));
        cJSON_free(status_json);
    } else {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "{}", 2);
    }
    return ESP_OK;
}

static esp_err_t api_get_whoami(httpd_req_t *req)
{
    uint32_t client_ip = 0;
    char resp[96];
    if (get_client_ip(req, &client_ip) == ESP_OK) {
        esp_ip4_addr_t ip = { .addr = client_ip };
        snprintf(resp, sizeof(resp), "ip=" IPSTR, IP2STR(&ip));
    } else {
        snprintf(resp, sizeof(resp), "ip=unknown");
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

static const httpd_uri_t uri_discovery = { .uri = "/", .method = HTTP_GET, .handler = api_get_discovery };
static const httpd_uri_t uri_payment = { .uri = "/", .method = HTTP_POST, .handler = api_post_payment };
static const httpd_uri_t uri_usage = { .uri = "/usage", .method = HTTP_GET, .handler = api_get_usage };
static const httpd_uri_t uri_whoami = { .uri = "/whoami", .method = HTTP_GET, .handler = api_get_whoami };
static const httpd_uri_t uri_wallet = { .uri = "/wallet", .method = HTTP_GET, .handler = api_get_wallet };
static const httpd_uri_t uri_wallet_swap = { .uri = "/wallet/swap", .method = HTTP_POST, .handler = api_post_wallet_swap };
static const httpd_uri_t uri_wallet_send = { .uri = "/wallet/send", .method = HTTP_POST, .handler = api_post_wallet_send };

esp_err_t tollgate_api_start(void)
{
    if (s_api_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 2121;
    config.ctrl_port = 32769;
    config.max_uri_handlers = 10;
    config.stack_size = 16384;

    esp_err_t ret = httpd_start(&s_api_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start API server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_register_uri_handler(s_api_server, &uri_discovery);
    httpd_register_uri_handler(s_api_server, &uri_payment);
    httpd_register_uri_handler(s_api_server, &uri_usage);
    httpd_register_uri_handler(s_api_server, &uri_whoami);
    httpd_register_uri_handler(s_api_server, &uri_wallet);
    httpd_register_uri_handler(s_api_server, &uri_wallet_swap);
    httpd_register_uri_handler(s_api_server, &uri_wallet_send);

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
