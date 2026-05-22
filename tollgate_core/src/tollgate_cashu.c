#include "tollgate_cashu.h"
#include "tollgate_core.h"
#include "tollgate_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

static const char *TAG = "tg_cashu";

static const char V3_PREFIX[] = "cashuA";
static const size_t V3_PREFIX_LEN = 6;

static int b64url_decode(const char *input, size_t input_len, char *out, size_t out_size, size_t *out_len)
{
    char *b64 = malloc(input_len + 4);
    if (!b64) return -1;
    size_t b64_len = input_len;
    memcpy(b64, input, b64_len);
    b64[b64_len] = '\0';

    for (size_t i = 0; i < b64_len; i++) {
        if (b64[i] == '-') b64[i] = '+';
        else if (b64[i] == '_') b64[i] = '/';
    }
    while (b64_len % 4 != 0) {
        b64[b64_len++] = '=';
    }
    b64[b64_len] = '\0';

    size_t olen = 0;
    int ret = mbedtls_base64_decode((unsigned char *)out, out_size, &olen,
                                     (const unsigned char *)b64, b64_len);
    free(b64);
    if (ret != 0) return -1;
    *out_len = olen;
    return 0;
}

static int parse_proofs_array(cJSON *arr, tg_cashu_token_t *out)
{
    if (!cJSON_IsArray(arr)) return -1;
    int count = cJSON_GetArraySize(arr);
    if (count > TG_CASHU_MAX_PROOFS) return -1;

    out->proof_count = 0;
    out->total_amount = 0;
    for (int i = 0; i < count; i++) {
        cJSON *proof = cJSON_GetArrayItem(arr, i);
        cJSON *amt = cJSON_GetObjectItemCaseSensitive(proof, "amount");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(proof, "id");
        cJSON *secret = cJSON_GetObjectItemCaseSensitive(proof, "secret");
        cJSON *c = cJSON_GetObjectItemCaseSensitive(proof, "C");

        if (!amt || !cJSON_IsNumber(amt)) return -1;

        out->proofs[i].amount = (uint64_t)amt->valuedouble;
        out->total_amount += out->proofs[i].amount;

        if (id && cJSON_IsString(id)) {
            strncpy(out->proofs[i].id, id->valuestring, sizeof(out->proofs[i].id) - 1);
        }
        if (secret && cJSON_IsString(secret)) {
            strncpy(out->proofs[i].secret, secret->valuestring, sizeof(out->proofs[i].secret) - 1);
        }
        if (c && cJSON_IsString(c)) {
            strncpy(out->proofs[i].c, c->valuestring, sizeof(out->proofs[i].c) - 1);
        }
        out->proof_count++;
    }
    return 0;
}

int tg_cashu_decode_token(const char *token_str, tg_cashu_token_t *out)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();
    if (!token_str || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t len = strlen(token_str);
    char *nl = strchr(token_str, '\n');
    if (nl) len = nl - token_str;
    char *cr = strchr(token_str, '\r');
    if (cr && (cr - token_str) < (int)len) len = cr - token_str;
    if (len <= V3_PREFIX_LEN) {
        if (p && p->log_error) p->log_error(TAG, "Token too short");
        return -1;
    }
    if (strncmp(token_str, V3_PREFIX, V3_PREFIX_LEN) != 0) {
        if (p && p->log_error) p->log_error(TAG, "Token missing cashuA prefix");
        return -1;
    }

    size_t b64_len = len - V3_PREFIX_LEN;
    size_t decoded_size = (b64_len * 3) / 4 + 4;
    char *json_buf = malloc(decoded_size);
    if (!json_buf) return -1;
    size_t json_len = 0;
    if (b64url_decode(token_str + V3_PREFIX_LEN, b64_len,
                      json_buf, decoded_size - 1, &json_len) != 0) {
        if (p && p->log_error) p->log_error(TAG, "Base64url decode failed");
        free(json_buf);
        return -1;
    }
    json_buf[json_len] = '\0';

    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);
    if (!root) {
        if (p && p->log_error) p->log_error(TAG, "JSON parse failed");
        return -1;
    }

    cJSON *token_arr = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (token_arr && cJSON_IsArray(token_arr)) {
        cJSON *first = cJSON_GetArrayItem(token_arr, 0);
        if (!first) { cJSON_Delete(root); return -1; }

        cJSON *mint = cJSON_GetObjectItemCaseSensitive(first, "mint");
        if (mint && cJSON_IsString(mint)) {
            strncpy(out->mint_url, mint->valuestring, sizeof(out->mint_url) - 1);
        }

        cJSON *proofs = cJSON_GetObjectItemCaseSensitive(first, "proofs");
        if (proofs) {
            if (parse_proofs_array(proofs, out) != 0) { cJSON_Delete(root); return -1; }
        }
    } else {
        cJSON *mint = cJSON_GetObjectItemCaseSensitive(root, "mint");
        if (mint && cJSON_IsString(mint)) {
            strncpy(out->mint_url, mint->valuestring, sizeof(out->mint_url) - 1);
        }

        cJSON *proofs = cJSON_GetObjectItemCaseSensitive(root, "proofs");
        if (proofs) {
            if (parse_proofs_array(proofs, out) != 0) { cJSON_Delete(root); return -1; }
        }
    }

    cJSON_Delete(root);

    if (out->proof_count == 0) {
        if (p && p->log_error) p->log_error(TAG, "No proofs in token");
        return -1;
    }

    if (p && p->log_info) p->log_info(TAG, "Decoded token: %d proofs, total=%llu, mint=%s",
             out->proof_count, (unsigned long long)out->total_amount, out->mint_url);
    return 0;
}

static void sha256_hex(const char *data, size_t data_len, char *hex_out)
{
    unsigned char hash[32];
    mbedtls_sha256((const unsigned char *)data, data_len, hash, 0);
    for (int i = 0; i < 32; i++) {
        sprintf(hex_out + i * 2, "%02x", hash[i]);
    }
    hex_out[64] = '\0';
}

int tg_cashu_check_proof_states(const char *mint_url, const tg_cashu_token_t *token,
                                 tg_cashu_proof_state_t *states, int *state_count)
{
    const tollgate_platform_t *p = tollgate_core_get_platform();

    cJSON *ys_arr = cJSON_CreateArray();
    for (int i = 0; i < token->proof_count; i++) {
        char y_hex[65];
        sha256_hex(token->proofs[i].secret, strlen(token->proofs[i].secret), y_hex);
        cJSON_AddItemToArray(ys_arr, cJSON_CreateString(y_hex));
        strncpy(states[i].y_hex, y_hex, sizeof(states[i].y_hex) - 1);
        states[i].spent = false;
    }
    *state_count = token->proof_count;

    char *ys_json = cJSON_PrintUnformatted(ys_arr);
    cJSON_Delete(ys_arr);

    char *post_body = malloc(4096);
    if (!post_body) { cJSON_free(ys_json); return -1; }
    snprintf(post_body, 4096, "{\"Ys\":%s}", ys_json);
    cJSON_free(ys_json);

    char url[512];
    snprintf(url, sizeof(url), "%s/v1/checkstate", mint_url);

    if (!p || !p->http_post) {
        free(post_body);
        return -1;
    }

    char *resp_buf = malloc(8192);
    if (!resp_buf) { free(post_body); return -1; }

    int resp_len = p->http_post(url, "Content-Type: application/json",
                                 post_body, (int)strlen(post_body),
                                 resp_buf, 8191);
    free(post_body);

    if (resp_len <= 0) {
        if (p && p->log_error) p->log_error(TAG, "checkstate HTTP failed: resp_len=%d", resp_len);
        free(resp_buf);
        return -1;
    }
    resp_buf[resp_len] = '\0';

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!root) return -1;

    cJSON *states_arr = cJSON_GetObjectItemCaseSensitive(root, "states");
    if (!states_arr || !cJSON_IsArray(states_arr)) {
        cJSON_Delete(root);
        return -1;
    }

    int n = cJSON_GetArraySize(states_arr);
    for (int i = 0; i < n && i < token->proof_count; i++) {
        cJSON *s = cJSON_GetArrayItem(states_arr, i);
        cJSON *state = cJSON_GetObjectItemCaseSensitive(s, "state");
        if (state && cJSON_IsString(state)) {
            states[i].spent = (strcmp(state->valuestring, "SPENT") == 0);
        }
    }

    cJSON_Delete(root);
    return 0;
}

uint64_t tg_cashu_calculate_allotment_ms(uint64_t token_amount, uint64_t price_per_step,
                                          uint64_t step_size_ms)
{
    if (price_per_step == 0) return 0;
    return (token_amount / price_per_step) * step_size_ms;
}

uint64_t tg_cashu_calculate_allotment(uint64_t token_amount, uint64_t price_per_step,
                                       uint64_t step_size)
{
    if (price_per_step == 0) return 0;
    return (token_amount / price_per_step) * step_size;
}

bool tg_cashu_is_mint_accepted(const char *mint_url, const char *accepted_mint_url)
{
    if (!mint_url || mint_url[0] == '\0') return false;
    if (!accepted_mint_url || accepted_mint_url[0] == '\0') return false;
    return (strstr(mint_url, accepted_mint_url) != NULL ||
            strcmp(mint_url, accepted_mint_url) == 0);
}
