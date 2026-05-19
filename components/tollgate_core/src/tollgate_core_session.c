#include "tollgate_core_session.h"
#include "tollgate_core_firewall.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tg_core_session";
static tg_session_t s_sessions[TG_SESSION_MAX_CLIENTS];
static int s_session_count = 0;

static const tollgate_platform_t *s_platform;

void tollgate_core_session_set_platform(const tollgate_platform_t *platform)
{
    s_platform = platform;
}

static int64_t get_time_ms(void)
{
    if (s_platform && s_platform->get_time_ms) {
        return s_platform->get_time_ms();
    }
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

esp_err_t tollgate_core_session_init(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    s_session_count = 0;
    ESP_LOGI(TAG, "Session manager initialized");
    return ESP_OK;
}

static void populate_mac(tg_session_t *session, uint32_t client_ip)
{
    if (tollgate_core_fw_get_mac_for_ip(client_ip, session->mac, sizeof(session->mac)) != ESP_OK) {
        session->mac[0] = '\0';
    }
}

tg_session_t *tollgate_core_session_create(uint32_t client_ip, uint64_t allotment_ms)
{
    tg_session_t *existing = tollgate_core_session_find_by_ip(client_ip);
    if (existing) {
        tollgate_core_session_extend(existing, allotment_ms);
        return existing;
    }

    if (s_session_count >= TG_SESSION_MAX_CLIENTS) {
        for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
            if (!s_sessions[i].active || tollgate_core_session_is_expired(&s_sessions[i])) {
                tollgate_core_session_revoke(&s_sessions[i]);
                break;
            }
        }
    }

    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (!s_sessions[i].active) {
            s_sessions[i].client_ip = client_ip;
            s_sessions[i].allotment_ms = allotment_ms;
            s_sessions[i].start_time_ms = get_time_ms();
            s_sessions[i].active = true;
            populate_mac(&s_sessions[i], client_ip);

            s_session_count++;
            tollgate_core_fw_grant(client_ip);

            esp_ip4_addr_t ip = { .addr = client_ip };
            ESP_LOGI(TAG, "Session created: " IPSTR " mac=%s allotment=%llums", IP2STR(&ip),
                     s_sessions[i].mac[0] ? s_sessions[i].mac : "unknown",
                     (unsigned long long)allotment_ms);
            return &s_sessions[i];
        }
    }

    ESP_LOGW(TAG, "No free session slots");
    return NULL;
}

tg_session_t *tollgate_core_session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes)
{
    tg_session_t *s = tollgate_core_session_create(client_ip, 0);
    if (s) {
        s->allotment_bytes = allotment_bytes;
        s->bytes_consumed = 0;
        s->allotment_ms = INT64_MAX;
        esp_ip4_addr_t ip = { .addr = client_ip };
        ESP_LOGI(TAG, "Bytes session created: " IPSTR " allotment=%llu bytes", IP2STR(&ip),
                 (unsigned long long)allotment_bytes);
    }
    return s;
}

void tollgate_core_session_add_bytes(uint32_t client_ip, uint64_t bytes)
{
    tg_session_t *s = tollgate_core_session_find_by_ip(client_ip);
    if (s && s->active) {
        s->bytes_consumed += bytes;
    }
}

tg_session_t *tollgate_core_session_find_by_ip(uint32_t client_ip)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].client_ip == client_ip) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

tg_session_t *tollgate_core_session_find_by_mac(const char *mac)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].mac[0] != '\0' &&
            strcmp(s_sessions[i].mac, mac) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

void tollgate_core_session_extend(tg_session_t *session, uint64_t additional_ms)
{
    if (!session || !session->active) return;
    session->allotment_ms += additional_ms;
    esp_ip4_addr_t ip = { .addr = session->client_ip };
    ESP_LOGI(TAG, "Session extended: " IPSTR " +%llums (total=%llu)", IP2STR(&ip),
             (unsigned long long)additional_ms, (unsigned long long)session->allotment_ms);
}

bool tollgate_core_session_is_expired(const tg_session_t *session)
{
    if (!session || !session->active) return true;

    if (s_platform && s_platform->get_metric) {
        const char *metric = s_platform->get_metric();
        if (metric && strcmp(metric, "bytes") == 0) {
            return session->bytes_consumed >= session->allotment_bytes;
        }
    }

    int64_t elapsed = get_time_ms() - session->start_time_ms;
    return elapsed >= (int64_t)session->allotment_ms;
}

static void check_expiry(void)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && tollgate_core_session_is_expired(&s_sessions[i])) {
            esp_ip4_addr_t ip = { .addr = s_sessions[i].client_ip };
            ESP_LOGI(TAG, "Session expired: " IPSTR " mac=%s", IP2STR(&ip),
                     s_sessions[i].mac[0] ? s_sessions[i].mac : "unknown");
            tollgate_core_session_revoke(&s_sessions[i]);
        }
    }
}

void tollgate_core_session_revoke(tg_session_t *session)
{
    if (!session || !session->active) return;
    tollgate_core_fw_revoke(session->client_ip);
    session->active = false;
    s_session_count--;
}

void tollgate_core_session_revoke_all(void)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) {
            tollgate_core_session_revoke(&s_sessions[i]);
        }
    }
}

int tollgate_core_session_active_count(void)
{
    int count = 0;
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) count++;
    }
    return count;
}

void tollgate_core_session_tick(void)
{
    check_expiry();
}

tg_session_t *tollgate_core_session_get_array(void)
{
    return s_sessions;
}

int tollgate_core_session_get_array_size(void)
{
    return TG_SESSION_MAX_CLIENTS;
}
