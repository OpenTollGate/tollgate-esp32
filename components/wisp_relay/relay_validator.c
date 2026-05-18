#include "relay_validator.h"
#include "relay_types.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "secp256k1.h"
#include "secp256k1_extrakeys.h"
#include "secp256k1_schnorrsig.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "relay_validator";

static int hex_to_bytes(const char *hex, size_t hex_len, uint8_t *out, size_t out_len)
{
    if (hex_len != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

static char *serialize_event_for_id(const char *event_json, size_t event_len)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, event_len);
    if (!obj) return NULL;

    cJSON *serial = cJSON_CreateArray();
    cJSON_AddItemToArray(serial, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(serial, cJSON_CreateString(
        cJSON_GetObjectItem(obj, "pubkey")->valuestring));
    cJSON_AddItemToArray(serial, cJSON_CreateNumber(
        cJSON_GetObjectItem(obj, "created_at")->valuedouble));
    cJSON_AddItemToArray(serial, cJSON_CreateNumber(
        cJSON_GetObjectItem(obj, "kind")->valueint));
    cJSON *tags = cJSON_GetObjectItem(obj, "tags");
    cJSON_AddItemToArray(serial, cJSON_Duplicate(tags, 1));
    cJSON_AddItemToArray(serial, cJSON_CreateString(
        cJSON_GetObjectItem(obj, "content")->valuestring));

    char *result = cJSON_PrintUnformatted(serial);
    cJSON_Delete(serial);
    cJSON_Delete(obj);
    return result;
}

static bool verify_event_id(const char *event_json, size_t event_len,
                            const uint8_t expected_id[32])
{
    char *serialized = serialize_event_for_id(event_json, event_len);
    if (!serialized) return false;

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)serialized, strlen(serialized), hash, 0);
    free(serialized);

    return memcmp(hash, expected_id, 32) == 0;
}

static bool verify_schnorr_sig(const uint8_t pubkey[32], const uint8_t msg[32],
                               const uint8_t sig[64])
{
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (!ctx) return false;

    secp256k1_xonly_pubkey xonly_pub;
    if (!secp256k1_xonly_pubkey_parse(ctx, &xonly_pub, pubkey)) {
        secp256k1_context_destroy(ctx);
        return false;
    }

    bool valid = secp256k1_schnorrsig_verify(ctx, sig, msg, 32, &xonly_pub);
    secp256k1_context_destroy(ctx);
    return valid;
}

bool relay_validator_verify_event(const char *event_json, size_t event_len)
{
    cJSON *obj = cJSON_ParseWithLength(event_json, event_len);
    if (!obj) {
        ESP_LOGD(TAG, "Invalid JSON");
        return false;
    }

    cJSON *id_item = cJSON_GetObjectItem(obj, "id");
    cJSON *pk_item = cJSON_GetObjectItem(obj, "pubkey");
    cJSON *sig_item = cJSON_GetObjectItem(obj, "sig");

    if (!id_item || !pk_item || !sig_item) {
        cJSON_Delete(obj);
        ESP_LOGD(TAG, "Missing required fields");
        return false;
    }

    const char *id_hex = id_item->valuestring;
    const char *pk_hex = pk_item->valuestring;
    const char *sig_hex = sig_item->valuestring;

    if (strlen(id_hex) != 64 || strlen(pk_hex) != 64 || strlen(sig_hex) != 128) {
        cJSON_Delete(obj);
        ESP_LOGD(TAG, "Invalid field lengths");
        return false;
    }

    uint8_t event_id[32], pubkey[32], sig[64];
    if (hex_to_bytes(id_hex, 64, event_id, 32) != 0 ||
        hex_to_bytes(pk_hex, 64, pubkey, 32) != 0 ||
        hex_to_bytes(sig_hex, 128, sig, 64) != 0) {
        cJSON_Delete(obj);
        ESP_LOGD(TAG, "Invalid hex encoding");
        return false;
    }

    cJSON_Delete(obj);

    if (!verify_event_id(event_json, event_len, event_id)) {
        ESP_LOGD(TAG, "Event ID mismatch");
        return false;
    }

    if (!verify_schnorr_sig(pubkey, event_id, sig)) {
        ESP_LOGD(TAG, "Invalid signature");
        return false;
    }

    return true;
}

validation_result_t relay_validator_check(const uint8_t *id,
                                           const uint8_t *pubkey,
                                           uint64_t created_at,
                                           int kind,
                                           const char *content,
                                           size_t content_len,
                                           const char *tags_json,
                                           const uint8_t *sig,
                                           const validator_config_t *config)
{
    (void)content; (void)content_len; (void)tags_json;

    if (config) {
        if (config->max_future_sec > 0) {
            int64_t now = (int64_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
            if ((int64_t)created_at > now + config->max_future_sec)
                return VALIDATION_ERR_FUTURE;
        }
    }

    if (!verify_schnorr_sig(pubkey, id, sig))
        return VALIDATION_ERR_SIG;

    return VALIDATION_OK;
}

const char *relay_validator_result_string(validation_result_t result)
{
    switch (result) {
        case VALIDATION_OK: return "ok";
        case VALIDATION_ERR_SCHEMA: return "invalid: schema";
        case VALIDATION_ERR_ID: return "invalid: event id";
        case VALIDATION_ERR_SIG: return "invalid: signature";
        case VALIDATION_ERR_EXPIRED: return "invalid: expired";
        case VALIDATION_ERR_FUTURE: return "invalid: future";
        case VALIDATION_ERR_DUPLICATE: return "duplicate";
        case VALIDATION_ERR_POW: return "pow: insufficient";
        case VALIDATION_ERR_BLOCKED: return "blocked";
        case VALIDATION_ERR_TOO_OLD: return "invalid: too old";
        default: return "error: unknown";
    }
}
