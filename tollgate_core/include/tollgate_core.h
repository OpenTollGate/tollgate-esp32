#ifndef TOLLGATE_CORE_H
#define TOLLGATE_CORE_H

#include "tollgate_platform.h"
#include <stdbool.h>
#include <stdint.h>

int tollgate_core_init(const tollgate_platform_t *platform, uint32_t ap_ip);
void tollgate_core_tick(void);

int tollgate_core_dns_start(uint32_t upstream_dns);
void tollgate_core_dns_stop(void);

int tollgate_core_process_payment(uint32_t client_ip, const char *token_str);
int tollgate_core_process_share(uint32_t client_ip, const char *job_id,
                                const char *nonce, const char *ntime, const char *version);

void tollgate_core_client_connected(const uint8_t *mac, uint32_t client_ip);
void tollgate_core_client_disconnected(const uint8_t *mac);

bool tollgate_core_is_client_allowed(uint32_t client_ip);
bool tollgate_core_is_dns_running(void);

char *tollgate_core_get_status_json(void);
char *tollgate_core_get_config_json(void);

int tollgate_core_active_session_count(void);
int tollgate_core_allowed_client_count(void);
int tollgate_core_firewall_revoke_all(void);
void tollgate_core_firewall_set_mining_port(uint16_t port);
void tollgate_core_firewall_set_sandbox_mint_access(bool enable);

bool tollgate_core_is_owner(uint32_t client_ip);
bool tollgate_core_is_owner_connected(void);

double tollgate_core_get_hashprice(void);
void tollgate_core_set_nbits(uint32_t nbits);
const void *tollgate_core_get_current_job(void);
void tollgate_core_set_job(const void *job);

int tollgate_core_stratum_client_start(void);
void tollgate_core_stratum_client_stop(void);

int tollgate_core_stratum_proxy_init(uint16_t port);
void tollgate_core_stratum_proxy_get_stats(void *out);

void tollgate_core_mining_init(void);

void tollgate_core_beacon_start(void);

void tollgate_core_market_init(void);
void tollgate_core_market_on_scan_result(const void *ie_data, int ie_len,
                                          const uint8_t *bssid, int rssi);

const void *tollgate_core_get_sessions_array(void);
int tollgate_core_get_sessions_array_size(void);
void *tollgate_core_find_session_by_ip(uint32_t ip);
void *tollgate_core_find_session_by_mac(const char *mac);
void *tollgate_core_session_create(uint32_t client_ip, uint64_t allotment_ms);
void *tollgate_core_session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes);
void tollgate_core_session_extend(void *session, uint64_t additional_ms);
void tollgate_core_session_revoke(void *session);
int tollgate_core_session_add_bytes(uint32_t client_ip, uint64_t bytes);
bool tollgate_core_session_is_expired(const void *session);

void tollgate_core_firewall_grant(uint32_t client_ip);
void tollgate_core_firewall_revoke(uint32_t client_ip);
int tollgate_core_firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, int mac_out_size);

int tollgate_core_cashu_decode(const char *token_str, void *out);
int tollgate_core_cashu_check_states(const char *mint_url, const void *token,
                                     void *states, int *state_count);
uint64_t tollgate_core_cashu_allotment(uint64_t amount, uint64_t price, uint64_t step_size);
bool tollgate_core_cashu_is_mint_accepted(const char *mint_url);
const char *tollgate_core_cashu_token_mint(const void *token);
uint64_t tollgate_core_cashu_token_amount(const void *token);

void tollgate_core_mining_update_hashrate(uint32_t client_ip, bool accepted);
const void *tollgate_core_mining_get_client_stats(uint32_t client_ip);
double tollgate_core_mining_get_hashprice(void);
uint64_t tollgate_core_mining_shares_to_allotment_ms(double hashrate, double hashprice,
                                                     int price, int step_ms);
uint64_t tollgate_core_mining_shares_to_allotment_bytes(double hashrate, double hashprice,
                                                        int price, int step_bytes);
void tollgate_core_mining_set_nbits(uint32_t nbits);

const tollgate_platform_t *tollgate_core_get_platform(void);
uint32_t tollgate_core_get_ap_ip(void);

#endif
