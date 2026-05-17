#include "session.h"
#include "firewall.h"
#include "dns_server.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "session";
static session_t s_sessions[SESSION_MAX_CLIENTS];
static int s_session_count = 0;

static int64_t get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

esp_err_t session_manager_init(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    s_session_count = 0;
    ESP_LOGI(TAG, "Session manager initialized");
    return ESP_OK;
}

static void populate_mac(session_t *session, uint32_t client_ip)
{
    if (firewall_get_mac_for_ip(client_ip, session->mac, sizeof(session->mac)) != ESP_OK) {
        session->mac[0] = '\0';
    }
}

session_t *session_create(uint32_t client_ip, uint64_t allotment_ms)
{
    session_t *existing = session_find_by_ip(client_ip);
    if (existing) {
        session_extend(existing, allotment_ms);
        return existing;
    }

    if (s_session_count >= SESSION_MAX_CLIENTS) {
        for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
            if (!s_sessions[i].active || session_is_expired(&s_sessions[i])) {
                session_revoke(&s_sessions[i]);
                break;
            }
        }
    }

    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (!s_sessions[i].active) {
            s_sessions[i].client_ip = client_ip;
            s_sessions[i].allotment_ms = allotment_ms;
            s_sessions[i].start_time_ms = get_time_ms();
            s_sessions[i].active = true;
            populate_mac(&s_sessions[i], client_ip);

            s_session_count++;
            firewall_grant_access(client_ip);

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

session_t *session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes)
{
    session_t *s = session_create(client_ip, 0);
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

void session_add_bytes(uint32_t client_ip, uint64_t bytes)
{
    session_t *s = session_find_by_ip(client_ip);
    if (s && s->active) {
        s->bytes_consumed += bytes;
    }
}

session_t *session_find_by_ip(uint32_t client_ip)
{
    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].client_ip == client_ip) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

session_t *session_find_by_mac(const char *mac)
{
    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].mac[0] != '\0' &&
            strcmp(s_sessions[i].mac, mac) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

void session_extend(session_t *session, uint64_t additional_ms)
{
    if (!session || !session->active) return;
    session->allotment_ms += additional_ms;
    esp_ip4_addr_t ip = { .addr = session->client_ip };
    ESP_LOGI(TAG, "Session extended: " IPSTR " +%llums (total=%llu)", IP2STR(&ip),
             (unsigned long long)additional_ms, (unsigned long long)session->allotment_ms);
}

bool session_is_expired(const session_t *session)
{
    if (!session || !session->active) return true;

    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg && strcmp(cfg->metric, "bytes") == 0) {
        return session->bytes_consumed >= session->allotment_bytes;
    }

    int64_t elapsed = get_time_ms() - session->start_time_ms;
    return elapsed >= (int64_t)session->allotment_ms;
}

void session_check_expiry(void)
{
    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && session_is_expired(&s_sessions[i])) {
            esp_ip4_addr_t ip = { .addr = s_sessions[i].client_ip };
            ESP_LOGI(TAG, "Session expired: " IPSTR " mac=%s", IP2STR(&ip),
                     s_sessions[i].mac[0] ? s_sessions[i].mac : "unknown");
            session_revoke(&s_sessions[i]);
        }
    }
}

void session_revoke(session_t *session)
{
    if (!session || !session->active) return;
    firewall_revoke_access(session->client_ip);
    session->active = false;
    s_session_count--;
}

void session_revoke_all(void)
{
    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) {
            session_revoke(&s_sessions[i]);
        }
    }
}

int session_active_count(void)
{
    int count = 0;
    for (int i = 0; i < SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) count++;
    }
    return count;
}

void session_tick(void)
{
    session_check_expiry();
}
