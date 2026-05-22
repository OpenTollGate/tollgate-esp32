#ifndef SESSION_H
#define SESSION_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define SESSION_MAX_CLIENTS     10
#define SESSION_MAX_MAC_LEN     18

typedef enum {
    PAYMENT_METHOD_CASHU,
    PAYMENT_METHOD_MINING,
    PAYMENT_METHOD_BYTES
} payment_method_t;

typedef struct {
    uint32_t client_ip;
    char mac[SESSION_MAX_MAC_LEN];
    uint64_t allotment_ms;
    int64_t start_time_ms;
    uint64_t allotment_bytes;
    uint64_t bytes_consumed;
    payment_method_t payment_method;
    bool active;
} session_t;

esp_err_t session_manager_init(void);
session_t *session_create(uint32_t client_ip, uint64_t allotment_ms);
session_t *session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes);
void session_add_bytes(uint32_t client_ip, uint64_t bytes);
session_t *session_find_by_ip(uint32_t client_ip);
session_t *session_find_by_mac(const char *mac);
void session_extend(session_t *session, uint64_t additional_ms);
bool session_is_expired(const session_t *session);
void session_check_expiry(void);
void session_revoke(session_t *session);
void session_revoke_all(void);
int session_active_count(void);
void session_tick(void);
session_t *cvm_get_sessions_array(void);
int cvm_get_sessions_count(void);

#endif
