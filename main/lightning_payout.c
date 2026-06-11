#include "lightning_payout.h"
#include "lnurl_pay.h"
#include "nucula_wallet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ln_payout";

static payout_config_t s_config;
static int64_t s_last_check_ms = 0;

static int64_t get_time_ms(void) {
    return (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

esp_err_t lightning_payout_init(const payout_config_t *config)
{
    if (!config) return ESP_FAIL;
    memcpy(&s_config, config, sizeof(s_config));
    s_last_check_ms = get_time_ms();
    ESP_LOGI(TAG, "Payout initialized: %d mints, %d recipients, interval=%ds",
             s_config.mint_count, s_config.recipient_count, s_config.check_interval_s);
    return ESP_OK;
}

static esp_err_t payout_one(const char *lightning_address, uint64_t amount_sats, uint64_t fee_tolerance_pct)
{
    if (amount_sats == 0) return ESP_OK;

    char bolt11[LNURL_MAX_BOLT11_LEN];
    esp_err_t err = lnurl_get_invoice(lightning_address, amount_sats, bolt11, sizeof(bolt11));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get invoice for %s (%llu sats)", lightning_address, (unsigned long long)amount_sats);
        return err;
    }

    uint64_t max_cost = amount_sats + (amount_sats * fee_tolerance_pct / 100);
    err = nucula_wallet_melt(bolt11, max_cost);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Melt failed for %s", lightning_address);
        return err;
    }

    ESP_LOGI(TAG, "Payout: %llu sats -> %s", (unsigned long long)amount_sats, lightning_address);
    return ESP_OK;
}

void lightning_payout_tick(void)
{
    if (!s_config.enabled) return;
    if (s_config.recipient_count == 0) return;

    int64_t now = get_time_ms();
    int64_t elapsed = now - s_last_check_ms;
    int64_t interval_ms = (int64_t)s_config.check_interval_s * 1000;
    if (elapsed < interval_ms) return;

    s_last_check_ms = now;

    uint64_t balance = nucula_wallet_balance();

    if (strcmp(nucula_wallet_unit(), "sat") != 0) {
        ESP_LOGI(TAG, "Wallet unit is '%s', skipping Lightning payout", nucula_wallet_unit());
        return;
    }

    for (int m = 0; m < s_config.mint_count; m++) {
        const payout_mint_config_t *mc = &s_config.mints[m];

        if (balance < mc->min_payout_amount) {
            ESP_LOGI(TAG, "Balance %llu < min_payout %llu for %s, skipping",
                     (unsigned long long)balance, (unsigned long long)mc->min_payout_amount, mc->url);
            continue;
        }

        uint64_t payout_pool = balance - mc->min_balance;
        if (payout_pool == 0) continue;

        ESP_LOGI(TAG, "Payout pool: %llu sats (balance=%llu - reserve=%llu)",
                 (unsigned long long)payout_pool, (unsigned long long)balance,
                 (unsigned long long)mc->min_balance);

        for (int r = 0; r < s_config.recipient_count; r++) {
            const payout_recipient_t *recip = &s_config.recipients[r];
            if (recip->factor <= 0.0 || recip->lightning_address[0] == '\0') continue;

            uint64_t share = (uint64_t)round((double)payout_pool * recip->factor);
            if (share == 0) continue;

            payout_one(recip->lightning_address, share, s_config.fee_tolerance_pct);
        }

        balance = nucula_wallet_balance();
    }
}
