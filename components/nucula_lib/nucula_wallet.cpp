#include "nucula_wallet.h"
#include "wallet.hpp"
#include "cashu_json.hpp"
#include "crypto.h"
#include "hex.h"
#include "esp_log.h"
#include "esp_random.h"
#include "secp256k1.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "nucula_wallet";

static const int MAX_WALLETS = 4;

static secp256k1_context *s_ctx = nullptr;
static cashu::Wallet *s_wallets[MAX_WALLETS] = {};
static int s_wallet_count = 0;
static char s_wallet_urls[MAX_WALLETS][256] = {};
static bool s_keysets_loaded[MAX_WALLETS] = {};
static int s_keyset_attempts[MAX_WALLETS] = {};
static SemaphoreHandle_t s_wallet_mutex = nullptr;

static const int KEYSET_BASE_DELAY_MS = 1000;
static const int KEYSET_MAX_DELAY_MS = 30000;
static const int KEYSET_JITTER_MS = 500;

struct WalletLock {
    WalletLock() {
        if (s_wallet_mutex) xSemaphoreTake(s_wallet_mutex, pdMS_TO_TICKS(10000));
    }
    ~WalletLock() {
        if (s_wallet_mutex) xSemaphoreGive(s_wallet_mutex);
    }
};

static bool ensure_keysets(int slot)
{
    if (slot < 0 || slot >= MAX_WALLETS || !s_wallets[slot])
        return false;

    if (s_keysets_loaded[slot])
        return true;

    if (!s_wallets[slot]->keysets().empty()) {
        s_keysets_loaded[slot] = true;
        return true;
    }

    int delay_ms = KEYSET_BASE_DELAY_MS << s_keyset_attempts[slot];
    if (delay_ms > KEYSET_MAX_DELAY_MS)
        delay_ms = KEYSET_MAX_DELAY_MS;
    delay_ms += (int)(esp_random() % KEYSET_JITTER_MS);

    ESP_LOGI(TAG, "Keyset load attempt %d for slot %d, waiting %d ms",
             s_keyset_attempts[slot] + 1, slot, delay_ms);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    if (s_wallets[slot]->load_keysets()) {
        int attempts = s_keyset_attempts[slot] + 1;
        s_keysets_loaded[slot] = true;
        s_keyset_attempts[slot] = 0;
        ESP_LOGI(TAG, "Keyset load succeeded for slot %d after %d attempt(s)",
                 slot, attempts);
        return true;
    }

    s_keyset_attempts[slot]++;
    return false;
}

static int wallet_slot(const cashu::Wallet *w)
{
    for (int i = 0; i < s_wallet_count; i++)
        if (s_wallets[i] == w) return i;
    return -1;
}

static cashu::Wallet *find_wallet_for_token(const cashu::Token &tok)
{
    for (int i = 0; i < s_wallet_count; i++) {
        if (s_wallets[i] && !s_wallets[i]->mint_url().empty()) {
            if (tok.mint.find(s_wallets[i]->mint_url()) != std::string::npos ||
                s_wallets[i]->mint_url().find(tok.mint) != std::string::npos) {
                return s_wallets[i];
            }
        }
    }
    if (s_wallet_count > 0 && s_wallets[0]) return s_wallets[0];
    return nullptr;
}

static cashu::Wallet *find_wallet_for_send(int amount)
{
    for (int i = 0; i < s_wallet_count; i++) {
        if (s_wallets[i] && s_wallets[i]->balance() >= amount) {
            return s_wallets[i];
        }
    }
    return s_wallet_count > 0 ? s_wallets[0] : nullptr;
}

static std::vector<cashu::Proof> &mutable_proofs(cashu::Wallet *w)
{
    return const_cast<std::vector<cashu::Proof> &>(w->proofs());
}

static esp_err_t init_wallet(int slot, const char *mint_url)
{
    if (slot >= MAX_WALLETS) return ESP_FAIL;

    s_wallets[slot] = new cashu::Wallet(std::string(mint_url), s_ctx, slot);
    if (!s_wallets[slot]) {
        ESP_LOGE(TAG, "Failed to create wallet for slot %d", slot);
        return ESP_FAIL;
    }
    strncpy(s_wallet_urls[slot], mint_url, sizeof(s_wallet_urls[slot]) - 1);

    s_wallets[slot]->load_from_nvs();

    if (s_wallets[slot]->proofs().empty()) {
        ESP_LOGI(TAG, "Wallet[%d] no proofs loaded (fresh or recovered from corruption)", slot);
    }

    ESP_LOGI(TAG, "Wallet[%d] initialized: url=%s balance=%d proofs=%d (keysets lazy)",
             slot, mint_url, s_wallets[slot]->balance(),
             (int)s_wallets[slot]->proofs().size());
    return ESP_OK;
}

esp_err_t nucula_wallet_init(const char *mint_url)
{
    if (s_wallet_count > 0) return ESP_OK;

    if (!s_wallet_mutex) {
        s_wallet_mutex = xSemaphoreCreateMutex();
        if (!s_wallet_mutex) {
            ESP_LOGE(TAG, "Failed to create wallet mutex");
            return ESP_FAIL;
        }
    }

    if (!s_ctx) {
        s_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        if (!s_ctx) {
            ESP_LOGE(TAG, "Failed to create secp256k1 context");
            return ESP_FAIL;
        }
    }

    esp_err_t ret = init_wallet(0, mint_url);
    if (ret == ESP_OK) s_wallet_count = 1;
    return ret;
}

esp_err_t nucula_wallet_init_multi(const char mint_urls[][256], int count)
{
    if (s_wallet_count > 0) return ESP_OK;
    if (count > MAX_WALLETS) count = MAX_WALLETS;

    if (!s_wallet_mutex) {
        s_wallet_mutex = xSemaphoreCreateMutex();
        if (!s_wallet_mutex) {
            ESP_LOGE(TAG, "Failed to create wallet mutex");
            return ESP_FAIL;
        }
    }

    if (!s_ctx) {
        s_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        if (!s_ctx) {
            ESP_LOGE(TAG, "Failed to create secp256k1 context");
            return ESP_FAIL;
        }
    }

    int ok = 0;
    for (int i = 0; i < count; i++) {
        if (init_wallet(i, mint_urls[i]) == ESP_OK) {
            ok++;
        }
    }

    s_wallet_count = count;
    ESP_LOGI(TAG, "Multi-wallet initialized: %d/%d wallets", ok, count);
    return ok > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t nucula_wallet_receive(const char *token_str)
{
    if (s_wallet_count == 0 || !token_str) return ESP_FAIL;
    WalletLock lock;

    cashu::Token tok;
    bool decoded = false;

    if (strncmp(token_str, "cashuA", 6) == 0) {
        decoded = cashu::deserialize_token_v3(token_str, tok);
    }

    if (!decoded) {
        ESP_LOGE(TAG, "Failed to decode token");
        return ESP_FAIL;
    }

    cashu::Wallet *w = find_wallet_for_token(tok);
    if (!w) {
        ESP_LOGE(TAG, "No wallet found for mint: %s", tok.mint.c_str());
        return ESP_FAIL;
    }

    if (!ensure_keysets(wallet_slot(w))) {
        ESP_LOGE(TAG, "Keysets not available for receive");
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    std::vector<cashu::Proof> proofs_out;
    if (!w->receive(tok, proofs_out)) {
        ESP_LOGE(TAG, "Receive failed");
        return ESP_FAIL;
    }

    int total = 0;
    for (const auto &p : proofs_out) total += p.amount;
    ESP_LOGI(TAG, "Received %d sat (%d proofs) via wallet[%s], new balance=%d",
             total, (int)proofs_out.size(), w->mint_url().c_str(), w->balance());
    return ESP_OK;
}

esp_err_t nucula_wallet_send(uint64_t amount_sat, char *token_out, size_t token_out_size)
{
    if (s_wallet_count == 0) return ESP_FAIL;
    WalletLock lock;

    int amount = (int)amount_sat;
    cashu::Wallet *w = find_wallet_for_send(amount);
    if (!w) return ESP_FAIL;

    if (!ensure_keysets(wallet_slot(w))) {
        ESP_LOGE(TAG, "Keysets not available for send");
        return ESP_FAIL;
    }

    std::string wallet_unit = "sat";
    for (const auto &ks : w->keysets()) {
        if (ks.active) {
            wallet_unit = ks.unit;
            break;
        }
    }

    std::vector<cashu::Proof> selected, remaining;
    if (!w->select_proofs(amount, selected, remaining)) {
        ESP_LOGE(TAG, "Insufficient balance for %d %s", amount, wallet_unit.c_str());
        return ESP_FAIL;
    }

    std::vector<cashu::Proof> new_proofs, change;
    if (!w->swap(selected, (int)amount_sat, new_proofs, change, wallet_unit)) {
        ESP_LOGE(TAG, "Swap for send failed");
        return ESP_FAIL;
    }

    cashu::Token token;
    token.mint = w->mint_url();
    token.unit = wallet_unit;
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

    auto &proofs = mutable_proofs(w);
    proofs = remaining;
    for (auto &p : change) proofs.push_back(p);
    w->save_proofs();

    ESP_LOGI(TAG, "Sent %llu sat via wallet[%s], token=%zu bytes, remaining balance=%d",
             (unsigned long long)amount_sat, w->mint_url().c_str(),
             encoded.size(), w->balance());
    return ESP_OK;
}

uint64_t nucula_wallet_balance(void)
{
    WalletLock lock;
    uint64_t total = 0;
    for (int i = 0; i < s_wallet_count; i++) {
        if (s_wallets[i]) total += (uint64_t)s_wallets[i]->balance();
    }
    return total;
}

int nucula_wallet_proof_count(void)
{
    WalletLock lock;
    int total = 0;
    for (int i = 0; i < s_wallet_count; i++) {
        if (s_wallets[i]) total += (int)s_wallets[i]->proofs().size();
    }
    return total;
}

const char *nucula_wallet_unit(void)
{
    WalletLock lock;
    static char s_unit[16] = "sat";
    for (int i = 0; i < s_wallet_count; i++) {
        if (!s_wallets[i]) continue;
        for (const auto &ks : s_wallets[i]->keysets()) {
            if (ks.active) {
                strncpy(s_unit, ks.unit.c_str(), sizeof(s_unit) - 1);
                s_unit[sizeof(s_unit) - 1] = '\0';
                return s_unit;
            }
        }
    }
    return s_unit;
}

char *nucula_wallet_proofs_json(void)
{
    WalletLock lock;
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_wallet_count; i++) {
        if (!s_wallets[i]) continue;
        const auto &proofs = s_wallets[i]->proofs();
        for (const auto &p : proofs) {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "amount", p.amount);
            cJSON_AddStringToObject(obj, "id", p.id.c_str());
            cJSON_AddStringToObject(obj, "mint", s_wallet_urls[i]);
            cJSON_AddItemToArray(arr, obj);
        }
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

esp_err_t nucula_wallet_swap_all(void)
{
    if (s_wallet_count == 0) return ESP_FAIL;
    WalletLock lock;

    bool any_ok = false;
    for (int i = 0; i < s_wallet_count; i++) {
        if (!s_wallets[i]) continue;

        auto &proofs = mutable_proofs(s_wallets[i]);
        if (proofs.empty()) continue;

        if (!ensure_keysets(i)) {
            ESP_LOGE(TAG, "Keysets not available for swap_all wallet[%d]", i);
            continue;
        }

        int old_balance = s_wallets[i]->balance();

        std::vector<cashu::Proof> inputs = proofs;
        std::vector<cashu::Proof> new_proofs, change;
        if (!s_wallets[i]->swap(inputs, -1, new_proofs, change)) {
            ESP_LOGE(TAG, "Swap failed for wallet[%d]", i);
            continue;
        }

        proofs.clear();
        for (auto &p : new_proofs) proofs.push_back(p);
        for (auto &p : change) proofs.push_back(p);
        s_wallets[i]->save_proofs();

        ESP_LOGI(TAG, "Swap wallet[%d]: %d -> %d sat (%d proofs)",
                 i, old_balance, s_wallets[i]->balance(), (int)proofs.size());
        any_ok = true;
    }

    return any_ok ? ESP_OK : ESP_FAIL;
}

void nucula_wallet_print_status(void)
{
    WalletLock lock;
    if (s_wallet_count == 0) {
        ESP_LOGI(TAG, "No wallets initialized");
        return;
    }
    for (int i = 0; i < s_wallet_count; i++) {
        if (!s_wallets[i]) continue;
        ESP_LOGI(TAG, "Wallet[%d] %s: balance=%d proofs=%d keysets=%d",
                 i, s_wallet_urls[i],
                 s_wallets[i]->balance(), (int)s_wallets[i]->proofs().size(),
                 (int)s_wallets[i]->keysets().size());
        const auto &proofs = s_wallets[i]->proofs();
        for (size_t j = 0; j < proofs.size() && j < 10; j++) {
            ESP_LOGI(TAG, "  [%d][%d] amount=%d id=%s", (int)i, (int)j,
                     proofs[j].amount, proofs[j].id.c_str());
        }
    }
}

esp_err_t nucula_wallet_melt(const char *bolt11_invoice, uint64_t max_fee_sats)
{
    if (s_wallet_count == 0 || !bolt11_invoice) return ESP_FAIL;
    WalletLock lock;

    cashu::Wallet *w = nullptr;
    for (int i = 0; i < s_wallet_count; i++) {
        if (s_wallets[i] && s_wallets[i]->balance() > 0) {
            w = s_wallets[i];
            break;
        }
    }
    if (!w) return ESP_FAIL;

    if (!ensure_keysets(wallet_slot(w))) {
        ESP_LOGE(TAG, "Keysets not available for melt");
        return ESP_FAIL;
    }

    cashu::MeltQuote quote;
    if (!w->request_melt_quote(std::string(bolt11_invoice), quote)) {
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

    int balance_before = w->balance();
    if (balance_before < quote.amount) {
        ESP_LOGE(TAG, "Insufficient balance: %d < %d", balance_before, quote.amount);
        return ESP_FAIL;
    }

    int change_amount = 0;
    if (!w->melt_tokens(quote, change_amount)) {
        ESP_LOGE(TAG, "Melt tokens failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Melted via wallet[%s]: %d sats paid, %d change, balance=%d->%d",
             w->mint_url().c_str(), quote.amount, change_amount,
             balance_before, w->balance());
    return ESP_OK;
}
