#ifndef TOLLGATE_CORE_SESSION_H
#define TOLLGATE_CORE_SESSION_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TG_SESSION_MAX_CLIENTS     10
#define TG_SESSION_MAX_MAC_LEN     18

typedef struct {
    uint32_t client_ip;
    char mac[TG_SESSION_MAX_MAC_LEN];
    uint64_t allotment_ms;
    int64_t start_time_ms;
    uint64_t allotment_bytes;
    uint64_t bytes_consumed;
    bool active;
} tg_session_t;

esp_err_t tollgate_core_session_init(void);

tg_session_t *tollgate_core_session_create(uint32_t client_ip, uint64_t allotment_ms);
tg_session_t *tollgate_core_session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes);

void tollgate_core_session_add_bytes(uint32_t client_ip, uint64_t bytes);

tg_session_t *tollgate_core_session_find_by_ip(uint32_t client_ip);
tg_session_t *tollgate_core_session_find_by_mac(const char *mac);

void tollgate_core_session_extend(tg_session_t *session, uint64_t additional_ms);

bool tollgate_core_session_is_expired(const tg_session_t *session);

void tollgate_core_session_revoke(tg_session_t *session);
void tollgate_core_session_revoke_all(void);

int tollgate_core_session_active_count(void);

void tollgate_core_session_tick(void);

tg_session_t *tollgate_core_session_get_array(void);
int tollgate_core_session_get_array_size(void);

#endif
