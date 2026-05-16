#include "wallet.h"
#include "wallet_persist.h"
#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wallet";
static wallet_t s_wallet;

static const char DOMAIN_SEPARATOR[] = "Secp256k1_HashToCurve_Cashu_";

static mbedtls_ecp_group s_grp;
static mbedtls_mpi s_order;
static bool s_grp_loaded = false;

static esp_err_t init_ecp_group(void)
{
    if (s_grp_loaded) return ESP_OK;
    mbedtls_ecp_group_init(&s_grp);
    mbedtls_mpi_init(&s_order);
    int ret = mbedtls_ecp_group_load(&s_grp, MBEDTLS_ECP_DP_SECP256K1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to load secp256k1 group: -0x%x", -ret);
        return ESP_FAIL;
    }
    mbedtls_mpi_copy(&s_order, &s_grp.N);
    s_grp_loaded = true;
    return ESP_OK;
}

static void random_bytes(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

static esp_err_t random_scalar(mbedtls_mpi *r)
{
    uint8_t buf[32];
    random_bytes(buf, 32);
    mbedtls_mpi_init(r);
    int ret = mbedtls_mpi_read_binary(r, buf, 32);
    if (ret != 0) return ESP_FAIL;
    ret = mbedtls_mpi_mod_mpi(r, r, &s_order);
    if (ret != 0) return ESP_FAIL;
    if (mbedtls_mpi_cmp_int(r, 1) < 0) {
        mbedtls_mpi_add_int(r, r, 1);
    }
    return ESP_OK;
}

static esp_err_t hash_to_curve(const uint8_t *msg, size_t msg_len, mbedtls_ecp_point *Y)
{
    uint8_t msg_hash[32];
    size_t ds_len = strlen(DOMAIN_SEPARATOR);
    uint8_t *hash_input = malloc(ds_len + msg_len);
    if (!hash_input) return ESP_FAIL;
    memcpy(hash_input, DOMAIN_SEPARATOR, ds_len);
    memcpy(hash_input + ds_len, msg, msg_len);
    mbedtls_sha256(hash_input, ds_len + msg_len, msg_hash, 0);
    free(hash_input);

    mbedtls_ecp_point_init(Y);
    for (uint32_t counter = 0; counter < 256; counter++) {
        uint8_t counter_bytes[4];
        counter_bytes[0] = counter & 0xFF;
        counter_bytes[1] = (counter >> 8) & 0xFF;
        counter_bytes[2] = (counter >> 16) & 0xFF;
        counter_bytes[3] = (counter >> 24) & 0xFF;

        uint8_t to_hash[32 + 4 + 1];
        memcpy(to_hash, msg_hash, 32);
        memcpy(to_hash + 32, counter_bytes, 4);

        uint8_t point_hash[32];
        mbedtls_sha256(to_hash, 36, point_hash, 0);

        uint8_t compressed[33];
        compressed[0] = 0x02;
        memcpy(compressed + 1, point_hash, 32);

        int ret = mbedtls_ecp_point_read_binary(&s_grp, Y, compressed, 33);
        if (ret == 0) {
            ret = mbedtls_ecp_check_pubkey(&s_grp, Y);
            if (ret == 0) return ESP_OK;
        }

        compressed[0] = 0x03;
        ret = mbedtls_ecp_point_read_binary(&s_grp, Y, compressed, 33);
        if (ret == 0) {
            ret = mbedtls_ecp_check_pubkey(&s_grp, Y);
            if (ret == 0) return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "hash_to_curve failed after 256 attempts");
    return ESP_FAIL;
}

static esp_err_t point_add(const mbedtls_ecp_point *A, const mbedtls_ecp_point *B,
                             mbedtls_ecp_point *R)
{
    mbedtls_mpi one;
    mbedtls_mpi_init(&one);
    mbedtls_mpi_lset(&one, 1);
    int ret = mbedtls_ecp_muladd(&s_grp, R, &one, A, &one, B);
    if (ret != 0) {
        ESP_LOGE(TAG, "point_add failed: -0x%x", -ret);
    }
    mbedtls_mpi_free(&one);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t scalar_mul(const mbedtls_mpi *m, const mbedtls_ecp_point *P,
                              mbedtls_ecp_point *R)
{
    int ret = mbedtls_ecp_mul(&s_grp, R, m, P, NULL, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "scalar_mul failed: -0x%x", -ret);
    }
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t bytes_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len / 2 > bytes_len) return -1;
    for (size_t i = 0; i < hex_len / 2; i++) {
        unsigned int b;
        sscanf(hex + i * 2, "%02x", &b);
        bytes[i] = (uint8_t)b;
    }
    return hex_len / 2;
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02x", bytes[i]);
    }
    hex[len * 2] = '\0';
}

esp_err_t wallet_init(void)
{
    memset(&s_wallet, 0, sizeof(s_wallet));
    esp_err_t err = init_ecp_group();
    if (err != ESP_OK) return err;
    wallet_persist_load();
    ESP_LOGI(TAG, "Wallet initialized (secp256k1 loaded)");
    return ESP_OK;
}

wallet_t *wallet_get(void)
{
    return &s_wallet;
}

uint64_t wallet_balance(void)
{
    return s_wallet.balance;
}

esp_err_t wallet_add_proofs(const wallet_proof_t *proofs, int count)
{
    for (int i = 0; i < count; i++) {
        if (s_wallet.proof_count >= WALLET_MAX_PROOFS) {
            ESP_LOGW(TAG, "Wallet full, cannot add more proofs");
            return ESP_ERR_NO_MEM;
        }
        memcpy(&s_wallet.proofs[s_wallet.proof_count], &proofs[i], sizeof(wallet_proof_t));
        s_wallet.balance += proofs[i].amount;
        s_wallet.proof_count++;
        ESP_LOGI(TAG, "Added proof: amount=%llu, total_balance=%llu",
                 (unsigned long long)proofs[i].amount,
                 (unsigned long long)s_wallet.balance);
    }
    wallet_persist_save();
    return ESP_OK;
}

esp_err_t wallet_remove_proof(int index)
{
    if (index < 0 || index >= s_wallet.proof_count) return ESP_ERR_INVALID_ARG;
    s_wallet.balance -= s_wallet.proofs[index].amount;
    for (int i = index; i < s_wallet.proof_count - 1; i++) {
        memcpy(&s_wallet.proofs[i], &s_wallet.proofs[i + 1], sizeof(wallet_proof_t));
    }
    memset(&s_wallet.proofs[s_wallet.proof_count - 1], 0, sizeof(wallet_proof_t));
    s_wallet.proof_count--;
    wallet_persist_save();
    return ESP_OK;
}

void wallet_clear(void)
{
    s_wallet.balance = 0;
    s_wallet.proof_count = 0;
    wallet_persist_save();
}

esp_err_t wallet_fetch_keysets(const char *mint_url)
{
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/keysets", mint_url);

    char *resp_buf = malloc(8192);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(resp_buf); return ESP_FAIL; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Keyset fetch open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp_buf);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Keyset fetch: status=%d content_length=%d", status, content_length);

    int resp_len = esp_http_client_read(client, resp_buf, 8191);
    ESP_LOGI(TAG, "Keyset fetch: read %d bytes", resp_len);
    esp_http_client_cleanup(client);

    if (status != 200 || resp_len <= 0) {
        ESP_LOGE(TAG, "Keyset fetch failed: status=%d len=%d", status, resp_len);
        free(resp_buf);
        return ESP_FAIL;
    }
    resp_buf[resp_len] = '\0';

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) return ESP_FAIL;

    cJSON *keysets = cJSON_GetObjectItemCaseSensitive(root, "keysets");
    if (!keysets || !cJSON_IsArray(keysets)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    s_wallet.keyset_count = 0;
    int n = cJSON_GetArraySize(keysets);
    for (int i = 0; i < n && i < WALLET_MAX_KEYSETS; i++) {
        cJSON *ks = cJSON_GetArrayItem(keysets, i);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(ks, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(s_wallet.keysets[s_wallet.keyset_count].id, id->valuestring,
                    WALLET_KEYSET_ID_LEN - 1);
            cJSON *fee = cJSON_GetObjectItemCaseSensitive(ks, "input_fee_ppk");
            s_wallet.keysets[s_wallet.keyset_count].input_fee_ppk = fee ? fee->valueint : 0;
            s_wallet.keyset_count++;
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Fetched %d keysets from %s", s_wallet.keyset_count, mint_url);
    return ESP_OK;
}

esp_err_t wallet_swap_proofs(const char *mint_url, int start_index, int count)
{
    ESP_LOGI(TAG, "wallet_swap_proofs called: start=%d count=%d keysets=%d proofs=%d",
             start_index, count, s_wallet.keyset_count, s_wallet.proof_count);

    if (s_wallet.keyset_count == 0) {
        ESP_LOGE(TAG, "No keysets loaded, fetch first");
        return ESP_FAIL;
    }
    if (start_index < 0 || start_index + count > s_wallet.proof_count) {
        return ESP_ERR_INVALID_ARG;
    }

    wallet_proof_t *old_proofs = &s_wallet.proofs[start_index];
    int n = count;

    uint64_t total_input = 0;
    for (int i = 0; i < n; i++) total_input += old_proofs[i].amount;

    int fee_ppk = s_wallet.keysets[0].input_fee_ppk;
    uint64_t fee_sats = (total_input * fee_ppk + 999) / 1000;
    uint64_t total_output = total_input - fee_sats;
    ESP_LOGI(TAG, "Swap: total_input=%llu fee_ppk=%d fee=%llu total_output=%llu",
             (unsigned long long)total_input, fee_ppk,
             (unsigned long long)fee_sats, (unsigned long long)total_output);

    cJSON *inputs = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "amount", (double)old_proofs[i].amount);
        cJSON_AddStringToObject(p, "id", old_proofs[i].id);
        cJSON_AddStringToObject(p, "secret", old_proofs[i].secret);
        cJSON_AddStringToObject(p, "C", old_proofs[i].c);
        cJSON_AddItemToArray(inputs, p);
    }

    typedef struct {
        uint8_t secret[32];
        mbedtls_mpi r;
        mbedtls_ecp_point Y;
    } swap_output_t;

    swap_output_t *outputs = heap_caps_malloc(n * sizeof(swap_output_t), MALLOC_CAP_SPIRAM);
    if (!outputs) { cJSON_Delete(inputs); return ESP_ERR_NO_MEM; }

    cJSON *blinded_msgs = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        random_bytes(outputs[i].secret, 32);
        mbedtls_ecp_point_init(&outputs[i].Y);
        esp_err_t htc_ret = hash_to_curve(outputs[i].secret, 32, &outputs[i].Y);
        if (htc_ret != ESP_OK) {
            ESP_LOGE(TAG, "hash_to_curve failed for output %d", i);
        }
        mbedtls_mpi_init(&outputs[i].r);
        random_scalar(&outputs[i].r);

        mbedtls_ecp_point rG, B_;
        mbedtls_ecp_point_init(&rG);
        mbedtls_ecp_point_init(&B_);

        esp_err_t sm_ret = scalar_mul(&outputs[i].r, &s_grp.G, &rG);
        if (sm_ret != ESP_OK) {
            ESP_LOGE(TAG, "scalar_mul failed for output %d", i);
        }
        esp_err_t pa_ret = point_add(&outputs[i].Y, &rG, &B_);
        if (pa_ret != ESP_OK) {
            ESP_LOGE(TAG, "point_add failed for output %d", i);
        }

        uint8_t b_bytes[33];
        size_t olen = 0;
        int wret = mbedtls_ecp_point_write_binary(&s_grp, &B_, MBEDTLS_ECP_PF_COMPRESSED, &olen, b_bytes, 33);
        if (wret != 0 || olen == 0) {
            ESP_LOGE(TAG, "Blinded point write failed: ret=-0x%x olen=%zu", -wret, olen);
            olen = 1;
            b_bytes[0] = 0x00;
        }
        char b_hex[67];
        bytes_to_hex(b_bytes, olen, b_hex);

        uint64_t out_amount = old_proofs[i].amount;
        if (i == n - 1) {
            uint64_t running = 0;
            for (int j = 0; j < n - 1; j++) running += old_proofs[j].amount;
            out_amount = total_output - running;
        }

        cJSON *bm = cJSON_CreateObject();
        cJSON_AddNumberToObject(bm, "amount", (double)out_amount);
        cJSON_AddStringToObject(bm, "id", s_wallet.keysets[0].id);
        cJSON_AddStringToObject(bm, "B_", b_hex);
        cJSON_AddItemToArray(blinded_msgs, bm);

        mbedtls_ecp_point_free(&rG);
        mbedtls_ecp_point_free(&B_);
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "inputs", inputs);
    cJSON_AddItemToObject(body, "outputs", blinded_msgs);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    ESP_LOGI(TAG, "Swap request body (%zu bytes): %s", strlen(body_str), body_str);

    char url[512];
    snprintf(url, sizeof(url), "%s/v1/swap", mint_url);

    char *resp_buf = malloc(8192);
    if (!resp_buf) {
        free(body_str);
        for (int i = 0; i < n; i++) {
            mbedtls_mpi_free(&outputs[i].r);
            mbedtls_ecp_point_free(&outputs[i].Y);
        }
        free(outputs);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body_str);
        free(resp_buf);
        for (int i = 0; i < n; i++) {
            mbedtls_mpi_free(&outputs[i].r);
            mbedtls_ecp_point_free(&outputs[i].Y);
        }
        free(outputs);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_open(client, strlen(body_str));
    esp_http_client_write(client, body_str, strlen(body_str));
    free(body_str);

    esp_http_client_fetch_headers(client);
    int resp_len = esp_http_client_read(client, resp_buf, 8191);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status != 200 || resp_len <= 0) {
        if (resp_len > 0) {
            resp_buf[resp_len] = '\0';
            ESP_LOGE(TAG, "Swap failed: status=%d body=%s", status, resp_buf);
        } else {
            ESP_LOGE(TAG, "Swap failed: status=%d len=%d", status, resp_len);
        }
        free(resp_buf);
        for (int i = 0; i < n; i++) {
            mbedtls_mpi_free(&outputs[i].r);
            mbedtls_ecp_point_free(&outputs[i].Y);
        }
        free(outputs);
        return ESP_FAIL;
    }
    resp_buf[resp_len] = '\0';

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) {
        for (int i = 0; i < n; i++) {
            mbedtls_mpi_free(&outputs[i].r);
            mbedtls_ecp_point_free(&outputs[i].Y);
        }
        free(outputs);
        return ESP_FAIL;
    }

    cJSON *signatures = cJSON_GetObjectItemCaseSensitive(root, "signatures");
    if (!signatures || !cJSON_IsArray(signatures)) {
        ESP_LOGE(TAG, "No signatures in swap response");
        cJSON_Delete(root);
        for (int i = 0; i < n; i++) {
            mbedtls_mpi_free(&outputs[i].r);
            mbedtls_ecp_point_free(&outputs[i].Y);
        }
        free(outputs);
        return ESP_FAIL;
    }

    for (int i = start_index; i < start_index + n; i++) {
        s_wallet.balance -= s_wallet.proofs[i].amount;
    }

    int sig_count = cJSON_GetArraySize(signatures);
    for (int i = 0; i < sig_count && i < n; i++) {
        cJSON *sig = cJSON_GetArrayItem(signatures, i);
        cJSON *c_ = cJSON_GetObjectItemCaseSensitive(sig, "C_");
        cJSON *amt = cJSON_GetObjectItemCaseSensitive(sig, "amount");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(sig, "id");

        if (!c_ || !cJSON_IsString(c_)) continue;

        uint8_t c_bytes[33];
        int c_len = hex_to_bytes(c_->valuestring, c_bytes, 33);

        mbedtls_ecp_point C_;
        mbedtls_ecp_point_init(&C_);
        mbedtls_ecp_point_read_binary(&s_grp, &C_, c_bytes, c_len);

        char ks_id[WALLET_KEYSET_ID_LEN] = {0};
        if (id && cJSON_IsString(id)) {
            strncpy(ks_id, id->valuestring, WALLET_KEYSET_ID_LEN - 1);
        }

        mbedtls_mpi neg_r;
        mbedtls_mpi_init(&neg_r);
        mbedtls_mpi_sub_mpi(&neg_r, &s_order, &outputs[i].r);

        mbedtls_ecp_point neg_rG;
        mbedtls_ecp_point_init(&neg_rG);
        scalar_mul(&neg_r, &s_grp.G, &neg_rG);

        mbedtls_ecp_point C;
        mbedtls_ecp_point_init(&C);
        point_add(&C_, &neg_rG, &C);

        uint8_t c_final[33];
        size_t c_final_len;
        mbedtls_ecp_point_write_binary(&s_grp, &C, MBEDTLS_ECP_PF_COMPRESSED,
                                        &c_final_len, c_final, 33);

        if (s_wallet.proof_count < WALLET_MAX_PROOFS) {
            wallet_proof_t *wp = &s_wallet.proofs[s_wallet.proof_count];
            if (amt && cJSON_IsNumber(amt)) {
                wp->amount = (uint64_t)amt->valuedouble;
            }
            strncpy(wp->id, ks_id, WALLET_KEYSET_ID_LEN - 1);
            bytes_to_hex(outputs[i].secret, 32, wp->secret);
            bytes_to_hex(c_final, c_final_len, wp->c);
            s_wallet.balance += wp->amount;
            s_wallet.proof_count++;
        }

        mbedtls_mpi_free(&neg_r);
        mbedtls_ecp_point_free(&C_);
        mbedtls_ecp_point_free(&neg_rG);
        mbedtls_ecp_point_free(&C);
    }

    for (int i = 0; i < n; i++) {
        int idx = start_index;
        for (int j = idx; j < s_wallet.proof_count - 1; j++) {
            memcpy(&s_wallet.proofs[j], &s_wallet.proofs[j + 1], sizeof(wallet_proof_t));
        }
        s_wallet.proof_count--;
    }

    for (int i = 0; i < n; i++) {
        mbedtls_mpi_free(&outputs[i].r);
        mbedtls_ecp_point_free(&outputs[i].Y);
    }
    free(outputs);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Swap complete: %d proofs swapped, balance=%llu",
             n, (unsigned long long)s_wallet.balance);
    wallet_persist_save();
    return ESP_OK;
}

esp_err_t wallet_create_token(char *out, size_t out_size, uint64_t amount,
                               const char *mint_url)
{
    if (s_wallet.proof_count == 0 || s_wallet.balance < amount) {
        ESP_LOGE(TAG, "Insufficient balance: have=%llu need=%llu",
                 (unsigned long long)s_wallet.balance, (unsigned long long)amount);
        return ESP_FAIL;
    }

    cJSON *proofs_arr = cJSON_CreateArray();
    uint64_t remaining = amount;
    int indices_to_remove[10];
    int remove_count = 0;

    for (int i = 0; i < s_wallet.proof_count && remaining > 0 && remove_count < 10; i++) {
        if (s_wallet.proofs[i].amount <= remaining) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddNumberToObject(p, "amount", (double)s_wallet.proofs[i].amount);
            cJSON_AddStringToObject(p, "id", s_wallet.proofs[i].id);
            cJSON_AddStringToObject(p, "secret", s_wallet.proofs[i].secret);
            cJSON_AddStringToObject(p, "C", s_wallet.proofs[i].c);
            cJSON_AddItemToArray(proofs_arr, p);
            remaining -= s_wallet.proofs[i].amount;
            indices_to_remove[remove_count++] = i;
        }
    }

    if (remaining > 0) {
        cJSON_Delete(proofs_arr);
        ESP_LOGE(TAG, "Cannot make exact amount: %llu remaining", (unsigned long long)remaining);
        return ESP_FAIL;
    }

    cJSON *token_obj = cJSON_CreateObject();
    cJSON *token_arr = cJSON_CreateArray();
    cJSON *mint_proofs = cJSON_CreateObject();
    cJSON_AddStringToObject(mint_proofs, "mint", mint_url);
    cJSON_AddItemToObject(mint_proofs, "proofs", proofs_arr);
    cJSON_AddItemToArray(token_arr, mint_proofs);
    cJSON_AddItemToObject(token_obj, "token", token_arr);

    char *json_str = cJSON_PrintUnformatted(token_obj);
    cJSON_Delete(token_obj);

    size_t b64_len;
    mbedtls_base64_encode((unsigned char *)out + 6, out_size - 6, &b64_len,
                          (const unsigned char *)json_str, strlen(json_str));
    free(json_str);

    memcpy(out, "cashuA", 6);
    for (size_t i = 0; i < b64_len; i++) {
        if (out[6 + i] == '+') out[6 + i] = '-';
        else if (out[6 + i] == '/') out[6 + i] = '_';
        else if (out[6 + i] == '=') { out[6 + i] = '\0'; break; }
    }
    out[6 + b64_len] = '\0';

    for (int i = remove_count - 1; i >= 0; i--) {
        s_wallet.balance -= s_wallet.proofs[indices_to_remove[i]].amount;
        for (int j = indices_to_remove[i]; j < s_wallet.proof_count - 1; j++) {
            memcpy(&s_wallet.proofs[j], &s_wallet.proofs[j + 1], sizeof(wallet_proof_t));
        }
        s_wallet.proof_count--;
    }

    ESP_LOGI(TAG, "Created token for %llu sats, remaining balance=%llu",
             (unsigned long long)amount, (unsigned long long)s_wallet.balance);
    wallet_persist_save();
    return ESP_OK;
}

esp_err_t wallet_send(const char *mint_url, uint64_t amount,
                       char *token_out, size_t token_out_size)
{
    return wallet_create_token(token_out, token_out_size, amount, mint_url);
}

void wallet_print_status(void)
{
    ESP_LOGI(TAG, "Wallet: %d proofs, balance=%llu sats, %d keysets",
             s_wallet.proof_count,
             (unsigned long long)s_wallet.balance,
             s_wallet.keyset_count);
    for (int i = 0; i < s_wallet.proof_count; i++) {
        ESP_LOGI(TAG, "  [%d] amount=%llu id=%s", i,
                 (unsigned long long)s_wallet.proofs[i].amount,
                 s_wallet.proofs[i].id);
    }
}
