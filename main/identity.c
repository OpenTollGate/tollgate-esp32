#include "identity.h"
#include "config.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/md.h"
#include "secp256k1.h"
#include "secp256k1_extrakeys.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "identity";
static tollgate_identity_t s_identity;

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    if (strlen(hex) != out_len * 2) return 0;
    for (size_t i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return 0;
        out[i] = (uint8_t)byte;
    }
    return 1;
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++)
        sprintf(hex + i * 2, "%02x", bytes[i]);
    hex[len * 2] = '\0';
}

static void tollgate_derive(const uint8_t nsec[32], const char *label,
                            uint32_t index, uint8_t *out, size_t out_len)
{
    size_t label_len = strlen(label);
    size_t msg_len = label_len + 4;
    uint8_t *msg = (uint8_t *)malloc(msg_len);
    memcpy(msg, label, label_len);
    msg[label_len]     = (uint8_t)(index & 0xff);
    msg[label_len + 1] = (uint8_t)((index >> 8) & 0xff);
    msg[label_len + 2] = (uint8_t)((index >> 16) & 0xff);
    msg[label_len + 3] = (uint8_t)((index >> 24) & 0xff);

    uint8_t hmac[64];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),
                    nsec, 32, msg, msg_len, hmac);
    free(msg);

    memcpy(out, hmac, out_len);
}

esp_err_t identity_init(const char *nsec_hex)
{
    memset(&s_identity, 0, sizeof(s_identity));

    if (!nsec_hex || strlen(nsec_hex) != 64) {
        ESP_LOGE(TAG, "Invalid nsec: must be 64 hex chars");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_identity.nsec_hex, nsec_hex, sizeof(s_identity.nsec_hex) - 1);

    if (!hex_to_bytes(nsec_hex, s_identity.nsec, 32)) {
        ESP_LOGE(TAG, "Failed to parse nsec hex");
        return ESP_ERR_INVALID_ARG;
    }

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to create secp256k1 context");
        return ESP_ERR_NO_MEM;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, s_identity.nsec)) {
        ESP_LOGE(TAG, "Invalid nsec: secp256k1 key creation failed");
        secp256k1_context_destroy(ctx);
        return ESP_ERR_INVALID_ARG;
    }

    secp256k1_xonly_pubkey xonly;
    secp256k1_xonly_pubkey_from_pubkey(ctx, &xonly, NULL, &pubkey);
    uint8_t npub_bytes[32];
    secp256k1_xonly_pubkey_serialize(ctx, npub_bytes, &xonly);
    bytes_to_hex(npub_bytes, 32, s_identity.npub_hex);

    tollgate_derive(s_identity.nsec, "sta-mac", 0, s_identity.sta_mac, 6);
    s_identity.sta_mac[0] = (s_identity.sta_mac[0] | 0x02) & 0xFE;

    tollgate_derive(s_identity.nsec, "ap-mac", 0, s_identity.ap_mac, 6);
    s_identity.ap_mac[0] = (s_identity.ap_mac[0] | 0x02) & 0xFE;

    snprintf(s_identity.ap_ssid, sizeof(s_identity.ap_ssid),
             "TollGate-%02X%02X%02X",
             s_identity.ap_mac[3], s_identity.ap_mac[4], s_identity.ap_mac[5]);

    uint8_t b3 = s_identity.ap_mac[3];
    uint8_t b4 = s_identity.ap_mac[4];
    uint8_t b5 = s_identity.ap_mac[5];
    uint8_t subnet = (b4 ^ b5) % 200 + 10;
    IP4_ADDR(&s_identity.ap_ip, 10, b3, subnet, 1);
    snprintf(s_identity.ap_ip_str, sizeof(s_identity.ap_ip_str),
             IPSTR, IP2STR(&s_identity.ap_ip));

    tollgate_derive(s_identity.nsec, "tollgate-cashu-locking-key", 0,
                    s_identity.locking_privkey, 32);

    secp256k1_pubkey locking_pub;
    if (secp256k1_ec_pubkey_create(ctx, &locking_pub, s_identity.locking_privkey)) {
        size_t olen = 33;
        secp256k1_ec_pubkey_serialize(ctx, s_identity.locking_pubkey, &olen,
                                       &locking_pub, SECP256K1_EC_COMPRESSED);
        bytes_to_hex(s_identity.locking_pubkey, 33, s_identity.locking_pubkey_hex);
    } else {
        ESP_LOGE(TAG, "Failed to create locking pubkey from derived key");
        memset(s_identity.locking_pubkey, 0, 33);
        s_identity.locking_pubkey_hex[0] = '\0';
    }

    secp256k1_context_destroy(ctx);
    s_identity.initialized = true;

    ESP_LOGI(TAG, "Identity: npub=%s", s_identity.npub_hex);
    ESP_LOGI(TAG, "  STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_identity.sta_mac[0], s_identity.sta_mac[1], s_identity.sta_mac[2],
             s_identity.sta_mac[3], s_identity.sta_mac[4], s_identity.sta_mac[5]);
    ESP_LOGI(TAG, "  AP  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             s_identity.ap_mac[0], s_identity.ap_mac[1], s_identity.ap_mac[2],
             s_identity.ap_mac[3], s_identity.ap_mac[4], s_identity.ap_mac[5]);
    ESP_LOGI(TAG, "  SSID: %s, AP IP: %s", s_identity.ap_ssid, s_identity.ap_ip_str);
    ESP_LOGI(TAG, "  Locking pubkey: %s", s_identity.locking_pubkey_hex);

    return ESP_OK;
}

const tollgate_identity_t *identity_get(void)
{
    return &s_identity;
}
