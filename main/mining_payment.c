#include "mining_payment.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "mining_payment";

static mining_client_stats_t s_clients[MINING_MAX_CLIENTS];
static int s_client_count = 0;
static double s_current_hashprice = 0.0;
static uint32_t s_current_nbits = 0;
static uint64_t s_current_difficulty = 1;

static int64_t get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint64_t mining_nbits_to_difficulty(uint32_t nbits)
{
    if (nbits == 0) return UINT64_MAX;

    uint32_t exponent = (nbits >> 24) & 0xFF;
    uint32_t mantissa = nbits & 0x007FFFFF;

    if (exponent <= 3) {
        mantissa >>= (8 * (3 - exponent));
        if (mantissa == 0) return UINT64_MAX;
        return 0x00000000FFFF0000ULL / mantissa;
    }

    uint64_t target = (uint64_t)mantissa << (8 * (exponent - 3));
    if (target == 0) return UINT64_MAX;

    uint64_t pdiff = 0x00000000FFFF0000ULL;
    uint64_t diff = pdiff / (target >> (exponent > 7 ? 0 : 0));
    if (diff == 0) diff = 1;
    return diff;
}

double mining_calculate_hashprice(uint32_t nbits)
{
    uint64_t diff = mining_nbits_to_difficulty(nbits);
    if (diff == 0 || diff == UINT64_MAX) return 0.0;

    double network_hashrate_th = (double)diff * 4294967296.0 / 1e12;
    double daily_sats = (double)MINING_BLOCK_SUBSIDY_SATS * (double)MINING_BLOCKS_PER_DAY;
    double sats_per_th_day = daily_sats / network_hashrate_th;
    return sats_per_th_day / 1000.0;
}

double mining_calculate_hashprice_override(uint64_t sats_per_ghs_day)
{
    return (double)sats_per_ghs_day;
}

esp_err_t mining_validate_share(const uint8_t *header80, uint32_t nonce, const uint8_t *target, int target_len)
{
    (void)header80;
    (void)nonce;
    (void)target;
    (void)target_len;
    return ESP_OK;
}

uint64_t mining_shares_to_allotment_ms(double hashrate_ghs, double hashprice_sats_per_ghs_s,
                                         int price_per_step, int step_size_ms)
{
    if (hashrate_ghs <= 0.0 || hashprice_sats_per_ghs_s <= 0.0 || price_per_step <= 0) return 0;

    double sats_per_ms = hashrate_ghs * hashprice_sats_per_ghs_s / 86400000.0;
    double steps_earned = sats_per_ms * (double)step_size_ms / (double)price_per_step;
    uint64_t allotment = (uint64_t)(steps_earned * (double)step_size_ms);
    return allotment > 0 ? allotment : 1;
}

uint64_t mining_shares_to_allotment_bytes(double hashrate_ghs, double hashprice_sats_per_ghs_s,
                                            int price_per_step, int step_size_bytes)
{
    if (hashrate_ghs <= 0.0 || hashprice_sats_per_ghs_s <= 0.0 || price_per_step <= 0) return 0;

    double sats_per_ms = hashrate_ghs * hashprice_sats_per_ghs_s / 86400000.0;
    double steps_earned = sats_per_ms * 1000.0 / (double)price_per_step;
    uint64_t allotment = (uint64_t)(steps_earned * (double)step_size_bytes);
    return allotment > 0 ? allotment : 1;
}

mining_client_stats_t *mining_get_or_create_client(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) return &s_clients[i];
    }

    if (s_client_count >= MINING_MAX_CLIENTS) {
        for (int i = 0; i < MINING_MAX_CLIENTS; i++) {
            int64_t age = get_time_ms() - s_clients[i].last_share_time_ms;
            if (age > MINING_SHARE_WINDOW_S * 2000) {
                memset(&s_clients[i], 0, sizeof(mining_client_stats_t));
                s_clients[i].ip = client_ip;
                s_clients[i].first_share_time_ms = get_time_ms();
                return &s_clients[i];
            }
        }
        return NULL;
    }

    mining_client_stats_t *c = &s_clients[s_client_count];
    memset(c, 0, sizeof(mining_client_stats_t));
    c->ip = client_ip;
    c->first_share_time_ms = get_time_ms();
    s_client_count++;
    return c;
}

void mining_update_hashrate(uint32_t client_ip, bool accepted)
{
    mining_client_stats_t *stats = mining_get_or_create_client(client_ip);
    if (!stats) return;

    if (accepted) {
        stats->shares_accepted++;
    } else {
        stats->shares_rejected++;
    }
    stats->last_share_time_ms = get_time_ms();

    int64_t window_ms = stats->last_share_time_ms - stats->first_share_time_ms;
    if (window_ms < 1000) window_ms = 1000;

    double window_s = (double)window_ms / 1000.0;
    double shares_per_s = (double)stats->shares_accepted / window_s;
    double diff = (s_current_difficulty > 0) ? (double)s_current_difficulty : 1.0;
    stats->hashrate_ghs = shares_per_s * diff * 4294967296.0 / 1e9;
}

const mining_client_stats_t *mining_get_client_stats(uint32_t client_ip)
{
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i].ip == client_ip) return &s_clients[i];
    }
    return NULL;
}

double mining_get_current_hashprice(void)
{
    return s_current_hashprice;
}

void mining_set_current_nbits(uint32_t nbits)
{
    s_current_nbits = nbits;
    s_current_difficulty = mining_nbits_to_difficulty(nbits);
    s_current_hashprice = mining_calculate_hashprice(nbits);
    ESP_LOGI(TAG, "nbits updated: 0x%08lx, diff=%llu, hashprice=%.6f sat/GH/s/day",
             (unsigned long)nbits, (unsigned long long)s_current_difficulty, s_current_hashprice);
}

void mining_payment_init(void)
{
    memset(s_clients, 0, sizeof(s_clients));
    s_client_count = 0;
    s_current_hashprice = 0.0;
    s_current_nbits = 0;
    s_current_difficulty = 1;
    ESP_LOGI(TAG, "Mining payment module initialized");
}
