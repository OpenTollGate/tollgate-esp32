#include "tollgate_esp_platform.h"
#include "tollgate_core.h"
#include "config.h"
#include "nucula_wallet.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/lwip_napt.h"
#include <string.h>
#include <stdarg.h>

static const char *TAG = "tg_esp";

static void esp_log_info(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_write(ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
}

static void esp_log_warn(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_write(ESP_LOG_WARN, tag, fmt, args);
    va_end(args);
}

static void esp_log_error(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_write(ESP_LOG_ERROR, tag, fmt, args);
    va_end(args);
}

static uint16_t esp_get_price_sats(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (uint16_t)cfg->price_per_step : 21;
}

static int32_t esp_get_step_ms(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int32_t)cfg->step_size_ms : 60000;
}

static int64_t esp_get_step_bytes(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int64_t)cfg->step_size_bytes : 22020096;
}

static const char *esp_get_mint_url(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mint_url : NULL;
}

static const char *esp_get_metric(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->metric : "milliseconds";
}

static int64_t esp_get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static bool esp_wallet_receive(const char *token)
{
    return nucula_wallet_receive(token) == ESP_OK;
}

static bool esp_wallet_send(uint64_t amount, char *buf, size_t buf_len)
{
    return nucula_wallet_send(amount, buf, buf_len) == ESP_OK;
}

static uint64_t esp_wallet_balance(void)
{
    return nucula_wallet_balance();
}

static int esp_http_post(const char *url, const char *headers,
                         const char *body, int body_len,
                         char *resp, int resp_len)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    if (headers) {
        const char *h = headers;
        while (*h) {
            const char *colon = strchr(h, ':');
            if (!colon) break;
            char key[64] = {0};
            size_t klen = colon - h;
            if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, h, klen);
            esp_http_client_set_header(client, key, colon + 1);
            const char *next = strchr(colon, '\n');
            if (!next) break;
            h = next + 1;
        }
    }
    if (headers && headers[0]) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    int written = esp_http_client_write(client, body, body_len);
    (void)written;

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int rd = esp_http_client_read(client, resp, resp_len);
    esp_http_client_cleanup(client);

    if (status != 200 || rd <= 0) return -1;
    return rd;
}

static bool esp_create_task(void (*fn)(void*), void *arg,
                            const char *name, int stack_bytes, int priority)
{
    return xTaskCreate(fn, name ? name : "tg_task",
                       stack_bytes / sizeof(StackType_t),
                       arg, priority, NULL) == pdPASS;
}

static bool esp_mac_for_ip(uint32_t ip, char *mac_out, int mac_out_size)
{
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        wifi_sta_mac_ip_list_t ip_mac_list;
        if (esp_wifi_ap_get_sta_list_with_ip(&sta_list, &ip_mac_list) == ESP_OK) {
            for (int i = 0; i < ip_mac_list.num; i++) {
                if (ip_mac_list.sta[i].ip.addr == ip) {
                    snprintf(mac_out, mac_out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
                             ip_mac_list.sta[i].mac[0], ip_mac_list.sta[i].mac[1],
                             ip_mac_list.sta[i].mac[2], ip_mac_list.sta[i].mac[3],
                             ip_mac_list.sta[i].mac[4], ip_mac_list.sta[i].mac[5]);
                    return true;
                }
            }
        }
    }

    ip4_addr_t *entry_ip = NULL;
    struct netif *entry_netif = NULL;
    struct eth_addr *entry_eth = NULL;
    ssize_t i = 0;
    while (etharp_get_entry(i, &entry_ip, &entry_netif, &entry_eth) == ERR_OK) {
        if (entry_ip && entry_ip->addr == ip && entry_eth) {
            snprintf(mac_out, mac_out_size, "%02x:%02x:%02x:%02x:%02x:%02x",
                     entry_eth->addr[0], entry_eth->addr[1], entry_eth->addr[2],
                     entry_eth->addr[3], entry_eth->addr[4], entry_eth->addr[5]);
            return true;
        }
        i++;
    }
    return false;
}

static void esp_napt_enable(uint32_t ip, bool enable)
{
    ip_napt_enable(ip, enable ? 1 : 0);
}

static bool esp_mining_enabled(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mining_enabled : false;
}

static const char *esp_get_stratum_host(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_host : NULL;
}

static uint16_t esp_get_stratum_port(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_port : 3333;
}

static const char *esp_get_stratum_user(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_user : NULL;
}

static const char *esp_get_stratum_pass(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_pass : NULL;
}

static uint16_t esp_get_mining_port(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mining_port : 3334;
}

static uint64_t esp_get_hashprice_override(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->hashprice_sats_per_ghs_day : 0;
}

static void tg_esp_fill_random(void *buf, int len)
{
    esp_fill_random(buf, (size_t)len);
}

static int esp_get_accepted_mint_count(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->accepted_mint_count : 0;
}

static const char *esp_get_accepted_mint(int index)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    if (!cfg || index < 0 || index >= cfg->accepted_mint_count) return NULL;
    return cfg->accepted_mints[index];
}

static bool esp_is_mint_reachable(const char *mint_url)
{
    (void)mint_url;
    return true;
}

static const tollgate_platform_t s_platform = {
    .get_price_sats       = esp_get_price_sats,
    .get_step_ms          = esp_get_step_ms,
    .get_step_bytes       = esp_get_step_bytes,
    .get_mint_url         = esp_get_mint_url,
    .get_metric           = esp_get_metric,
    .get_time_ms          = esp_get_time_ms,
    .log_info             = esp_log_info,
    .log_warn             = esp_log_warn,
    .log_error            = esp_log_error,
    .wallet_receive       = esp_wallet_receive,
    .wallet_send          = esp_wallet_send,
    .wallet_balance       = esp_wallet_balance,
    .http_post            = esp_http_post,
    .create_task          = esp_create_task,
    .mac_for_ip           = esp_mac_for_ip,
    .napt_enable          = esp_napt_enable,
    .mining_enabled       = esp_mining_enabled,
    .get_stratum_host     = esp_get_stratum_host,
    .get_stratum_port     = esp_get_stratum_port,
    .get_stratum_user     = esp_get_stratum_user,
    .get_stratum_pass     = esp_get_stratum_pass,
    .get_mining_port      = esp_get_mining_port,
    .get_hashprice_override = esp_get_hashprice_override,
    .fill_random          = tg_esp_fill_random,
    .get_accepted_mint_count = esp_get_accepted_mint_count,
    .get_accepted_mint    = esp_get_accepted_mint,
    .is_mint_reachable    = esp_is_mint_reachable,
};

const tollgate_platform_t *tollgate_esp_get_platform(void)
{
    return &s_platform;
}
