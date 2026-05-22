#include "session.h"
#include "tollgate_core_session.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "session";

esp_err_t session_manager_init(void)
{
    tollgate_core_session_init();
    ESP_LOGI(TAG, "Session manager initialized (via tollgate_core)");
    return ESP_OK;
}

session_t *session_create(uint32_t client_ip, uint64_t allotment_ms)
{
    return (session_t *)tollgate_core_session_create(client_ip, allotment_ms);
}

session_t *session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes)
{
    return (session_t *)tollgate_core_session_create_bytes(client_ip, allotment_bytes);
}

void session_add_bytes(uint32_t client_ip, uint64_t bytes)
{
    tollgate_core_session_add_bytes(client_ip, bytes);
}

session_t *session_find_by_ip(uint32_t client_ip)
{
    return (session_t *)tollgate_core_session_find_by_ip(client_ip);
}

session_t *session_find_by_mac(const char *mac)
{
    return (session_t *)tollgate_core_session_find_by_mac(mac);
}

void session_extend(session_t *session, uint64_t additional_ms)
{
    tollgate_core_session_extend((tg_session_t *)session, additional_ms);
}

bool session_is_expired(const session_t *session)
{
    return tollgate_core_session_is_expired((const tg_session_t *)session);
}

void session_check_expiry(void)
{
}

void session_revoke(session_t *session)
{
    tollgate_core_session_revoke((tg_session_t *)session);
}

void session_revoke_all(void)
{
    tollgate_core_session_revoke_all();
}

int session_active_count(void)
{
    return tollgate_core_active_session_count();
}

void session_tick(void)
{
    tollgate_core_tick();
}

session_t *cvm_get_sessions_array(void)
{
    return (session_t *)tollgate_core_session_get_array();
}

int cvm_get_sessions_count(void)
{
    return tollgate_core_session_get_array_size();
}
