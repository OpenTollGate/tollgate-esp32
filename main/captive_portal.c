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
static char s_ap_ip_str[16] = "10.0.0.1";

static const char PORTAL_HTML_TEMPLATE[] = \
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
".price{background:#252525;border-radius:12px;padding:16px;margin-bottom:16px}"
".price-amount{font-size:36px;font-weight:bold;color:#f7931a}"
".price-unit{color:#888;font-size:14px}"
".mints{background:#252525;border-radius:12px;padding:12px;margin-bottom:16px;text-align:left}"
".mints-title{color:#888;font-size:12px;margin-bottom:8px}"
".mint-url{font-family:monospace;font-size:11px;color:#f7931a;word-break:break-all;"
"background:#1a1a1a;padding:8px;border-radius:6px;position:relative;cursor:pointer}"
".mint-url:active{opacity:0.7}"
".mint-hint{color:#666;font-size:10px;margin-top:4px}"
"#status{margin-top:16px;padding:12px;border-radius:8px;display:none;font-size:14px}"
"#status.success{display:block;background:#1a472a;color:#4caf50}"
"#status.error{display:block;background:#471a1a;color:#f44336}"
"#status.processing{display:block;background:#1a3a47;color:#2196f3}"
".btn{background:#f7931a;color:#000;border:none;border-radius:8px;padding:14px 28px;"
"font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px}"
".btn:hover{background:#e8850f}"
".btn:disabled{background:#333;color:#666;cursor:not-allowed}"
"textarea{width:100%;height:80px;background:#252525;border:1px solid #333;border-radius:8px;"
"color:#fff;padding:12px;font-family:monospace;font-size:12px;margin-top:8px;resize:none}"
"</style>"
"</head><body>"
"<div class='card'>"
"<h1>TollGate</h1>"
"<p class='subtitle'>Pay for internet access with ecash</p>"
"<div class='price'>"
"<div class='price-amount' id='price'>Loading...</div>"
"<div class='price-unit'>sats per minute</div>"
"</div>"
"<div class='mints'>"
"<div class='mints-title'>SUPPORTED MINTS</div>"
"<div class='mint-url' id='mintUrl' onclick='copyMint()'>Loading...</div>"
"<div class='mint-hint'>Tap to copy &bull; Mint tokens at this URL before paying</div>"
"</div>"
"<textarea id='tokenInput' placeholder='Paste your Cashu token here (cashuA...)'></textarea>"
"<button class='btn' id='payBtn' onclick='payToken()'>Pay & Connect</button>"
"<div id='status'></div>"
"</div>"
"<script>"
"const priceEl=document.getElementById('price');"
"const statusEl=document.getElementById('status');"
"const payBtn=document.getElementById('payBtn');"
"const tokenInput=document.getElementById('tokenInput');"
"const mintUrlEl=document.getElementById('mintUrl');"
"fetch('http://__AP_IP__:2121/').then(r=>r.json()).then(d=>{"
"if(d.tags){"
"const p=d.tags.find(t=>t[0]==='price_per_step');if(p){priceEl.textContent=p[2]||'21';"
"if(p[4]){mintUrlEl.textContent=p[4];}}"
"}"
"}).catch(()=>{priceEl.textContent='21';mintUrlEl.textContent='Error loading mint URL';});"
"function copyMint(){"
"const url=mintUrlEl.textContent;"
"if(navigator.clipboard){navigator.clipboard.writeText(url);"
"mintUrlEl.textContent='Copied!';setTimeout(()=>{mintUrlEl.textContent=url;},1000);}"
"}"
"function showStatus(msg,type){statusEl.textContent=msg;statusEl.className=type;}"
"function payToken(){"
"const token=tokenInput.value.trim();"
"if(!token||!token.startsWith('cashuA')){showStatus('Please paste a valid Cashu token','error');return;}"
"payBtn.disabled=true;"
"showStatus('Processing payment...','processing');"
"fetch('http://__AP_IP__:2121/',{method:'POST',body:token}).then(r=>{"
"if(r.ok)return r.json();"
"return r.json().then(d=>{throw new Error(d.content||'Payment failed');});"
"}).then(d=>{"
"if(d.kind===1022){showStatus('Connected! You have internet access.','success');payBtn.textContent='Connected!';"
"setTimeout(()=>{window.location.href='http://detectportal.firefox.com/success.txt';},2000);}"
"else if(d.kind===21023){showStatus('Error: '+(d.content||'Unknown error'),'error');payBtn.disabled=false;}"
"}).catch(e=>{showStatus(e.message||'Connection error','error');payBtn.disabled=false;});"
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

static esp_err_t portal_handler(httpd_req_t *req);

static esp_err_t portal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s from client", req->uri);
    httpd_resp_set_type(req, "text/html");

    char *html = NULL;
    const char *tpl = PORTAL_HTML_TEMPLATE;
    size_t tpl_len = strlen(tpl);
    int count = 0;
    const char *p = tpl;
    while ((p = strstr(p, "__AP_IP__")) != NULL) { count++; p += 9; }

    size_t ip_len = strlen(s_ap_ip_str);
    html = malloc(tpl_len + count * (ip_len > 9 ? ip_len - 9 : 0) + 1);
    if (!html) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    char *out = html;
    const char *src = tpl;
    while (*src) {
        const char *found = strstr(src, "__AP_IP__");
        if (found) {
            memcpy(out, src, found - src);
            out += found - src;
            memcpy(out, s_ap_ip_str, ip_len);
            out += ip_len;
            src = found + 9;
        } else {
            strcpy(out, src);
            out += strlen(src);
            break;
        }
    }
    *out = '\0';

    httpd_resp_send(req, html, out - html);
    free(html);
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
    char resp[96];
    if (get_client_ip(req, &client_ip) == ESP_OK) {
        char mac[18] = {0};
        esp_ip4_addr_t ip = { .addr = client_ip };
        if (firewall_get_mac_for_ip(client_ip, mac, sizeof(mac)) == ESP_OK) {
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

static esp_err_t redirect_to_portal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive detect: GET %s → 200 portal HTML", req->uri);
    return portal_handler(req);
}

static esp_err_t catchall_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Catchall: GET %s → 302 → http://%s/", req->uri, s_ap_ip_str);
    httpd_resp_set_status(req, "302 Found");

    char location[64];
    snprintf(location, sizeof(location), "http://%s/", s_ap_ip_str);
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_portal = { .uri = "/", .method = HTTP_GET, .handler = portal_handler };
static const httpd_uri_t uri_grant = { .uri = "/grant_access", .method = HTTP_GET, .handler = grant_access_handler };
static const httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler };
static const httpd_uri_t uri_whoami = { .uri = "/whoami", .method = HTTP_GET, .handler = whoami_handler };
static const httpd_uri_t uri_usage = { .uri = "/usage", .method = HTTP_GET, .handler = usage_handler };
static const httpd_uri_t uri_reset = { .uri = "/reset_authentication", .method = HTTP_GET, .handler = reset_auth_handler };
static const httpd_uri_t uri_gen204 = { .uri = "/generate_204", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_hotspot = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_canonical = { .uri = "/canonical.html", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_success = { .uri = "/success.txt", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_ncsi = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_connecttest = { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_wpad = { .uri = "/wpad.dat", .method = HTTP_GET, .handler = redirect_to_portal_handler };
static const httpd_uri_t uri_catchall = { .uri = "/*", .method = HTTP_GET, .handler = catchall_handler };

esp_err_t captive_portal_start(const char *ap_ip_str)
{
    if (s_server) return ESP_OK;
    strncpy(s_ap_ip_str, ap_ip_str, sizeof(s_ap_ip_str) - 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
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
    httpd_register_uri_handler(s_server, &uri_gen204);
    httpd_register_uri_handler(s_server, &uri_hotspot);
    httpd_register_uri_handler(s_server, &uri_canonical);
    httpd_register_uri_handler(s_server, &uri_success);
    httpd_register_uri_handler(s_server, &uri_ncsi);
    httpd_register_uri_handler(s_server, &uri_connecttest);
    httpd_register_uri_handler(s_server, &uri_wpad);
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
