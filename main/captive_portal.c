#include "captive_portal.h"
#include "firewall.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "captive_portal";
static httpd_handle_t s_server = NULL;

static const char PORTAL_HTML[] = \
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>TollGate</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:#0a0a0a;color:#fff;display:flex;align-items:center;justify-content:center;"
"min-height:100vh;padding:20px}"
".card{background:#1a1a1a;border:1px solid #333;border-radius:16px;padding:32px;"
"max-width:400px;width:100%;text-align:center}"
"h1{font-size:28px;margin-bottom:8px;color:#f7931a}"
".subtitle{color:#888;margin-bottom:24px;font-size:14px}"
".price{background:#252525;border-radius:12px;padding:16px;margin-bottom:24px}"
".price-amount{font-size:36px;font-weight:bold;color:#f7931a}"
".price-unit{color:#888;font-size:14px}"
"#status{margin-top:16px;padding:12px;border-radius:8px;display:none;font-size:14px}"
"#status.success{display:block;background:#1a472a;color:#4caf50}"
"#status.error{display:block;background:#471a1a;color:#f44336}"
"#status.processing{display:block;background:#1a3a47;color:#2196f3}"
".btn{background:#f7931a;color:#000;border:none;border-radius:8px;padding:14px 28px;"
"font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px}"
".btn:hover{background:#e8850f}"
".btn:disabled{background:#333;color:#666;cursor:not-allowed}"
"</style>"
"</head><body>"
"<div class='card'>"
"<h1>TollGate</h1>"
"<p class='subtitle'>Pay for internet access with ecash</p>"
"<div class='price'>"
"<div class='price-amount' id='price'>Loading...</div>"
"<div class='price-unit'>sats per minute</div>"
"</div>"
"<button class='btn' id='grantBtn' onclick='grantAccess()'>Grant Free Access</button>"
"<div id='status'></div>"
"</div>"
"<script>"
"const priceEl=document.getElementById('price');"
"const statusEl=document.getElementById('status');"
"const grantBtn=document.getElementById('grantBtn');"
"fetch('/api/status').then(r=>r.json()).then(d=>{priceEl.textContent=d.price||'21';}).catch(()=>{priceEl.textContent='21';});"
"function showStatus(msg,type){statusEl.textContent=msg;statusEl.className=type;}"
"function grantAccess(){"
"  grantBtn.disabled=true;"
"  showStatus('Connecting...','processing');"
"  fetch('/grant_access').then(r=>r.json()).then(d=>{"
"    if(d.status==='granted'){"
"      showStatus('Connected! You have internet access.','success');"
"      grantBtn.textContent='Connected!';"
"      setTimeout(()=>{window.location.href='http://detectportal.firefox.com/success.txt';},2000);"
"    }else{showStatus('Error: '+d.message,'error');grantBtn.disabled=false;}"
"  }).catch(e=>{showStatus('Connection error','error');grantBtn.disabled=false;});"
"}"
"</script>"
"</body></html>";

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

static bool is_captive_detection_uri(const char *uri)
{
    return strcmp(uri, "/generate_204") == 0 ||
           strcmp(uri, "/hotspot-detect.html") == 0 ||
           strcmp(uri, "/canonical.html") == 0 ||
           strcmp(uri, "/success.txt") == 0 ||
           strcmp(uri, "/ncsi.txt") == 0 ||
           strcmp(uri, "/connecttest.txt") == 0 ||
           strcmp(uri, "/wpad.dat") == 0 ||
           strcmp(uri, "/redirect") == 0;
}

static esp_err_t portal_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, strlen(PORTAL_HTML));
    return ESP_OK;
}

static esp_err_t grant_access_handler(httpd_req_t *req)
{
    uint32_t client_ip;
    if (get_client_ip(req, &client_ip) == ESP_OK) {
        firewall_grant_access(client_ip);
    }
    const char *resp = "{\"status\":\"granted\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", true);
    cJSON_AddNumberToObject(root, "price", cfg->price_per_step);
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t whoami_handler(httpd_req_t *req)
{
    uint32_t client_ip;
    char resp[64];
    if (get_client_ip(req, &client_ip) == ESP_OK) {
        esp_ip4_addr_t ip = { .addr = client_ip };
        snprintf(resp, sizeof(resp), "mac=" IPSTR, IP2STR(&ip));
    } else {
        snprintf(resp, sizeof(resp), "mac=unknown");
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t usage_handler(httpd_req_t *req)
{
    uint32_t client_ip;
    char resp[32];
    if (get_client_ip(req, &client_ip) == ESP_OK && firewall_is_client_allowed(client_ip)) {
        snprintf(resp, sizeof(resp), "0/0");
    } else {
        snprintf(resp, sizeof(resp), "-1/-1");
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t reset_auth_handler(httpd_req_t *req)
{
    firewall_revoke_all();
    const char *resp = "{\"status\":\"reset\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t catchall_handler(httpd_req_t *req)
{
    if (is_captive_detection_uri(req->uri)) {
        return portal_handler(req);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_portal = { .uri = "/", .method = HTTP_GET, .handler = portal_handler };
static const httpd_uri_t uri_grant = { .uri = "/grant_access", .method = HTTP_GET, .handler = grant_access_handler };
static const httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler };
static const httpd_uri_t uri_whoami = { .uri = "/whoami", .method = HTTP_GET, .handler = whoami_handler };
static const httpd_uri_t uri_usage = { .uri = "/usage", .method = HTTP_GET, .handler = usage_handler };
static const httpd_uri_t uri_reset = { .uri = "/reset_authentication", .method = HTTP_GET, .handler = reset_auth_handler };
static const httpd_uri_t uri_catchall = { .uri = "/*", .method = HTTP_GET, .handler = catchall_handler };

esp_err_t captive_portal_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_register_uri_handler(s_server, &uri_portal);
    httpd_register_uri_handler(s_server, &uri_grant);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_whoami);
    httpd_register_uri_handler(s_server, &uri_usage);
    httpd_register_uri_handler(s_server, &uri_reset);
    httpd_register_uri_handler(s_server, &uri_catchall);

    ESP_LOGI(TAG, "Captive portal started on port 80");
    return ESP_OK;
}

void captive_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

httpd_handle_t captive_portal_get_server(void)
{
    return s_server;
}
