#include "config.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tollgate_config";
static tollgate_config_t g_config;

esp_err_t tollgate_config_init(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config.max_retry = 5;
    g_config.ap_channel = 1;
    g_config.ap_max_conn = 4;
    g_config.price_per_step = 21;
    g_config.step_size_ms = 60000;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }

    FILE *f = fopen("/spiffs/config.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "No config.json found, generating default");
        const char *default_json = "{"
            "\"wifi_networks\":["
              "{\"ssid\":\"EnterSSID-2.4GHz\",\"password\":\"c03rad0r123!\"}"
            "],"
            "\"ap_ssid\":\"TollGate\","
            "\"ap_password\":\"\","
            "\"ap_channel\":1,"
            "\"mint_url\":\"https://testnut.cashu.space\","
            "\"lnurl_url\":\"https://redeem.cashu.me/.well-known/lnurlp/tollgate\","
            "\"price_per_step\":21,"
            "\"step_size_ms\":60000"
          "}";
        f = fopen("/spiffs/config.json", "w");
        if (f) {
            fputs(default_json, f);
            fclose(f);
        }
        f = fopen("/spiffs/config.json", "r");
    }

    if (!f) {
        ESP_LOGE(TAG, "Failed to open config.json");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config.json");
        return ESP_FAIL;
    }

    cJSON *networks = cJSON_GetObjectItem(root, "wifi_networks");
    if (networks && cJSON_IsArray(networks)) {
        int count = cJSON_GetArraySize(networks);
        if (count > TOLLGATE_MAX_WIFI_NETWORKS) count = TOLLGATE_MAX_WIFI_NETWORKS;
        for (int i = 0; i < count; i++) {
            cJSON *net = cJSON_GetArrayItem(networks, i);
            cJSON *ssid = cJSON_GetObjectItem(net, "ssid");
            cJSON *pass = cJSON_GetObjectItem(net, "password");
            if (ssid && pass) {
                strncpy(g_config.networks[i].ssid, ssid->valuestring, sizeof(g_config.networks[i].ssid) - 1);
                strncpy(g_config.networks[i].password, pass->valuestring, sizeof(g_config.networks[i].password) - 1);
                g_config.network_count++;
            }
        }
    }

    cJSON *ap_ssid = cJSON_GetObjectItem(root, "ap_ssid");
    if (ap_ssid) strncpy(g_config.ap_ssid, ap_ssid->valuestring, sizeof(g_config.ap_ssid) - 1);
    else strncpy(g_config.ap_ssid, "TollGate", sizeof(g_config.ap_ssid) - 1);

    cJSON *ap_pass = cJSON_GetObjectItem(root, "ap_password");
    if (ap_pass) strncpy(g_config.ap_password, ap_pass->valuestring, sizeof(g_config.ap_password) - 1);

    cJSON *ap_ch = cJSON_GetObjectItem(root, "ap_channel");
    if (ap_ch) g_config.ap_channel = ap_ch->valueint;

    cJSON *mint = cJSON_GetObjectItem(root, "mint_url");
    if (mint) strncpy(g_config.mint_url, mint->valuestring, sizeof(g_config.mint_url) - 1);

    cJSON *lnurl = cJSON_GetObjectItem(root, "lnurl_url");
    if (lnurl) strncpy(g_config.lnurl_url, lnurl->valuestring, sizeof(g_config.lnurl_url) - 1);

    cJSON *price = cJSON_GetObjectItem(root, "price_per_step");
    if (price) g_config.price_per_step = price->valueint;

    cJSON *step = cJSON_GetObjectItem(root, "step_size_ms");
    if (step) g_config.step_size_ms = step->valueint;

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Config loaded: AP='%s', %d WiFi networks, price=%d sats/%dms",
             g_config.ap_ssid, g_config.network_count, g_config.price_per_step, g_config.step_size_ms);
    return ESP_OK;
}

const tollgate_config_t *tollgate_config_get(void)
{
    return &g_config;
}

esp_err_t tollgate_config_get_wifi(wifi_config_t *wifi_config)
{
    if (g_config.network_count == 0) return ESP_ERR_NOT_FOUND;
    int idx = g_config.current_network % g_config.network_count;
    memset(wifi_config, 0, sizeof(wifi_config_t));
    strncpy((char *)wifi_config->sta.ssid, g_config.networks[idx].ssid, sizeof(wifi_config->sta.ssid) - 1);
    strncpy((char *)wifi_config->sta.password, g_config.networks[idx].password, sizeof(wifi_config->sta.password) - 1);
    wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    return ESP_OK;
}

esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config)
{
    g_config.current_network = (g_config.current_network + 1) % g_config.network_count;
    return tollgate_config_get_wifi(wifi_config);
}

void tollgate_config_derive_unique(tollgate_config_t *cfg)
{
    if (cfg->unique_derived) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(cfg->ap_ssid + strlen(cfg->ap_ssid),
             TOLLGATE_MAX_AP_SSID_LEN - strlen(cfg->ap_ssid),
             "-%02X%02X", mac[4], mac[5]);

    uint8_t b5 = mac[4];
    uint8_t b6 = mac[5];
    uint8_t subnet = (b5 ^ b6) % 200 + 10;
    IP4_ADDR(&cfg->ap_ip, 10, b5, subnet, 1);
    snprintf(cfg->ap_ip_str, sizeof(cfg->ap_ip_str), IPSTR, IP2STR(&cfg->ap_ip));

    cfg->unique_derived = true;

    ESP_LOGI(TAG, "Unique config: SSID='%s', AP_IP=%s", cfg->ap_ssid, cfg->ap_ip_str);
}
