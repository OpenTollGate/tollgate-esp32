#include "tollgate_core.h"
#include "tollgate_core_cashu.h"
#include "tollgate_core_dns.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core_session.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "tg_core";

static const tollgate_platform_t *s_platform;
static esp_ip4_addr_t s_ap_ip;

static uint32_t s_owner_ip;
static uint8_t s_owner_mac[6];
static bool s_owner_connected;

esp_err_t tollgate_core_init(const tollgate_platform_t *platform, esp_ip4_addr_t ap_ip)
{
    if (!platform) {
        ESP_LOGE(TAG, "Platform callbacks required");
        return ESP_FAIL;
    }

    s_platform = platform;
    s_ap_ip = ap_ip;
    s_owner_connected = false;
    memset(s_owner_mac, 0, sizeof(s_owner_mac));

    if (!platform->spend_proofs) {
        ESP_LOGW(TAG, "spend_proofs is NULL — double-spend window open until wallet integrated");
    }

    tollgate_core_session_set_platform(platform);
    tollgate_core_session_init();
    tollgate_core_fw_init(ap_ip);

    ESP_LOGI(TAG, "TollGate core initialized, AP IP=" IPSTR, IP2STR(&ap_ip));
    return ESP_OK;
}

esp_err_t tollgate_core_dns_start(esp_ip4_addr_t upstream_dns)
{
    return tollgate_core_dns_start_internal(s_ap_ip, upstream_dns);
}

esp_err_t tollgate_core_process_payment(uint32_t client_ip, const char *token_str)
{
    if (!s_platform || !token_str) return ESP_FAIL;

    const char *accepted_mint = s_platform->get_mint_url ? s_platform->get_mint_url() : NULL;
    if (!accepted_mint || accepted_mint[0] == '\0') {
        ESP_LOGE(TAG, "No mint URL configured");
        return ESP_FAIL;
    }

    tg_cashu_token_t token;
    esp_err_t ret = tollgate_core_cashu_decode_token(token_str, &token);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Token decode failed");
        return ESP_FAIL;
    }

    if (!tollgate_core_cashu_is_mint_accepted(token.mint_url, accepted_mint)) {
        ESP_LOGE(TAG, "Token mint '%s' not accepted (expected '%s')", token.mint_url, accepted_mint);
        return ESP_FAIL;
    }

    tg_cashu_proof_state_t states[TG_CASHU_MAX_PROOFS];
    int state_count = 0;
    ret = tollgate_core_cashu_check_proof_states(token.mint_url, &token, states, &state_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Proof state check failed");
        return ESP_FAIL;
    }

    for (int i = 0; i < state_count; i++) {
        if (states[i].spent) {
            ESP_LOGE(TAG, "Proof %d is SPENT — rejecting", i);
            return ESP_FAIL;
        }
    }

    if (s_platform->spend_proofs) {
        if (!s_platform->spend_proofs(token_str)) {
            ESP_LOGE(TAG, "spend_proofs rejected the token");
            return ESP_FAIL;
        }
    }

    const char *metric = s_platform->get_metric ? s_platform->get_metric() : "milliseconds";
    uint64_t price = s_platform->get_price_sats ? s_platform->get_price_sats() : 21;
    uint64_t step_size;

    if (strcmp(metric, "bytes") == 0) {
        step_size = s_platform->get_step_bytes ? (uint64_t)s_platform->get_step_bytes() : 22020096;
    } else {
        step_size = s_platform->get_step_ms ? (uint64_t)s_platform->get_step_ms() : 60000;
    }

    uint64_t allotment = tollgate_core_cashu_calculate_allotment(token.total_amount, price, step_size);
    if (allotment == 0) {
        ESP_LOGE(TAG, "Token amount %llu too small for price %llu",
                 (unsigned long long)token.total_amount, (unsigned long long)price);
        return ESP_FAIL;
    }

    if (strcmp(metric, "bytes") == 0) {
        if (!tollgate_core_session_create_bytes(client_ip, allotment)) {
            return ESP_FAIL;
        }
    } else {
        if (!tollgate_core_session_create(client_ip, allotment)) {
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Payment processed: %llu sats → %llu %s allotment for client",
             (unsigned long long)token.total_amount, (unsigned long long)allotment, metric);
    return ESP_OK;
}

void tollgate_core_client_connected(const uint8_t *mac, uint32_t client_ip)
{
    if (!s_owner_connected) {
        s_owner_connected = true;
        s_owner_ip = client_ip;
        if (mac) memcpy(s_owner_mac, mac, 6);

        esp_ip4_addr_t ip = { .addr = client_ip };
        ESP_LOGI(TAG, "First client = owner: " IPSTR, IP2STR(&ip));
        return;
    }

    ESP_LOGI(TAG, "Client connected (non-owner): " IPSTR, IP2STR(&(esp_ip4_addr_t){.addr=client_ip}));
}

void tollgate_core_client_disconnected(const uint8_t *mac)
{
    if (!s_owner_connected) return;

    if (mac && memcmp(s_owner_mac, mac, 6) == 0) {
        ESP_LOGI(TAG, "Owner disconnected — reassigning");
        s_owner_connected = false;
        memset(s_owner_mac, 0, sizeof(s_owner_mac));

        int fw_count = tollgate_core_fw_client_count();
        if (fw_count > 0) {
            tg_session_t *sessions = tollgate_core_session_get_array();
            for (int i = 0; i < tollgate_core_session_get_array_size(); i++) {
                if (sessions[i].active && sessions[i].mac[0] != '\0') {
                    if (memcmp(sessions[i].mac, mac, 6) != 0) {
                        s_owner_connected = true;
                        s_owner_ip = sessions[i].client_ip;
                        ESP_LOGI(TAG, "New owner: " IPSTR, IP2STR(&(esp_ip4_addr_t){.addr=s_owner_ip}));
                        break;
                    }
                }
            }
        }
        return;
    }

    ESP_LOGI(TAG, "Client disconnected");
}

void tollgate_core_tick(void)
{
    tollgate_core_session_tick();
}

bool tollgate_core_is_client_allowed(uint32_t client_ip)
{
    return tollgate_core_fw_is_allowed(client_ip);
}

bool tollgate_core_is_dns_running(void)
{
    return tollgate_core_dns_is_running();
}

char *tollgate_core_get_status_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ownerConnected", s_owner_connected);
    cJSON_AddNumberToObject(root, "activeSessions", tollgate_core_session_active_count());
    cJSON_AddNumberToObject(root, "allowedClients", tollgate_core_fw_client_count());
    cJSON_AddBoolToObject(root, "dnsRunning", tollgate_core_dns_is_running());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *tollgate_core_get_config_json(void)
{
    cJSON *root = cJSON_CreateObject();

    if (s_platform) {
        if (s_platform->get_price_sats)
            cJSON_AddNumberToObject(root, "priceSats", s_platform->get_price_sats());
        if (s_platform->get_step_ms)
            cJSON_AddNumberToObject(root, "stepMs", s_platform->get_step_ms());
        if (s_platform->get_mint_url)
            cJSON_AddStringToObject(root, "mintUrl", s_platform->get_mint_url());
        if (s_platform->get_metric)
            cJSON_AddStringToObject(root, "metric", s_platform->get_metric());
        if (s_platform->get_step_bytes)
            cJSON_AddNumberToObject(root, "stepBytes", s_platform->get_step_bytes());
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

int tollgate_core_active_session_count(void)
{
    return tollgate_core_session_active_count();
}

int tollgate_core_allowed_client_count(void)
{
    return tollgate_core_fw_client_count();
}

bool tollgate_core_is_owner(uint32_t client_ip)
{
    return s_owner_connected && s_owner_ip == client_ip;
}

bool tollgate_core_is_owner_connected(void)
{
    return s_owner_connected;
}

const tollgate_platform_t *tollgate_core_get_platform(void)
{
    return s_platform;
}
