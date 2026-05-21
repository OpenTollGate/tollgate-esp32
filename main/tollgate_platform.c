#include "tollgate_platform.h"
#include "tollgate_core.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tollgate_platform";

static uint16_t platform_get_price_sats(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (uint16_t)cfg->price_per_step : 21;
}

static int32_t platform_get_step_ms(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int32_t)cfg->step_size_ms : 60000;
}

static const char *platform_get_mint_url(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->mint_url : "https://testnut-nutshell.mints.orangesync.tech";
}

static const char *platform_get_metric(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? cfg->metric : "milliseconds";
}

static int32_t platform_get_step_bytes(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    return cfg ? (int32_t)cfg->step_size_bytes : 22020096;
}

static int64_t platform_get_time_ms(void)
{
    return (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static bool platform_spend_proofs(const char *raw_token_json)
{
    (void)raw_token_json;
    return true;
}

const tollgate_platform_t *tollgate_get_platform(void)
{
    static const tollgate_platform_t platform = {
        .get_price_sats = platform_get_price_sats,
        .get_step_ms = platform_get_step_ms,
        .get_mint_url = platform_get_mint_url,
        .get_metric = platform_get_metric,
        .get_step_bytes = platform_get_step_bytes,
        .get_time_ms = platform_get_time_ms,
        .spend_proofs = platform_spend_proofs,
    };
    return &platform;
}
