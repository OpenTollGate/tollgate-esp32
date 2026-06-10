#include "tollgate_esp_platform.h"
#include "tollgate_core.h"
#include "config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tg_esp";

static uint16_t esp_get_price_sats(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (uint16_t)cfg->price_per_step : 21;
}

static int32_t esp_get_step_ms(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int32_t)cfg->step_size_ms : 60000;
}

static const char *esp_get_mint_url(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mint_url : NULL;
}

static const char *esp_get_metric(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->metric : "milliseconds";
}

static int32_t esp_get_step_bytes(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int32_t)cfg->step_size_bytes : 22020096;
}

static int64_t esp_get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static bool esp_spend_proofs(const char *raw_token_json)
{
    (void)raw_token_json;
    return true;
}

static const char *esp_get_stratum_url(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_host : NULL;
}

static uint16_t esp_get_stratum_port(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_port : 3333;
}

static const char *esp_get_stratum_user(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_user : NULL;
}

static const char *esp_get_stratum_pass(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_pass : NULL;
}

static const char *esp_get_stratum_fallback_url(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_fallback_host : NULL;
}

static uint16_t esp_get_stratum_fallback_port(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->stratum_fallback_port : 3333;
}

static uint16_t esp_get_mining_port(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mining_port : 3334;
}

static const char *esp_get_mining_payout_mode(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? "upstream" : "upstream";
}

static uint64_t esp_get_hashprice_sats_per_ghs_day(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->hashprice_sats_per_ghs_day : 0;
}

static void esp_on_share_accepted(double difficulty)
{
    (void)difficulty;
}

static double esp_get_hashrate(void)
{
    return 0.0;
}

static const tollgate_platform_t s_platform = {
    .get_price_sats           = esp_get_price_sats,
    .get_step_ms              = esp_get_step_ms,
    .get_mint_url             = esp_get_mint_url,
    .get_metric               = esp_get_metric,
    .get_step_bytes           = esp_get_step_bytes,
    .get_time_ms              = esp_get_time_ms,
    .spend_proofs             = esp_spend_proofs,
    .get_stratum_url          = esp_get_stratum_url,
    .get_stratum_port         = esp_get_stratum_port,
    .get_stratum_user         = esp_get_stratum_user,
    .get_stratum_pass         = esp_get_stratum_pass,
    .get_stratum_fallback_url = esp_get_stratum_fallback_url,
    .get_stratum_fallback_port = esp_get_stratum_fallback_port,
    .get_mining_port          = esp_get_mining_port,
    .get_mining_payout_mode   = esp_get_mining_payout_mode,
    .get_hashprice_sats_per_ghs_day = esp_get_hashprice_sats_per_ghs_day,
    .on_share_accepted        = esp_on_share_accepted,
    .get_hashrate             = esp_get_hashrate,
};

const tollgate_platform_t *tollgate_esp_get_platform(void)
{
    return &s_platform;
}
