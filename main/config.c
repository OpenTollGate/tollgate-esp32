#include "config.h"
#include "identity.h"
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
    g_config.step_size_bytes = 22020096;
    strncpy(g_config.metric, "milliseconds", sizeof(g_config.metric) - 1);
    g_config.persist_threshold_sats = 1;
    g_config.nostr_publish_interval_s = 21600;
    g_config.client_enabled = false;
    g_config.client_steps_to_buy = 1;
    g_config.client_renewal_threshold_pct = 20;
    g_config.client_retry_interval_ms = 30000;
    g_config.payout.enabled = true;
    g_config.payout.fee_tolerance_pct = 10;
    g_config.payout.check_interval_s = 60;
    g_config.payout.recipient_count = 0;
    g_config.payout.mint_count = 0;
    g_config.cvm_enabled = true;
    strncpy(g_config.cvm_relays, "wss://relay.primal.net", sizeof(g_config.cvm_relays) - 1);

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
            "\"nsec\":\"a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2\","
            "\"wifi_networks\":["
              "{\"ssid\":\"c03rad0r\",\"password\":\"c03rad0r123\"}"
            "],"
            "\"ap_password\":\"\","
            "\"mint_url\":\"https://testnut.cashu.space\","
            "\"price_per_step\":21,"
            "\"step_size_ms\":60000,"
            "\"nostr_geohash\":\"u281w0dfz\","
            "\"nostr_relays\":[\"wss://relay.damus.io\",\"wss://nos.lol\"],"
            "\"nostr_publish_interval_s\":21600,"
            "\"client_enabled\":false,"
            "\"client_steps_to_buy\":1,"
            "\"client_renewal_threshold_pct\":20,"
            "\"client_retry_interval_ms\":30000"
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

    cJSON *nsec = cJSON_GetObjectItem(root, "nsec");
    if (nsec && cJSON_IsString(nsec)) {
        strncpy(g_config.nsec, nsec->valuestring, sizeof(g_config.nsec) - 1);
    } else {
        ESP_LOGE(TAG, "Missing 'nsec' in config.json");
        cJSON_Delete(root);
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

    cJSON *ap_pass = cJSON_GetObjectItem(root, "ap_password");
    if (ap_pass) strncpy(g_config.ap_password, ap_pass->valuestring, sizeof(g_config.ap_password) - 1);

    cJSON *mint = cJSON_GetObjectItem(root, "mint_url");
    if (mint) strncpy(g_config.mint_url, mint->valuestring, sizeof(g_config.mint_url) - 1);

    cJSON *lnurl = cJSON_GetObjectItem(root, "lnurl_url");
    if (lnurl) strncpy(g_config.lnurl_url, lnurl->valuestring, sizeof(g_config.lnurl_url) - 1);

    cJSON *price = cJSON_GetObjectItem(root, "price_per_step");
    if (price) g_config.price_per_step = price->valueint;

    cJSON *step = cJSON_GetObjectItem(root, "step_size_ms");
    if (step) g_config.step_size_ms = step->valueint;

    cJSON *step_bytes = cJSON_GetObjectItem(root, "step_size_bytes");
    if (step_bytes) g_config.step_size_bytes = step_bytes->valueint;

    cJSON *metric = cJSON_GetObjectItem(root, "metric");
    if (metric && cJSON_IsString(metric)) {
        strncpy(g_config.metric, metric->valuestring, sizeof(g_config.metric) - 1);
    }

    cJSON *persist = cJSON_GetObjectItem(root, "persist_threshold_sats");
    if (persist) g_config.persist_threshold_sats = (uint64_t)persist->valuedouble;

    cJSON *geohash = cJSON_GetObjectItem(root, "nostr_geohash");
    if (geohash) strncpy(g_config.nostr_geohash, geohash->valuestring, sizeof(g_config.nostr_geohash) - 1);
    else strncpy(g_config.nostr_geohash, "u281w0dfz", sizeof(g_config.nostr_geohash) - 1);

    cJSON *relays = cJSON_GetObjectItem(root, "nostr_relays");
    if (relays && cJSON_IsArray(relays)) {
        int rcount = cJSON_GetArraySize(relays);
        if (rcount > TOLLGATE_MAX_RELAYS) rcount = TOLLGATE_MAX_RELAYS;
        for (int i = 0; i < rcount; i++) {
            cJSON *r = cJSON_GetArrayItem(relays, i);
            if (r && cJSON_IsString(r)) {
                strncpy(g_config.nostr_relays[i], r->valuestring, sizeof(g_config.nostr_relays[i]) - 1);
                g_config.nostr_relay_count++;
            }
        }
    }

    cJSON *pub_interval = cJSON_GetObjectItem(root, "nostr_publish_interval_s");
    if (pub_interval) g_config.nostr_publish_interval_s = pub_interval->valueint;

    cJSON *client_enabled = cJSON_GetObjectItem(root, "client_enabled");
    if (client_enabled && cJSON_IsBool(client_enabled)) g_config.client_enabled = cJSON_IsTrue(client_enabled);

    cJSON *client_steps = cJSON_GetObjectItem(root, "client_steps_to_buy");
    if (client_steps) g_config.client_steps_to_buy = client_steps->valueint;

    cJSON *client_renewal = cJSON_GetObjectItem(root, "client_renewal_threshold_pct");
    if (client_renewal) g_config.client_renewal_threshold_pct = client_renewal->valueint;

    cJSON *client_retry = cJSON_GetObjectItem(root, "client_retry_interval_ms");
    if (client_retry) g_config.client_retry_interval_ms = client_retry->valueint;

    cJSON *payout = cJSON_GetObjectItem(root, "payout");
    if (payout && cJSON_IsObject(payout)) {
        cJSON *p_en = cJSON_GetObjectItem(payout, "enabled");
        if (p_en && cJSON_IsBool(p_en)) g_config.payout.enabled = cJSON_IsTrue(p_en);

        cJSON *p_fee = cJSON_GetObjectItem(payout, "fee_tolerance_pct");
        if (p_fee) g_config.payout.fee_tolerance_pct = (uint64_t)p_fee->valuedouble;

        cJSON *p_interval = cJSON_GetObjectItem(payout, "check_interval_s");
        if (p_interval) g_config.payout.check_interval_s = p_interval->valueint;

        cJSON *recipients = cJSON_GetObjectItem(payout, "recipients");
        if (recipients && cJSON_IsArray(recipients)) {
            int rcount = cJSON_GetArraySize(recipients);
            if (rcount > PAYOUT_MAX_RECIPIENTS) rcount = PAYOUT_MAX_RECIPIENTS;
            for (int i = 0; i < rcount; i++) {
                cJSON *r = cJSON_GetArrayItem(recipients, i);
                cJSON *addr = cJSON_GetObjectItem(r, "lightning_address");
                cJSON *factor = cJSON_GetObjectItem(r, "factor");
                if (addr && cJSON_IsString(addr)) {
                    strncpy(g_config.payout.recipients[i].lightning_address, addr->valuestring,
                            sizeof(g_config.payout.recipients[i].lightning_address) - 1);
                }
                if (factor && cJSON_IsNumber(factor)) {
                    g_config.payout.recipients[i].factor = factor->valuedouble;
                }
            }
            g_config.payout.recipient_count = rcount;
        }

        cJSON *mints = cJSON_GetObjectItem(payout, "mints");
        if (mints && cJSON_IsArray(mints)) {
            int mcount = cJSON_GetArraySize(mints);
            if (mcount > PAYOUT_MAX_MINTS) mcount = PAYOUT_MAX_MINTS;
            for (int i = 0; i < mcount; i++) {
                cJSON *m = cJSON_GetArrayItem(mints, i);
                cJSON *murl = cJSON_GetObjectItem(m, "url");
                cJSON *mbal = cJSON_GetObjectItem(m, "min_balance");
                cJSON *mpay = cJSON_GetObjectItem(m, "min_payout_amount");
                if (murl && cJSON_IsString(murl)) {
                    strncpy(g_config.payout.mints[i].url, murl->valuestring,
                            sizeof(g_config.payout.mints[i].url) - 1);
                }
                if (mbal && cJSON_IsNumber(mbal)) {
                    g_config.payout.mints[i].min_balance = (uint64_t)mbal->valuedouble;
                }
                if (mpay && cJSON_IsNumber(mpay)) {
                    g_config.payout.mints[i].min_payout_amount = (uint64_t)mpay->valuedouble;
                }
            }
            g_config.payout.mint_count = mcount;
        }
    }

    cJSON *cvm = cJSON_GetObjectItem(root, "cvm");
    if (cvm && cJSON_IsObject(cvm)) {
        cJSON *cvm_en = cJSON_GetObjectItem(cvm, "enabled");
        if (cvm_en && cJSON_IsBool(cvm_en)) g_config.cvm_enabled = cJSON_IsTrue(cvm_en);
        cJSON *cvm_relays = cJSON_GetObjectItem(cvm, "relays");
        if (cvm_relays && cJSON_IsString(cvm_relays)) {
            strncpy(g_config.cvm_relays, cvm_relays->valuestring, sizeof(g_config.cvm_relays) - 1);
        }
    }

    if (g_config.payout.mint_count == 0 && g_config.mint_url[0] != '\0') {
        strncpy(g_config.payout.mints[0].url, g_config.mint_url,
                sizeof(g_config.payout.mints[0].url) - 1);
        g_config.payout.mints[0].min_balance = 64;
        g_config.payout.mints[0].min_payout_amount = 128;
        g_config.payout.mint_count = 1;
    }

    if (g_config.payout.recipient_count == 0) {
        strncpy(g_config.payout.recipients[0].lightning_address, "TollGate@coinos.io",
                sizeof(g_config.payout.recipients[0].lightning_address) - 1);
        g_config.payout.recipients[0].factor = 1.0;
        g_config.payout.recipient_count = 1;
    }

    cJSON_Delete(root);

    if (g_config.nostr_relay_count == 0) {
        strncpy(g_config.nostr_relays[0], "wss://relay.damus.io", sizeof(g_config.nostr_relays[0]) - 1);
        strncpy(g_config.nostr_relays[1], "wss://nos.lol", sizeof(g_config.nostr_relays[1]) - 1);
        g_config.nostr_relay_count = 2;
    }

    ESP_LOGI(TAG, "Config loaded: nsec=%s...%s, %d WiFi networks, price=%d sats/%dms",
             g_config.nsec, g_config.nsec + 60, g_config.network_count,
             g_config.price_per_step, g_config.step_size_ms);
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
    wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA3_PSK;
    return ESP_OK;
}

esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config)
{
    if (g_config.network_count == 0) return ESP_ERR_NOT_FOUND;
    g_config.current_network = (g_config.current_network + 1) % g_config.network_count;
    return tollgate_config_get_wifi(wifi_config);
}

void tollgate_config_derive_unique(tollgate_config_t *cfg)
{
    if (cfg->identity_initialized) return;

    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Cannot derive unique config: identity not initialized");
        return;
    }

    strncpy(cfg->ap_ssid, id->ap_ssid, sizeof(cfg->ap_ssid) - 1);
    memcpy(cfg->sta_mac, id->sta_mac, 6);
    memcpy(cfg->ap_mac, id->ap_mac, 6);
    cfg->ap_ip = id->ap_ip;
    strncpy(cfg->ap_ip_str, id->ap_ip_str, sizeof(cfg->ap_ip_str) - 1);
    strncpy(cfg->npub, id->npub_hex, sizeof(cfg->npub) - 1);

    cfg->identity_initialized = true;

    ESP_LOGI(TAG, "Unique config derived from nsec: SSID='%s', AP_IP=%s",
             cfg->ap_ssid, cfg->ap_ip_str);
}

esp_err_t tollgate_config_add_wifi(const char *ssid, const char *password) {
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    if (g_config.network_count >= TOLLGATE_MAX_WIFI_NETWORKS) return ESP_ERR_NO_MEM;

    strncpy(g_config.networks[g_config.network_count].ssid, ssid,
            sizeof(g_config.networks[g_config.network_count].ssid) - 1);
    strncpy(g_config.networks[g_config.network_count].password, password,
            sizeof(g_config.networks[g_config.network_count].password) - 1);
    g_config.network_count++;

    cJSON *root = cJSON_CreateObject();
    cJSON *nsec = cJSON_CreateString(g_config.nsec);
    cJSON_AddItemToObject(root, "nsec", nsec);

    cJSON *networks = cJSON_CreateArray();
    for (int i = 0; i < g_config.network_count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", g_config.networks[i].ssid);
        cJSON_AddStringToObject(net, "password", g_config.networks[i].password);
        cJSON_AddItemToArray(networks, net);
    }
    cJSON_AddItemToObject(root, "wifi_networks", networks);

    if (g_config.ap_password[0])
        cJSON_AddStringToObject(root, "ap_password", g_config.ap_password);
    cJSON_AddStringToObject(root, "mint_url", g_config.mint_url);
    cJSON_AddNumberToObject(root, "price_per_step", g_config.price_per_step);
    cJSON_AddNumberToObject(root, "step_size_ms", g_config.step_size_ms);

    if (g_config.metric[0])
        cJSON_AddStringToObject(root, "metric", g_config.metric);

    cJSON_AddStringToObject(root, "nostr_geohash", g_config.nostr_geohash);

    cJSON *relays = cJSON_CreateArray();
    for (int i = 0; i < g_config.nostr_relay_count; i++) {
        cJSON_AddItemToArray(relays, cJSON_CreateString(g_config.nostr_relays[i]));
    }
    cJSON_AddItemToObject(root, "nostr_relays", relays);
    cJSON_AddNumberToObject(root, "nostr_publish_interval_s", g_config.nostr_publish_interval_s);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    FILE *f = fopen("/spiffs/config.json", "w");
    if (!f) {
        free(json_str);
        return ESP_FAIL;
    }
    fputs(json_str, f);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "Added WiFi network '%s' to config (total: %d)", ssid, g_config.network_count);
    return ESP_OK;
}
