#include "nostr_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "mbedtls/sha256.h"
#include "secp256k1.h"
#include "secp256k1_extrakeys.h"
#include "secp256k1_schnorrsig.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

static const char *TAG = "nostr_event";

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++)
        sprintf(hex + i * 2, "%02x", bytes[i]);
    hex[len * 2] = '\0';
}

esp_err_t nostr_event_init(nostr_event_t *event, const char *npub_hex,
                           int kind, const char *tags_json, const char *content)
{
    memset(event, 0, sizeof(*event));
    strncpy(event->pubkey, npub_hex, sizeof(event->pubkey) - 1);
    event->kind = kind;
    event->tags_json = tags_json ? tags_json : "[]";
    event->content = content ? content : "";

    struct timeval tv;
    gettimeofday(&tv, NULL);
    event->created_at = (uint64_t)tv.tv_sec;

    cJSON *serial = cJSON_CreateArray();
    cJSON_AddItemToArray(serial, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(serial, cJSON_CreateString(event->pubkey));
    cJSON_AddItemToArray(serial, cJSON_CreateNumber((double)event->created_at));
    cJSON_AddItemToArray(serial, cJSON_CreateNumber(event->kind));
    cJSON_AddItemToArray(serial, cJSON_Parse(event->tags_json));
    cJSON_AddItemToArray(serial, cJSON_CreateString(event->content));

    char *serialized = cJSON_PrintUnformatted(serial);
    cJSON_Delete(serial);

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)serialized, strlen(serialized),
                   hash, 0);
    free(serialized);

    bytes_to_hex(hash, 32, event->id);
    return ESP_OK;
}

esp_err_t nostr_event_sign(nostr_event_t *event, const uint8_t nsec[32])
{
    uint8_t msg[32];
    for (size_t i = 0; i < 32; i++) {
        unsigned int byte;
        sscanf(event->id + i * 2, "%02x", &byte);
        msg[i] = (uint8_t)byte;
    }

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to create secp256k1 context");
        return ESP_ERR_NO_MEM;
    }

    secp256k1_keypair keypair;
    if (!secp256k1_keypair_create(ctx, &keypair, nsec)) {
        ESP_LOGE(TAG, "Invalid nsec for signing");
        secp256k1_context_destroy(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t sig[64];
    if (!secp256k1_schnorrsig_sign32(ctx, sig, msg, &keypair, NULL)) {
        ESP_LOGE(TAG, "Schnorr signing failed");
        secp256k1_context_destroy(ctx);
        return ESP_FAIL;
    }

    bytes_to_hex(sig, 64, event->sig);
    secp256k1_context_destroy(ctx);
    return ESP_OK;
}

esp_err_t nostr_event_to_json(const nostr_event_t *event, char *buf, size_t buf_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", event->id);
    cJSON_AddStringToObject(root, "pubkey", event->pubkey);
    cJSON_AddNumberToObject(root, "created_at", (double)event->created_at);
    cJSON_AddNumberToObject(root, "kind", event->kind);
    cJSON_AddItemToObject(root, "tags", cJSON_Parse(event->tags_json));
    cJSON_AddStringToObject(root, "content", event->content);
    cJSON_AddStringToObject(root, "sig", event->sig);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) return ESP_FAIL;
    size_t len = strlen(json);
    if (len >= buf_len) {
        free(json);
        return ESP_ERR_NO_MEM;
    }
    memcpy(buf, json, len + 1);
    free(json);
    return ESP_OK;
}
