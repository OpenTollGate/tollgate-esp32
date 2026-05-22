#include "tollgate_session.h"
#include "tollgate_core.h"
#include "tollgate_platform.h"
#include "tollgate_firewall.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tg_session";
static tg_session_t s_sessions[TG_SESSION_MAX_CLIENTS];
static int s_session_count = 0;

static void format_ip(uint32_t ip, char *buf, int buf_len)
{
    snprintf(buf, buf_len, "%d.%d.%d.%d",
             (int)((ip >> 0) & 0xFF), (int)((ip >> 8) & 0xFF),
             (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));
}

static int64_t get_time_ms(void)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->get_time_ms) return p->get_time_ms();
    return 0;
}

static void log_session(const char *verb, uint32_t client_ip, const char *mac, const char *detail)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->log_info) {
        char ip_str[16];
        format_ip(client_ip, ip_str, sizeof(ip_str));
        p->log_info(TAG, "%s: %s mac=%s %s", verb, ip_str, mac ? mac : "unknown", detail ? detail : "");
    }
}

int tg_session_init(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    s_session_count = 0;
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->log_info) p->log_info(TAG, "Session manager initialized");
    return 0;
}

static void populate_mac(tg_session_t *session, uint32_t client_ip)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->mac_for_ip) {
        if (!p->mac_for_ip(client_ip, session->mac, sizeof(session->mac))) {
            session->mac[0] = '\0';
        }
    } else {
        session->mac[0] = '\0';
    }
}

tg_session_t *tg_session_create(uint32_t client_ip, uint64_t allotment_ms)
{
    tg_session_t *existing = tg_session_find_by_ip(client_ip);
    if (existing) {
        tg_session_extend(existing, allotment_ms);
        return existing;
    }

    if (s_session_count >= TG_SESSION_MAX_CLIENTS) {
        for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
            if (!s_sessions[i].active || tg_session_is_expired(&s_sessions[i])) {
                tg_session_revoke(&s_sessions[i]);
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
            s_sessions[i].payment_method = TG_PAYMENT_CASHU;
            populate_mac(&s_sessions[i], client_ip);

            s_session_count++;
            tg_firewall_grant(client_ip);

            char detail[64];
            snprintf(detail, sizeof(detail), "allotment=%llums", (unsigned long long)allotment_ms);
            log_session("created", client_ip,
                        s_sessions[i].mac[0] ? s_sessions[i].mac : "unknown", detail);
            return &s_sessions[i];
        }
    }

    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->log_warn) p->log_warn(TAG, "No free session slots");
    return NULL;
}

tg_session_t *tg_session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes)
{
    tg_session_t *s = tg_session_create(client_ip, 0);
    if (s) {
        s->allotment_bytes = allotment_bytes;
        s->bytes_consumed = 0;
        s->allotment_ms = (uint64_t)-1;
        s->payment_method = TG_PAYMENT_BYTES;
        char detail[64];
        snprintf(detail, sizeof(detail), "allotment=%llu bytes", (unsigned long long)allotment_bytes);
        log_session("bytes session", client_ip, s->mac[0] ? s->mac : "unknown", detail);
    }
    return s;
}

void tg_session_add_bytes(uint32_t client_ip, uint64_t bytes)
{
    tg_session_t *s = tg_session_find_by_ip(client_ip);
    if (s && s->active) {
        s->bytes_consumed += bytes;
    }
}

tg_session_t *tg_session_find_by_ip(uint32_t client_ip)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].client_ip == client_ip) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

tg_session_t *tg_session_find_by_mac(const char *mac)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active && s_sessions[i].mac[0] != '\0' &&
            strcmp(s_sessions[i].mac, mac) == 0) {
            return &s_sessions[i];
        }
    }
    return NULL;
}

void tg_session_extend(tg_session_t *session, uint64_t additional_ms)
{
    if (!session || !session->active) return;
    session->allotment_ms += additional_ms;
    char detail[64];
    snprintf(detail, sizeof(detail), "+%llums (total=%llu)",
             (unsigned long long)additional_ms, (unsigned long long)session->allotment_ms);
    log_session("extended", session->client_ip,
                session->mac[0] ? session->mac : "unknown", detail);
}

bool tg_session_is_expired(const tg_session_t *session)
{
    if (!session || !session->active) return true;

    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (p && p->get_metric) {
        const char *metric = p->get_metric();
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
        if (s_sessions[i].active && tg_session_is_expired(&s_sessions[i])) {
            log_session("expired", s_sessions[i].client_ip,
                        s_sessions[i].mac[0] ? s_sessions[i].mac : "unknown", NULL);
            tg_session_revoke(&s_sessions[i]);
        }
    }
}

void tg_session_revoke(tg_session_t *session)
{
    if (!session || !session->active) return;
    tg_firewall_revoke(session->client_ip);
    session->active = false;
    s_session_count--;
}

void tg_session_revoke_all(void)
{
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) {
            tg_session_revoke(&s_sessions[i]);
        }
    }
}

int tg_session_active_count(void)
{
    int count = 0;
    for (int i = 0; i < TG_SESSION_MAX_CLIENTS; i++) {
        if (s_sessions[i].active) count++;
    }
    return count;
}

void tg_session_tick(void)
{
    check_expiry();
}

tg_session_t *tg_session_get_array(void)
{
    return s_sessions;
}

int tg_session_get_array_size(void)
{
    return TG_SESSION_MAX_CLIENTS;
}
