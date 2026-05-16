#include "wallet_persist.h"
#include "wallet.h"
#include "config.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static const char *TAG = "wallet_persist";
static const char *WALLET_FILE = "/spiffs/wallet.json";

esp_err_t wallet_persist_save(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    wallet_t *w = wallet_get();

    if (w->balance < cfg->persist_threshold_sats) {
        if (w->proof_count == 0) {
            unlink(WALLET_FILE);
            ESP_LOGI(TAG, "Wallet empty, removed persist file");
        }
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "balance", (double)w->balance);

    cJSON *proofs = cJSON_CreateArray();
    for (int i = 0; i < w->proof_count; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "amount", (double)w->proofs[i].amount);
        cJSON_AddStringToObject(p, "id", w->proofs[i].id);
        cJSON_AddStringToObject(p, "secret", w->proofs[i].secret);
        cJSON_AddStringToObject(p, "C", w->proofs[i].c);
        cJSON_AddItemToArray(proofs, p);
    }
    cJSON_AddItemToObject(root, "proofs", proofs);

    cJSON *keysets = cJSON_CreateArray();
    for (int i = 0; i < w->keyset_count; i++) {
        cJSON *ks = cJSON_CreateObject();
        cJSON_AddStringToObject(ks, "id", w->keysets[i].id);
        cJSON_AddItemToArray(keysets, ks);
    }
    cJSON_AddItemToObject(root, "keysets", keysets);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    FILE *f = fopen(WALLET_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", WALLET_FILE);
        cJSON_free(json_str);
        return ESP_FAIL;
    }

    size_t written = fwrite(json_str, 1, strlen(json_str), f);
    fclose(f);
    cJSON_free(json_str);

    ESP_LOGI(TAG, "Wallet persisted: %d proofs, balance=%llu (%zu bytes)",
             w->proof_count, (unsigned long long)w->balance, written);
    return ESP_OK;
}

esp_err_t wallet_persist_load(void)
{
    wallet_t *w = wallet_get();

    FILE *f = fopen(WALLET_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "No persisted wallet found, starting fresh");
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 65536) {
        fclose(f);
        ESP_LOGW(TAG, "Wallet file size invalid: %ld", fsize);
        return ESP_FAIL;
    }

    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse wallet.json");
        return ESP_FAIL;
    }

    cJSON *balance_j = cJSON_GetObjectItemCaseSensitive(root, "balance");
    if (balance_j && cJSON_IsNumber(balance_j)) {
        w->balance = (uint64_t)balance_j->valuedouble;
    }

    cJSON *proofs = cJSON_GetObjectItemCaseSensitive(root, "proofs");
    if (proofs && cJSON_IsArray(proofs)) {
        int count = cJSON_GetArraySize(proofs);
        if (count > WALLET_MAX_PROOFS) count = WALLET_MAX_PROOFS;
        for (int i = 0; i < count; i++) {
            cJSON *p = cJSON_GetArrayItem(proofs, i);
            cJSON *amt = cJSON_GetObjectItemCaseSensitive(p, "amount");
            cJSON *id = cJSON_GetObjectItemCaseSensitive(p, "id");
            cJSON *secret = cJSON_GetObjectItemCaseSensitive(p, "secret");
            cJSON *c = cJSON_GetObjectItemCaseSensitive(p, "C");
            if (amt) w->proofs[i].amount = (uint64_t)amt->valuedouble;
            if (id && cJSON_IsString(id))
                strncpy(w->proofs[i].id, id->valuestring, WALLET_KEYSET_ID_LEN - 1);
            if (secret && cJSON_IsString(secret))
                strncpy(w->proofs[i].secret, secret->valuestring, WALLET_SECRET_LEN - 1);
            if (c && cJSON_IsString(c))
                strncpy(w->proofs[i].c, c->valuestring, WALLET_SIG_LEN - 1);
            w->proof_count++;
        }
    }

    cJSON *keysets = cJSON_GetObjectItemCaseSensitive(root, "keysets");
    if (keysets && cJSON_IsArray(keysets)) {
        int count = cJSON_GetArraySize(keysets);
        if (count > WALLET_MAX_KEYSETS) count = WALLET_MAX_KEYSETS;
        for (int i = 0; i < count; i++) {
            cJSON *ks = cJSON_GetArrayItem(keysets, i);
            cJSON *id = cJSON_GetObjectItemCaseSensitive(ks, "id");
            if (id && cJSON_IsString(id))
                strncpy(w->keysets[i].id, id->valuestring, WALLET_KEYSET_ID_LEN - 1);
            w->keyset_count++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Wallet loaded: %d proofs, %d keysets, balance=%llu",
             w->proof_count, w->keyset_count, (unsigned long long)w->balance);
    return ESP_OK;
}
