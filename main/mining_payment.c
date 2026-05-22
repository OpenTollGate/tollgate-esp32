#include "mining_payment.h"
#include "tollgate_core_mining.h"
#include "tollgate_core.h"
#include "esp_log.h"

static const char *TAG = "mining_payment";

uint64_t mining_nbits_to_difficulty(uint32_t nbits)
{
    return tollgate_core_mining_nbits_to_difficulty(nbits);
}

double mining_calculate_hashprice(uint32_t nbits)
{
    return tollgate_core_mining_calc_hashprice(nbits);
}

double mining_calculate_hashprice_override(uint64_t sats_per_ghs_day)
{
    return tollgate_core_mining_calc_hashprice_override(sats_per_ghs_day);
}

esp_err_t mining_validate_share(const uint8_t *header80, uint32_t nonce, const uint8_t *target, int target_len)
{
    return tollgate_core_mining_validate_share(header80, nonce, target, target_len);
}

uint64_t mining_shares_to_allotment_ms(double hashrate_ghs, double hashprice_sats_per_ghs_s, int price_per_step, int step_size_ms)
{
    return tollgate_core_mining_shares_to_allotment_ms(hashrate_ghs, hashprice_sats_per_ghs_s, price_per_step, step_size_ms);
}

uint64_t mining_shares_to_allotment_bytes(double hashrate_ghs, double hashprice_sats_per_ghs_s, int price_per_step, int step_size_bytes)
{
    return tollgate_core_mining_shares_to_allotment_bytes(hashrate_ghs, hashprice_sats_per_ghs_s, price_per_step, step_size_bytes);
}

mining_client_stats_t *mining_get_or_create_client(uint32_t client_ip)
{
    return (mining_client_stats_t *)tollgate_core_mining_get_or_create_client(client_ip);
}

void mining_update_hashrate(uint32_t client_ip, bool accepted)
{
    tollgate_core_mining_update_hashrate(client_ip, accepted);
}

const mining_client_stats_t *mining_get_client_stats(uint32_t client_ip)
{
    return (const mining_client_stats_t *)tollgate_core_mining_get_client_stats(client_ip);
}

double mining_get_current_hashprice(void)
{
    return tollgate_core_mining_get_current_hashprice();
}

void mining_set_current_nbits(uint32_t nbits)
{
    tollgate_core_mining_set_current_nbits(nbits);
}

void mining_payment_init(void)
{
    ESP_LOGI(TAG, "Mining payment initialized (via tollgate_core)");
}
