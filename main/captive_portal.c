#include "captive_portal.h"
#include "firewall.h"
#include "session.h"
#include "config.h"
#include "mining_payment.h"
#include "stratum_proxy.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
".tabs{display:flex;gap:4px;margin-bottom:20px}"
".tab{flex:1;padding:10px;border:none;border-radius:8px;background:#252525;color:#888;"
"cursor:pointer;font-size:13px;font-weight:bold}"
".tab.active{background:#f7931a;color:#000}"
".tab-content{display:none}"
".tab-content.active{display:block}"
".price{background:#252525;border-radius:12px;padding:16px;margin-bottom:16px}"
".price-amount{font-size:36px;font-weight:bold;color:#f7931a}"
".price-unit{color:#888;font-size:14px}"
"textarea{width:100%;height:80px;background:#252525;border:1px solid #333;border-radius:8px;"
"color:#fff;padding:12px;font-family:monospace;font-size:12px;resize:none}"
".btn{background:#f7931a;color:#000;border:none;border-radius:8px;padding:14px 28px;"
"font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px}"
".btn:hover{background:#e8850f}"
".btn:disabled{background:#333;color:#666;cursor:not-allowed}"
".mints{background:#252525;border-radius:12px;padding:12px;margin-top:16px;text-align:left}"
".mints-title{color:#888;font-size:12px;margin-bottom:8px}"
".mint-url{font-family:monospace;font-size:11px;color:#f7931a;word-break:break-all;"
"background:#1a1a1a;padding:8px;border-radius:6px;cursor:pointer}"
".mint-url:active{opacity:0.7}"
".mint-hint{color:#666;font-size:10px;margin-top:4px}"
".mining-stats{background:#252525;border-radius:12px;padding:16px;margin-bottom:16px;text-align:left}"
".mining-stat{display:flex;justify-content:space-between;margin-bottom:8px;font-size:13px}"
".mining-stat .label{color:#888}"
".mining-stat .value{color:#f7931a;font-weight:bold}"
"#status{margin-top:16px;padding:12px;border-radius:8px;display:none;font-size:14px}"
"#status.success{display:block;background:#1a472a;color:#4caf50}"
"#status.error{display:block;background:#471a1a;color:#f44336}"
"#status.processing{display:block;background:#1a3a47;color:#2196f3}"
".mining-info{color:#666;font-size:11px;margin-top:12px;line-height:1.5}"
"</style>"
"</head><body>"
"<div class='card'>"
"<h1>TollGate</h1>"
"<p class='subtitle'>Pay for internet access with ecash or mining</p>"
"__MINING_TABS__"
"<div id='tab-cashu' class='tab-content __CASHU_ACTIVE__'>"
"<div class='price'>"
"<div class='price-amount'>__PRICE__</div>"
"<div class='price-unit'>sats per minute</div>"
"</div>"
"<textarea id='tokenInput' placeholder='Paste your Cashu token here (cashuA...)'></textarea>"
"<button class='btn' id='payBtn' onclick='payToken()'>Pay & Connect</button>"
"<div class='mints'>"
"<div class='mints-title'>SUPPORTED MINTS</div>"
"<div class='mint-url' id='mintUrl' onclick='copyMint()'>__MINT_URL__</div>"
"<div class='mint-hint'>Tap to copy &bull; Mint tokens at this URL before paying</div>"
"</div>"
"</div>"
"<div id='tab-mining' class='tab-content __MINING_ACTIVE__'>"
"<div class='mining-stats'>"
"<div class='mining-stat'><span class='label'>Hashrate</span><span class='value' id='hashrate'>0.00 GH/s</span></div>"
"<div class='mining-stat'><span class='label'>Shares</span><span class='value' id='shareCount'>0</span></div>"
"<div class='mining-stat'><span class='label'>Hashprice</span><span class='value' id='hashprice'>0.00 sat/GH/s/day</span></div>"
"<div class='mining-stat'><span class='label'>Time earned</span><span class='value' id='timeEarned'>0 min</span></div>"
"</div>"
"<button class='btn' id='mineBtn' onclick='toggleMining()'>Start Mining</button>"
"<div class='mining-info'>Mining earns internet time by contributing SHA256 hashpower. "
"Connect a Stratum miner to port __MINING_PORT__ or use the built-in web miner.</div>"
"</div>"
"<div id='status'></div>"
"</div>"
"<script>"
"const mintUrlEl=document.getElementById('mintUrl');"
"const mintUrl=mintUrlEl.textContent;"
"const statusEl=document.getElementById('status');"
"const payBtn=document.getElementById('payBtn');"
"const tokenInput=document.getElementById('tokenInput');"
"let miningActive=false;"
"let miningInterval=null;"
"function switchTab(tab){"
"document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));"
"document.querySelectorAll('.tab-content').forEach(t=>t.classList.remove('active'));"
"event.target.classList.add('active');"
"document.getElementById('tab-'+tab).classList.add('active');"
"}"
"function copyMint(){"
"if(navigator.clipboard){navigator.clipboard.writeText(mintUrl);"
"mintUrlEl.textContent='Copied!';setTimeout(()=>{mintUrlEl.textContent=mintUrl;},1000);}"
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
"function pollMiningStats(){"
"fetch('http://__AP_IP__:2121/mining/stats').then(r=>r.json()).then(d=>{"
"document.getElementById('hashrate').textContent=d.proxy.hashrate_ghs.toFixed(2)+' GH/s';"
"document.getElementById('shareCount').textContent=d.proxy.total_accepted;"
"document.getElementById('hashprice').textContent=d.proxy.hashprice.toFixed(2)+' sat/GH/s/day';"
"}).catch(()=>{});"
"fetch('http://__AP_IP__:2121/usage').then(r=>r.text()).then(t=>{"
"if(t&&t!=='-1/-1'){"
"const parts=t.split('/');const rem=Math.floor(parseInt(parts[0])/60000);"
"document.getElementById('timeEarned').textContent=rem+' min';"
"}}).catch(()=>{});"
"}"
"function toggleMining(){"
"if(miningActive){miningActive=false;clearInterval(miningInterval);"
"document.getElementById('mineBtn').textContent='Start Mining';return;}"
"miningActive=true;document.getElementById('mineBtn').textContent='Mining...';"
"miningInterval=setInterval(pollMiningStats,2000);pollMiningStats();"
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

    const tollgate_config_t *cfg = tollgate_config_get();
    char price_str[16];
    snprintf(price_str, sizeof(price_str), "%d", cfg->price_per_step);

    const char *tpl = PORTAL_HTML_TEMPLATE;
    size_t tpl_len = strlen(tpl);

    struct { const char *key; const char *val; } subs[] = {
        { "__AP_IP__", s_ap_ip_str },
        { "__PRICE__", price_str },
        { "__MINT_URL__", cfg->mint_url },
        { "__MINING_TABS__", cfg->mining_enabled ?
            "<div class='tabs'>"
            "<button class='tab active' onclick=\"switchTab('cashu')\">Cashu</button>"
            "<button class='tab' onclick=\"switchTab('mining')\">Mine</button>"
            "</div>" : "" },
        { "__MINING_PORT__", cfg->mining_enabled ?
            (char[]){ [0 ... 7] = 0 } : "3333" },
        { "__CASHU_ACTIVE__", "active" },
        { "__MINING_ACTIVE__", "" },
    };
    char mining_port_buf[8] = "3333";
    if (cfg->mining_enabled) {
        snprintf(mining_port_buf, sizeof(mining_port_buf), "%d", cfg->mining_port);
        subs[4].val = mining_port_buf;
    }
    int nsubs = sizeof(subs) / sizeof(subs[0]);

    size_t extra = 0;
    for (int i = 0; i < nsubs; i++) {
        const char *p = tpl;
        size_t klen = strlen(subs[i].key);
        while ((p = strstr(p, subs[i].key)) != NULL) {
            extra += strlen(subs[i].val) - klen;
            p += klen;
        }
    }

    size_t out_size = tpl_len + extra + 1;
    char *html = malloc(out_size);
    if (!html) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    char *out = html;
    const char *src = tpl;
    while (*src) {
        const char *earliest = NULL;
        int ei = -1;
        for (int i = 0; i < nsubs; i++) {
            const char *found = strstr(src, subs[i].key);
            if (found && (earliest == NULL || found < earliest)) {
                earliest = found;
                ei = i;
            }
        }
        if (earliest) {
            size_t vlen = strlen(subs[ei].val);
            memcpy(out, src, earliest - src);
            out += earliest - src;
            memcpy(out, subs[ei].val, vlen);
            out += vlen;
            src = earliest + strlen(subs[ei].key);
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
    if (get_client_ip(req, &client_ip) != ESP_OK) {
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "-1/-1", 5);
        return ESP_OK;
    }

    session_t *session = session_find_by_ip(client_ip);
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

static esp_err_t reset_auth_handler(httpd_req_t *req)
{
    session_revoke_all();
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
