#include "nucula_wallet.h"
#include "wallet.hpp"
#include "cashu_json.hpp"
#include "crypto.h"
#include "hex.h"
#include "esp_log.h"
#include "secp256k1.h"
#include "cJSON.h"
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "nucula_wallet";

static secp256k1_context *s_ctx = nullptr;
static cashu::Wallet *s_wallet = nullptr;

static std::vector<cashu::Proof> &mutable_proofs()
{
    return const_cast<std::vector<cashu::Proof> &>(s_wallet->proofs());
}

esp_err_t nucula_wallet_init(const char *mint_url)
{
    if (s_wallet) return ESP_OK;

    s_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (!s_ctx) {
        ESP_LOGE(TAG, "Failed to create secp256k1 context");
        return ESP_FAIL;
    }

    s_wallet = new cashu::Wallet(std::string(mint_url), s_ctx, 0);
    if (!s_wallet) {
        ESP_LOGE(TAG, "Failed to create wallet");
        secp256k1_context_destroy(s_ctx);
        s_ctx = nullptr;
        return ESP_FAIL;
    }

    s_wallet->load_from_nvs();

    if (!s_wallet->load_keysets()) {
        ESP_LOGW(TAG, "Keyset load failed (may be offline)");
    }

    ESP_LOGI(TAG, "Wallet initialized: balance=%d proofs=%d keysets=%d",
             s_wallet->balance(), (int)s_wallet->proofs().size(),
             (int)s_wallet->keysets().size());
    return ESP_OK;
}

esp_err_t nucula_wallet_receive(const char *token_str)
{
    if (!s_wallet || !token_str) return ESP_FAIL;

    cashu::Token tok;
    bool decoded = false;

    if (strncmp(token_str, "cashuA", 6) == 0) {
        decoded = cashu::deserialize_token_v3(token_str, tok);
    }

    if (!decoded) {
        ESP_LOGE(TAG, "Failed to decode token");
        return ESP_FAIL;
    }

    std::vector<cashu::Proof> proofs_out;
    if (!s_wallet->receive(tok, proofs_out)) {
        ESP_LOGE(TAG, "Receive failed");
        return ESP_FAIL;
    }

    int total = 0;
    for (const auto &p : proofs_out) total += p.amount;
    ESP_LOGI(TAG, "Received %d sat (%d proofs), new balance=%d",
             total, (int)proofs_out.size(), s_wallet->balance());
    return ESP_OK;
}

esp_err_t nucula_wallet_send(uint64_t amount_sat, char *token_out, size_t token_out_size)
{
    if (!s_wallet) return ESP_FAIL;

    int amount = (int)amount_sat;
    std::vector<cashu::Proof> selected, remaining;
    if (!s_wallet->select_proofs(amount, selected, remaining)) {
        ESP_LOGE(TAG, "Insufficient balance for %d sat", amount);
        return ESP_FAIL;
    }

    std::vector<cashu::Proof> new_proofs, change;
    if (!s_wallet->swap(selected, (int)amount_sat, new_proofs, change)) {
        ESP_LOGE(TAG, "Swap for send failed");
        return ESP_FAIL;
    }

    cashu::Token token;
    token.mint = s_wallet->mint_url();
    token.unit = "sat";
    for (auto &p : new_proofs) token.proofs.push_back(p);

    std::string encoded = cashu::serialize_token_v3(token);
    if (encoded.empty()) {
        ESP_LOGE(TAG, "Token serialization failed");
        return ESP_FAIL;
    }

    if (encoded.size() >= token_out_size) {
        ESP_LOGE(TAG, "Token too large: %zu >= %zu", encoded.size(), token_out_size);
        return ESP_FAIL;
    }

    memcpy(token_out, encoded.c_str(), encoded.size() + 1);

    auto &proofs = mutable_proofs();
    proofs = remaining;
    for (auto &p : change) proofs.push_back(p);
    s_wallet->save_proofs();

    ESP_LOGI(TAG, "Sent %llu sat, token=%zu bytes, remaining balance=%d",
             (unsigned long long)amount_sat, encoded.size(), s_wallet->balance());
    return ESP_OK;
}

uint64_t nucula_wallet_balance(void)
{
    if (!s_wallet) return 0;
    return (uint64_t)s_wallet->balance();
}

int nucula_wallet_proof_count(void)
{
    if (!s_wallet) return 0;
    return (int)s_wallet->proofs().size();
}

char *nucula_wallet_proofs_json(void)
{
    if (!s_wallet) return nullptr;

    const auto &proofs = s_wallet->proofs();
    cJSON *arr = cJSON_CreateArray();
    for (const auto &p : proofs) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "amount", p.amount);
        cJSON_AddStringToObject(obj, "id", p.id.c_str());
        cJSON_AddItemToArray(arr, obj);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

esp_err_t nucula_wallet_swap_all(void)
{
    if (!s_wallet) return ESP_FAIL;

    auto &proofs = mutable_proofs();
    if (proofs.empty()) {
        ESP_LOGW(TAG, "No proofs to swap");
        return ESP_FAIL;
    }

    int old_balance = s_wallet->balance();

    std::vector<cashu::Proof> inputs = proofs;
    std::vector<cashu::Proof> new_proofs, change;
    if (!s_wallet->swap(inputs, -1, new_proofs, change)) {
        ESP_LOGE(TAG, "Swap failed");
        return ESP_FAIL;
    }

    proofs.clear();
    for (auto &p : new_proofs) proofs.push_back(p);
    for (auto &p : change) proofs.push_back(p);
    s_wallet->save_proofs();

    ESP_LOGI(TAG, "Swap complete: %d -> %d sat (%d proofs)",
             old_balance, s_wallet->balance(), (int)proofs.size());
    return ESP_OK;
}

void nucula_wallet_print_status(void)
{
    if (!s_wallet) {
        ESP_LOGI(TAG, "Wallet not initialized");
        return;
    }
    ESP_LOGI(TAG, "Wallet: balance=%d proofs=%d keysets=%d",
             s_wallet->balance(), (int)s_wallet->proofs().size(),
             (int)s_wallet->keysets().size());
    const auto &proofs = s_wallet->proofs();
    for (size_t i = 0; i < proofs.size(); i++) {
        ESP_LOGI(TAG, "  [%d] amount=%d id=%s", (int)i,
                 proofs[i].amount, proofs[i].id.c_str());
    }
}

esp_err_t nucula_wallet_melt(const char *bolt11_invoice, uint64_t max_fee_sats)
{
    if (!s_wallet || !bolt11_invoice) return ESP_FAIL;

    cashu::MeltQuote quote;
    if (!s_wallet->request_melt_quote(std::string(bolt11_invoice), quote)) {
        ESP_LOGE(TAG, "Melt quote request failed");
        return ESP_FAIL;
    }

    uint64_t total_cost = (uint64_t)quote.amount + (uint64_t)quote.fee_reserve;
    if (total_cost > max_fee_sats) {
        ESP_LOGE(TAG, "Melt cost %llu exceeds max %llu (amount=%d fee=%d)",
                 (unsigned long long)total_cost, (unsigned long long)max_fee_sats,
                 quote.amount, quote.fee_reserve);
        return ESP_FAIL;
    }

    int balance_before = s_wallet->balance();
    if (balance_before < quote.amount) {
        ESP_LOGE(TAG, "Insufficient balance: %d < %d", balance_before, quote.amount);
        return ESP_FAIL;
    }

    int change_amount = 0;
    if (!s_wallet->melt_tokens(quote, change_amount)) {
        ESP_LOGE(TAG, "Melt tokens failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Melted: %d sats paid, %d change, balance=%d->%d",
             quote.amount, change_amount, balance_before, s_wallet->balance());
    return ESP_OK;
}
