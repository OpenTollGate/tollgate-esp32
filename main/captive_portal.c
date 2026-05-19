#include "captive_portal.h"
#include "firewall.h"
#include "session.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/param.h>
#include <stdio.h>

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
"#status{margin-top:16px;padding:12px;border-radius:8px;display:none;font-size:14px}"
"#status.success{display:block;background:#1a472a;color:#4caf50}"
"#status.error{display:block;background:#471a1a;color:#f44336}"
"#status.processing{display:block;background:#1a3a47;color:#2196f3}"
"</style>"
"</head><body>"
"<div class='card'>"
"<h1>TollGate</h1>"
"<p class='subtitle'>Pay for internet access with ecash</p>"
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
"<div id='status'></div>"
"</div>"
"<script>"
"const mintUrlEl=document.getElementById('mintUrl');"
"const mintUrl=mintUrlEl.textContent;"
"const statusEl=document.getElementById('status');"
"const payBtn=document.getElementById('payBtn');"
"const tokenInput=document.getElementById('tokenInput');"
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
    };
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

static esp_err_t catchall_err_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (err == HTTPD_404_NOT_FOUND) {
        ESP_LOGI(TAG, "Catchall 404: GET %s → 302 → http://%s/", req->uri, s_ap_ip_str);
        httpd_resp_set_status(req, "302 Found");
        char location[64];
        snprintf(location, sizeof(location), "http://%s/", s_ap_ip_str);
        httpd_resp_set_hdr(req, "Location", location);
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    return ESP_FAIL;
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

static const char SETUP_HTML_TEMPLATE[] = \
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>TollGate Setup</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:#0a0a0a;color:#fff;display:flex;align-items:center;justify-content:center;"
"min-height:100vh;padding:20px}"
".card{background:#1a1a1a;border:1px solid #333;border-radius:16px;padding:32px;"
"max-width:400px;width:100%;text-align:center}"
"h1{font-size:24px;margin-bottom:8px;color:#f7931a}"
".subtitle{color:#888;margin-bottom:20px;font-size:13px}"
".networks{margin-top:16px;text-align:left}"
".net-item{background:#252525;border:1px solid #333;border-radius:8px;"
"padding:12px;margin-bottom:8px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}"
".net-item:hover{border-color:#f7931a}"
".net-item:active{background:#333}"
".net-ssid{font-size:14px}"
".net-rssi{font-size:11px;color:#888}"
".net-lock{color:#f7931a;margin-right:4px}"
".manual{margin-top:12px}"
"input{width:100%;background:#252525;border:1px solid #333;border-radius:8px;"
"color:#fff;padding:12px;font-size:14px;margin-bottom:8px;outline:none}"
"input:focus{border-color:#f7931a}"
".btn{background:#f7931a;color:#000;border:none;border-radius:8px;padding:14px 28px;"
"font-size:16px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px}"
".btn:hover{background:#e8850f}"
".btn:disabled{background:#333;color:#666;cursor:not-allowed}"
"#status{margin-top:12px;padding:10px;border-radius:8px;display:none;font-size:13px}"
"#status.success{display:block;background:#1a472a;color:#4caf50}"
"#status.error{display:block;background:#471a1a;color:#f44336}"
"#status.processing{display:block;background:#1a3a47;color:#2196f3}"
".refresh{background:none;border:1px solid #444;color:#aaa;border-radius:6px;"
"padding:6px 12px;font-size:12px;cursor:pointer;margin-top:4px}"
".refresh:hover{border-color:#f7931a;color:#f7931a}"
"#manualForm{display:none;margin-top:12px}"
"</style>"
"</head><body>"
"<div class='card'>"
"<h1>TollGate Setup</h1>"
"<p class='subtitle'>Configure upstream WiFi</p>"
"<div id='scanStatus'>Scanning...</div>"
"<div class='networks' id='networkList'></div>"
"<button class='refresh' onclick='scanWifi()'>Rescan</button>"
"<button class='refresh' onclick='showManual()'>Manual entry</button>"
"<div id='manualForm'>"
"<input id='manualSsid' placeholder='SSID'>"
"<input id='manualPass' type='password' placeholder='Password'>"
"<button class='btn' onclick='connectManual()'>Connect</button>"
"</div>"
"<div id='passwordForm' style='display:none'>"
"<p style='margin:12px 0 8px;text-align:left' id='selectedNetwork'></p>"
"<input id='wifiPass' type='password' placeholder='WiFi password'>"
"<button class='btn' onclick='connectSelected()'>Connect</button>"
"</div>"
"<div id='status'></div>"
"</div>"
"<script>"
"const apIp='__AP_IP__';"
"let selectedSsid='';"
"function showStatus(msg,type){const s=document.getElementById('status');"
"s.textContent=msg;s.className=type;}"
"function scanWifi(){"
"document.getElementById('scanStatus').textContent='Scanning...';"
"document.getElementById('networkList').innerHTML='';"
"fetch('/wifi/scan').then(r=>r.json()).then(aps=>{"
"document.getElementById('scanStatus').textContent=aps.length+' networks found';"
"const list=document.getElementById('networkList');"
"aps.forEach(ap=>{"
"const div=document.createElement('div');"
"div.className='net-item';"
"const lock=ap.secured?'<span class=net-lock>&#128274;</span>':'';"
"div.innerHTML='<span class=net-ssid>'+lock+ap.ssid+'</span>"
"<span class=net-rssi>'+ap.rssi+' dBm</span>';"
"div.onclick=()=>selectNetwork(ap.ssid,ap.secured);"
"list.appendChild(div);"
"});"
"}).catch(e=>{document.getElementById('scanStatus').textContent='Scan failed';});"
"}"
"function selectNetwork(ssid,secured){"
"selectedSsid=ssid;"
"document.getElementById('selectedNetwork').textContent='Connect to: '+ssid;"
"document.getElementById('passwordForm').style.display='block';"
"document.getElementById('scanStatus').style.display='none';"
"document.getElementById('networkList').style.display='none';"
"document.querySelector('.refresh').style.display='none';"
"if(!secured){connectSelected();}"
"}"
"function showManual(){"
"document.getElementById('manualForm').style.display='block';"
"}"
"function connectSelected(){"
"const pass=document.getElementById('wifiPass').value;"
"doConnect(selectedSsid,pass);"
"}"
"function connectManual(){"
"const ssid=document.getElementById('manualSsid').value.trim();"
"const pass=document.getElementById('manualPass').value;"
"if(!ssid){showStatus('Enter SSID','error');return;}"
"doConnect(ssid,pass);"
"}"
"function doConnect(ssid,pass){"
"showStatus('Connecting to '+ssid+'...','processing');"
"fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:pass})})"
".then(r=>r.json()).then(d=>{"
"if(d.ok){showStatus('Connected! Device is restarting...','success');}"
"else{showStatus('Failed: '+(d.error||'unknown'),'error');}"
"}).catch(e=>{showStatus('Connection error','error');});"
"}"
"scanWifi();"
"</script>"
"</body></html>";

static char *template_replace(const char *tpl, const char *key, const char *val) {
    const char *p;
    size_t klen = strlen(key);
    size_t vlen = strlen(val);
    size_t tlen = strlen(tpl);
    size_t extra = 0;
    p = tpl;
    while ((p = strstr(p, key)) != NULL) {
        extra += vlen - klen;
        p += klen;
    }
    size_t out_size = tlen + extra + 1;
    char *out = malloc(out_size);
    if (!out) return NULL;
    char *dst = out;
    p = tpl;
    while (*p) {
        const char *found = strstr(p, key);
        if (found) {
            memcpy(dst, p, found - p);
            dst += found - p;
            memcpy(dst, val, vlen);
            dst += vlen;
            p = found + klen;
        } else {
            strcpy(dst, p);
            dst += strlen(p);
            break;
        }
    }
    *dst = '\0';
    return out;
}

static bool is_setup_available(void) {
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg->network_count == 0;
}

static esp_err_t setup_page_handler(httpd_req_t *req) {
    if (!is_setup_available()) {
        httpd_resp_set_status(req, "302 Found");
        char location[64];
        snprintf(location, sizeof(location), "http://%s/", s_ap_ip_str);
        httpd_resp_set_hdr(req, "Location", location);
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    char *html = template_replace(SETUP_HTML_TEMPLATE, "__AP_IP__", s_ap_ip_str);
    if (!html) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    httpd_resp_send(req, html, strlen(html));
    free(html);
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *req) {
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));

    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;
    wifi_ap_record_t aps[20];
    esp_wifi_scan_get_ap_records(&ap_count, aps);

    for (int i = 0; i < (int)ap_count - 1; i++) {
        for (int j = i + 1; j < (int)ap_count; j++) {
            if (aps[j].rssi > aps[i].rssi) {
                wifi_ap_record_t tmp = aps[i];
                aps[i] = aps[j];
                aps[j] = tmp;
            }
        }
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < (int)ap_count; i++) {
        if (aps[i].ssid[0] == '\0') continue;
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (const char *)aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(ap, "secured", aps[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(root, ap);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);

    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg->network_count > 0) {
        wifi_config_t wifi_cfg;
        if (tollgate_config_get_wifi(&wifi_cfg) == ESP_OK) {
            esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
            esp_wifi_connect();
        }
    }

    return ESP_OK;
}

static esp_err_t wifi_connect_handler(httpd_req_t *req) {
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 1024) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"invalid request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char *body = malloc(content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    int total = 0;
    while (total < content_len) {
        int r = httpd_req_recv(req, body + total, content_len - total);
        if (r <= 0) { free(body); httpd_resp_send_500(req); return ESP_OK; }
        total += r;
    }
    body[total] = '\0';

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"invalid JSON\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(json, "password");
    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"missing ssid\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";

    esp_err_t err = tollgate_config_add_wifi(ssid, password);
    if (err != ESP_OK) {
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"save failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_connect();

    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t *req) {
    wifi_ap_record_t ap_info;
    bool connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", connected);

    if (connected) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                cJSON_AddStringToObject(root, "ip", ip_str);
            }
        }
        cJSON_AddStringToObject(root, "ssid", (const char *)ap_info.ssid);
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static const httpd_uri_t uri_setup = { .uri = "/setup", .method = HTTP_GET, .handler = setup_page_handler };
static const httpd_uri_t uri_wifi_scan = { .uri = "/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler };
static const httpd_uri_t uri_wifi_connect = { .uri = "/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler };
static const httpd_uri_t uri_wifi_status = { .uri = "/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler };

esp_err_t captive_portal_start(const char *ap_ip_str)
{
    if (s_server) return ESP_OK;
    strncpy(s_ap_ip_str, ap_ip_str, sizeof(s_ap_ip_str) - 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;

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
    httpd_register_uri_handler(s_server, &uri_setup);
    ret = httpd_register_uri_handler(s_server, &uri_wifi_scan);
    ESP_LOGI(TAG, "Registered /wifi/scan: %s", esp_err_to_name(ret));
    ret = httpd_register_uri_handler(s_server, &uri_wifi_connect);
    ESP_LOGI(TAG, "Registered /wifi/connect: %s", esp_err_to_name(ret));
    ret = httpd_register_uri_handler(s_server, &uri_wifi_status);
    ESP_LOGI(TAG, "Registered /wifi/status: %s", esp_err_to_name(ret));

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, catchall_err_handler);

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
