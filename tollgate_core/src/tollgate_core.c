#include "tollgate_core.h"
#include "tollgate_platform.h"
#include "tollgate_cashu.h"
#include "tollgate_session.h"
#include "tollgate_firewall.h"
#include "tollgate_mining.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "tg_core";
static const tollgate_platform_t *s_platform;
static uint32_t s_ap_ip;

static uint32_t s_owner_ip;
static uint8_t s_owner_mac[6];
static bool s_owner_connected;

const tollgate_platform_t *tollgate_core_get_platform(void)
{
    return s_platform;
}

uint32_t tollgate_core_get_ap_ip(void)
{
    return s_ap_ip;
}

int tollgate_core_init(const tollgate_platform_t *platform, uint32_t ap_ip)
{
    if (!platform) return -1;

    s_platform = platform;
    s_ap_ip = ap_ip;
    s_owner_connected = false;
    memset(s_owner_mac, 0, sizeof(s_owner_mac));

    tg_session_init();
    tg_firewall_init(ap_ip);
    tg_mining_init();

    if (platform->log_info) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 (int)((ap_ip >> 0) & 0xFF), (int)((ap_ip >> 8) & 0xFF),
                 (int)((ap_ip >> 16) & 0xFF), (int)((ap_ip >> 24) & 0xFF));
        platform->log_info(TAG, "TollGate core initialized, AP IP=%s", ip_str);
    }
    return 0;
}

void tollgate_core_tick(void)
{
    tg_session_tick();
}

int tollgate_core_process_payment(uint32_t client_ip, const char *token_str)
{
    if (!s_platform || !token_str) return -1;

    const char *accepted_mint = s_platform->get_mint_url ? s_platform->get_mint_url() : NULL;
    if (!accepted_mint || accepted_mint[0] == '\0') {
        if (s_platform->log_error) s_platform->log_error(TAG, "No mint URL configured");
        return -1;
    }

    tg_cashu_token_t token;
    if (tg_cashu_decode_token(token_str, &token) != 0) {
        if (s_platform->log_error) s_platform->log_error(TAG, "Token decode failed");
        return -1;
    }

    bool mint_ok = false;
    if (s_platform->get_accepted_mint_count && s_platform->get_accepted_mint) {
        int count = s_platform->get_accepted_mint_count();
        for (int i = 0; i < count; i++) {
            if (tg_cashu_is_mint_accepted(token.mint_url, s_platform->get_accepted_mint(i))) {
                mint_ok = true;
                break;
            }
        }
    } else {
        mint_ok = tg_cashu_is_mint_accepted(token.mint_url, accepted_mint);
    }
    if (!mint_ok) {
        if (s_platform->log_error) s_platform->log_error(TAG, "Token mint not accepted");
        return -1;
    }

    tg_cashu_proof_state_t states[TG_CASHU_MAX_PROOFS];
    int state_count = 0;
    if (tg_cashu_check_proof_states(token.mint_url, &token, states, &state_count) != 0) {
        if (s_platform->log_error) s_platform->log_error(TAG, "Proof state check failed (continuing)");
    } else {
        for (int i = 0; i < state_count; i++) {
            if (states[i].spent) {
                if (s_platform->log_error) s_platform->log_error(TAG, "Proof %d is SPENT", i);
                return -1;
            }
        }
    }

    if (s_platform->wallet_receive) {
        if (!s_platform->wallet_receive(token_str)) {
            if (s_platform->log_error) s_platform->log_error(TAG, "wallet_receive rejected token");
            return -1;
        }
    }

    const char *metric = (s_platform->get_metric) ? s_platform->get_metric() : "milliseconds";
    uint64_t price = (s_platform->get_price_sats) ? s_platform->get_price_sats() : 21;
    uint64_t step_size;

    if (strcmp(metric, "bytes") == 0) {
        step_size = (s_platform->get_step_bytes) ? (uint64_t)s_platform->get_step_bytes() : 22020096;
    } else {
        step_size = (s_platform->get_step_ms) ? (uint64_t)s_platform->get_step_ms() : 60000;
    }

    uint64_t allotment = tg_cashu_calculate_allotment(token.total_amount, price, step_size);
    if (allotment == 0) {
        if (s_platform->log_error) s_platform->log_error(TAG, "Token amount too small");
        return -1;
    }

    if (strcmp(metric, "bytes") == 0) {
        if (!tg_session_create_bytes(client_ip, allotment)) return -1;
    } else {
        if (!tg_session_create(client_ip, allotment)) return -1;
    }

    if (s_platform->log_info) s_platform->log_info(TAG, "Payment: %llu sats -> %llu %s",
             (unsigned long long)token.total_amount, (unsigned long long)allotment, metric);
    return 0;
}

int tollgate_core_process_share(uint32_t client_ip, const char *job_id,
                                 const char *nonce, const char *ntime, const char *version)
{
    (void)job_id; (void)nonce; (void)ntime; (void)version;
    if (!s_platform) return -1;

    tg_mining_update_hashrate(client_ip, true);
    const tg_mining_client_stats_t *stats = tg_mining_get_client_stats(client_ip);
    if (!stats) return -1;

    double hashprice = tg_mining_get_current_hashprice();
    uint64_t override = (s_platform->get_hashprice_override) ? s_platform->get_hashprice_override() : 0;
    if (override > 0) hashprice = tg_mining_calculate_hashprice_override(override);

    const char *metric = (s_platform->get_metric) ? s_platform->get_metric() : "milliseconds";
    int price = (s_platform->get_price_sats) ? s_platform->get_price_sats() : 21;

    tg_session_t *existing = tg_session_find_by_ip(client_ip);
    if (existing && existing->payment_method == TG_PAYMENT_MINING) {
        uint64_t allotment;
        if (strcmp(metric, "bytes") == 0) {
            int step_bytes = (s_platform->get_step_bytes) ? (int)s_platform->get_step_bytes() : 22020096;
            allotment = tg_mining_shares_to_allotment_bytes(stats->hashrate_ghs, hashprice, price, step_bytes);
            existing->allotment_bytes += allotment;
        } else {
            int step_ms = (s_platform->get_step_ms) ? s_platform->get_step_ms() : 60000;
            allotment = tg_mining_shares_to_allotment_ms(stats->hashrate_ghs, hashprice, price, step_ms);
            tg_session_extend(existing, allotment);
        }
        return 0;
    }

    uint64_t allotment;
    if (strcmp(metric, "bytes") == 0) {
        int step_bytes = (s_platform->get_step_bytes) ? (int)s_platform->get_step_bytes() : 22020096;
        allotment = tg_mining_shares_to_allotment_bytes(stats->hashrate_ghs, hashprice, price, step_bytes);
        tg_session_t *s = tg_session_create_bytes(client_ip, allotment);
        if (s) s->payment_method = TG_PAYMENT_MINING;
    } else {
        int step_ms = (s_platform->get_step_ms) ? s_platform->get_step_ms() : 60000;
        allotment = tg_mining_shares_to_allotment_ms(stats->hashrate_ghs, hashprice, price, step_ms);
        tg_session_t *s = tg_session_create(client_ip, allotment);
        if (s) s->payment_method = TG_PAYMENT_MINING;
    }

    return 0;
}

void tollgate_core_client_connected(const uint8_t *mac, uint32_t client_ip)
{
    if (!s_owner_connected) {
        s_owner_connected = true;
        s_owner_ip = client_ip;
        if (mac) memcpy(s_owner_mac, mac, 6);
        if (s_platform && s_platform->log_info) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                     (int)((client_ip >> 0) & 0xFF), (int)((client_ip >> 8) & 0xFF),
                     (int)((client_ip >> 16) & 0xFF), (int)((client_ip >> 24) & 0xFF));
            s_platform->log_info(TAG, "First client = owner: %s", ip_str);
        }
        return;
    }
    if (s_platform && s_platform->log_info) s_platform->log_info(TAG, "Client connected (non-owner)");
}

void tollgate_core_client_disconnected(const uint8_t *mac)
{
    if (!s_owner_connected) return;
    if (mac && memcmp(s_owner_mac, mac, 6) == 0) {
        s_owner_connected = false;
        memset(s_owner_mac, 0, sizeof(s_owner_mac));
        if (s_platform && s_platform->log_info) s_platform->log_info(TAG, "Owner disconnected");
    }
}

bool tollgate_core_is_client_allowed(uint32_t client_ip)
{
    return tg_firewall_is_allowed(client_ip);
}

bool tollgate_core_is_dns_running(void)
{
    return false;
}

char *tollgate_core_get_status_json(void)
{
    const int BUFSIZE = 512;
    char *json = malloc(BUFSIZE);
    if (!json) return NULL;
    snprintf(json, BUFSIZE,
             "{\"ownerConnected\":%s,\"activeSessions\":%d,\"allowedClients\":%d,\"dnsRunning\":%s}",
             s_owner_connected ? "true" : "false",
             tg_session_active_count(),
             tg_firewall_client_count(),
             tollgate_core_is_dns_running() ? "true" : "false");
    return json;
}

char *tollgate_core_get_config_json(void)
{
    const int BUFSIZE = 512;
    char *json = malloc(BUFSIZE);
    if (!json) return NULL;
    int pos = 0;
    pos += snprintf(json + pos, BUFSIZE - pos, "{");
    if (s_platform) {
        if (s_platform->get_price_sats)
            pos += snprintf(json + pos, BUFSIZE - pos, "\"priceSats\":%d,", (int)s_platform->get_price_sats());
        if (s_platform->get_step_ms)
            pos += snprintf(json + pos, BUFSIZE - pos, "\"stepMs\":%d,", (int)s_platform->get_step_ms());
        if (s_platform->get_mint_url)
            pos += snprintf(json + pos, BUFSIZE - pos, "\"mintUrl\":\"%s\",", s_platform->get_mint_url());
        if (s_platform->get_metric)
            pos += snprintf(json + pos, BUFSIZE - pos, "\"metric\":\"%s\"", s_platform->get_metric());
    }
    pos += snprintf(json + pos, BUFSIZE - pos, "}");
    return json;
}

int tollgate_core_active_session_count(void)
{
    return tg_session_active_count();
}

int tollgate_core_allowed_client_count(void)
{
    return tg_firewall_client_count();
}

int tollgate_core_firewall_revoke_all(void)
{
    tg_session_revoke_all();
    return tg_firewall_revoke_all();
}

void tollgate_core_firewall_set_mining_port(uint16_t port)
{
    tg_firewall_set_mining_port(port);
}

void tollgate_core_firewall_set_sandbox_mint_access(bool enable)
{
    tg_firewall_set_sandbox_mint_access(enable);
}

bool tollgate_core_is_owner(uint32_t client_ip)
{
    return s_owner_connected && s_owner_ip == client_ip;
}

bool tollgate_core_is_owner_connected(void)
{
    return s_owner_connected;
}

double tollgate_core_get_hashprice(void)
{
    return tg_mining_get_current_hashprice();
}

void tollgate_core_set_nbits(uint32_t nbits)
{
    tg_mining_set_current_nbits(nbits);
}

const void *tollgate_core_get_current_job(void) { return NULL; }
void tollgate_core_set_job(const void *job) { (void)job; }
int tollgate_core_stratum_client_start(void) { return -1; }
void tollgate_core_stratum_client_stop(void) { }
int tollgate_core_stratum_proxy_init(uint16_t port) { (void)port; return -1; }
void tollgate_core_stratum_proxy_get_stats(void *out) { (void)out; }
void tollgate_core_beacon_start(void) { }
void tollgate_core_market_init(void) { }
void tollgate_core_market_on_scan_result(const void *ie_data, int ie_len, const uint8_t *bssid, int rssi)
{
    (void)ie_data; (void)ie_len; (void)bssid; (void)rssi;
}

const void *tollgate_core_get_sessions_array(void)
{
    return tg_session_get_array();
}

int tollgate_core_get_sessions_array_size(void)
{
    return tg_session_get_array_size();
}

void *tollgate_core_find_session_by_ip(uint32_t ip)
{
    return tg_session_find_by_ip(ip);
}

void *tollgate_core_find_session_by_mac(const char *mac)
{
    return tg_session_find_by_mac(mac);
}

void tollgate_core_session_extend(void *session, uint64_t additional_ms)
{
    tg_session_extend((tg_session_t *)session, additional_ms);
}

int tollgate_core_session_add_bytes(uint32_t client_ip, uint64_t bytes)
{
    tg_session_add_bytes(client_ip, bytes);
    return 0;
}

void *tollgate_core_session_create(uint32_t client_ip, uint64_t allotment_ms)
{
    return tg_session_create(client_ip, allotment_ms);
}

void *tollgate_core_session_create_bytes(uint32_t client_ip, uint64_t allotment_bytes)
{
    return tg_session_create_bytes(client_ip, allotment_bytes);
}

void tollgate_core_session_revoke(void *session)
{
    tg_session_revoke((tg_session_t *)session);
}

bool tollgate_core_session_is_expired(const void *session)
{
    return tg_session_is_expired((const tg_session_t *)session);
}

void tollgate_core_firewall_grant(uint32_t client_ip)
{
    tg_firewall_grant(client_ip);
}

void tollgate_core_firewall_revoke(uint32_t client_ip)
{
    tg_firewall_revoke(client_ip);
}

int tollgate_core_firewall_get_mac_for_ip(uint32_t client_ip, char *mac_out, int mac_out_size)
{
    return tg_firewall_get_mac_for_ip(client_ip, mac_out, mac_out_size);
}

int tollgate_core_cashu_decode(const char *token_str, void *out)
{
    return tg_cashu_decode_token(token_str, (tg_cashu_token_t *)out);
}

int tollgate_core_cashu_check_states(const char *mint_url, const void *token,
                                     void *states, int *state_count)
{
    return tg_cashu_check_proof_states(mint_url, (const tg_cashu_token_t *)token,
                                       (tg_cashu_proof_state_t *)states, state_count);
}

uint64_t tollgate_core_cashu_allotment(uint64_t amount, uint64_t price, uint64_t step_size)
{
    return tg_cashu_calculate_allotment(amount, price, step_size);
}

bool tollgate_core_cashu_is_mint_accepted(const char *mint_url)
{
    if (!s_platform) return false;
    const char *accepted = s_platform->get_mint_url ? s_platform->get_mint_url() : NULL;
    if (!accepted) return false;
    if (s_platform->get_accepted_mint_count && s_platform->get_accepted_mint) {
        int count = s_platform->get_accepted_mint_count();
        for (int i = 0; i < count; i++) {
            if (tg_cashu_is_mint_accepted(mint_url, s_platform->get_accepted_mint(i)))
                return true;
        }
        return false;
    }
    return tg_cashu_is_mint_accepted(mint_url, accepted);
}

const char *tollgate_core_cashu_token_mint(const void *token)
{
    const tg_cashu_token_t *t = (const tg_cashu_token_t *)token;
    return t->mint_url;
}

uint64_t tollgate_core_cashu_token_amount(const void *token)
{
    const tg_cashu_token_t *t = (const tg_cashu_token_t *)token;
    return t->total_amount;
}

void tollgate_core_mining_update_hashrate(uint32_t client_ip, bool accepted)
{
    tg_mining_update_hashrate(client_ip, accepted);
}

const void *tollgate_core_mining_get_client_stats(uint32_t client_ip)
{
    return tg_mining_get_client_stats(client_ip);
}

double tollgate_core_mining_get_hashprice(void)
{
    return tg_mining_get_current_hashprice();
}

uint64_t tollgate_core_mining_shares_to_allotment_ms(double hashrate, double hashprice,
                                                     int price, int step_ms)
{
    return tg_mining_shares_to_allotment_ms(hashrate, hashprice, price, step_ms);
}

uint64_t tollgate_core_mining_shares_to_allotment_bytes(double hashrate, double hashprice,
                                                        int price, int step_bytes)
{
    return tg_mining_shares_to_allotment_bytes(hashrate, hashprice, price, step_bytes);
}

void tollgate_core_mining_set_nbits(uint32_t nbits)
{
    tg_mining_set_current_nbits(nbits);
}

int tollgate_core_dns_start(uint32_t upstream_dns)
{
    (void)upstream_dns;
    return -1;
}

void tollgate_core_dns_stop(void) { }
