#include "cashu.h"
#include "tollgate_core_cashu.h"
#include "tollgate_core.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "cashu";

esp_err_t cashu_decode_token(const char *token_str, cashu_token_t *out)
{
    return tollgate_core_cashu_decode_token(token_str, (tg_cashu_token_t *)out);
}

esp_err_t cashu_check_proof_states(const char *mint_url, const cashu_token_t *token,
                                   cashu_proof_state_t *states, int *state_count)
{
    return tollgate_core_cashu_check_proof_states(mint_url, (const tg_cashu_token_t *)token,
                                                   (tg_cashu_proof_state_t *)states, state_count);
}

uint64_t cashu_calculate_allotment_ms(uint64_t token_amount, uint64_t price_per_step,
                                       uint64_t step_size_ms)
{
    return tollgate_core_cashu_calculate_allotment(token_amount, price_per_step, step_size_ms);
}

uint64_t cashu_calculate_allotment(uint64_t token_amount, uint64_t price_per_step,
                                    const char *metric, uint64_t step_size)
{
    (void)metric;
    return tollgate_core_cashu_calculate_allotment(token_amount, price_per_step, step_size);
}

bool cashu_is_mint_accepted(const char *mint_url)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    if (cfg) {
        for (int i = 0; i < cfg->accepted_mint_count; i++) {
            if (tollgate_core_cashu_is_mint_accepted(mint_url, cfg->accepted_mints[i]))
                return true;
        }
    }
    return false;
}
