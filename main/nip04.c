#include "nip04.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "mbedtls/aes.h"
#include <string.h>
#include <stdlib.h>

#include <secp256k1.h>
#include <secp256k1_ecdh.h>

static const char *TAG = "nip04";

static const unsigned char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *src, size_t len, char *dst)
{
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = src[i];
        uint32_t b = (i + 1 < len) ? src[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? src[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        dst[j++] = base64_table[(triple >> 18) & 0x3F];
        dst[j++] = base64_table[(triple >> 12) & 0x3F];
        dst[j++] = (i + 1 < len) ? base64_table[(triple >> 6) & 0x3F] : '=';
        dst[j++] = (i + 2 < len) ? base64_table[triple & 0x3F] : '=';
    }
    dst[j] = '\0';
    return j;
}

static int base64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t base64_decode(const char *src, size_t len, uint8_t *dst)
{
    size_t padding = 0;
    if (len >= 1 && src[len - 1] == '=') padding++;
    if (len >= 2 && src[len - 2] == '=') padding++;
    size_t expected = (len / 4) * 3 - padding;

    size_t j = 0;
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint32_t a = (uint32_t)base64_val(src[i]);
        uint32_t b = (uint32_t)base64_val(src[i + 1]);
        uint32_t c = (src[i + 2] != '=') ? (uint32_t)base64_val(src[i + 2]) : 0;
        uint32_t d = (src[i + 3] != '=') ? (uint32_t)base64_val(src[i + 3]) : 0;
        uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
        if (j < expected) dst[j++] = (triple >> 16) & 0xFF;
        if (j < expected) dst[j++] = (triple >> 8) & 0xFF;
        if (j < expected) dst[j++] = triple & 0xFF;
    }
    return j;
}

static int ecdh_xonly_hash(unsigned char *output, const uint8_t *x32, const uint8_t *y32, void *data)
{
    (void)y32;
    (void)data;
    memcpy(output, x32, 32);
    return 1;
}

static void compute_shared_secret(const uint8_t *privkey, const uint8_t *pubkey32,
                                   uint8_t shared_secret[32])
{
    if (!privkey || !pubkey32) {
        memset(shared_secret, 0, 32);
        return;
    }

    uint8_t compressed[33];
    compressed[0] = 0x02;
    memcpy(compressed + 1, pubkey32, 32);

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_parse(ctx, &pk, compressed, 33)) {
        ESP_LOGE(TAG, "Failed to parse compressed pubkey");
        memset(shared_secret, 0, 32);
        secp256k1_context_destroy(ctx);
        return;
    }

    uint8_t shared[32];
    secp256k1_ecdh(ctx, shared, &pk, privkey, ecdh_xonly_hash, NULL);
    memcpy(shared_secret, shared, 32);

    ESP_LOGI(TAG, "Shared secret: %02x%02x%02x%02x... (pubkey32=%02x%02x%02x%02x...)",
             shared[0], shared[1], shared[2], shared[3],
             pubkey32[0], pubkey32[1], pubkey32[2], pubkey32[3]);

    secp256k1_context_destroy(ctx);
}

static void pkcs7_pad(uint8_t *buf, size_t data_len, size_t block_size, size_t *padded_len)
{
    size_t pad = block_size - (data_len % block_size);
    for (size_t i = 0; i < pad; i++) {
        buf[data_len + i] = (uint8_t)pad;
    }
    *padded_len = data_len + pad;
}

static size_t pkcs7_unpad(const uint8_t *buf, size_t len)
{
    if (len == 0) return 0;
    uint8_t pad = buf[len - 1];
    if (pad == 0 || pad > 16) return 0;
    for (size_t i = len - pad; i < len; i++) {
        if (buf[i] != pad) return 0;
    }
    return len - pad;
}

void nip04_encrypt(const uint8_t *sender_privkey, const uint8_t *recipient_pubkey,
                   const char *plaintext, uint8_t *ciphertext_base64, size_t *out_len)
{
    uint8_t shared_secret[32];
    compute_shared_secret(sender_privkey, recipient_pubkey, shared_secret);

    size_t pt_len = strlen(plaintext);
    uint8_t iv[16];
    esp_fill_random(iv, 16);
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);

    uint8_t padded[4096];
    memcpy(padded, plaintext, pt_len);
    size_t padded_len;
    pkcs7_pad(padded, pt_len, 16, &padded_len);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, shared_secret, 256);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv, padded, padded);
    mbedtls_aes_free(&aes);

    size_t total = 16 + padded_len;
    uint8_t combined[4112];
    memcpy(combined, iv_copy, 16);
    memcpy(combined + 16, padded, padded_len);

    *out_len = base64_encode(combined, total, (char *)ciphertext_base64);
    ((char *)ciphertext_base64)[*out_len] = '\0';

    ESP_LOGD(TAG, "Encrypted %zu bytes -> %zu base64", pt_len, *out_len);
}

int nip04_decrypt(const uint8_t *recipient_privkey, const uint8_t *sender_pubkey,
                  const char *ciphertext_base64, char *plaintext, size_t plaintext_max)
{
    uint8_t shared_secret[32];
    compute_shared_secret(recipient_privkey, sender_pubkey, shared_secret);

    size_t b64_len = strlen(ciphertext_base64);
    uint8_t combined[4112];
    size_t combined_len = base64_decode(ciphertext_base64, b64_len, combined);

    if (combined_len < 32) {
        ESP_LOGE(TAG, "Ciphertext too short: %zu", combined_len);
        return -1;
    }

    uint8_t iv[16];
    memcpy(iv, combined, 16);

    size_t ct_len = combined_len - 16;
    uint8_t ct[4096];
    memcpy(ct, combined + 16, ct_len);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, shared_secret, 256);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ct_len, iv, ct, ct);
    mbedtls_aes_free(&aes);

    size_t pt_len = pkcs7_unpad(ct, ct_len);
    if (pt_len == 0 || pt_len >= plaintext_max) {
        ESP_LOGE(TAG, "Invalid padding: pt_len=%zu ct_len=%zu last_byte=%d padded_bytes:",
                 pt_len, ct_len, ct_len > 0 ? ct[ct_len-1] : -1);
        for (size_t i = ct_len > 4 ? ct_len - 4 : 0; i < ct_len; i++) {
            ESP_LOGE(TAG, "  ct[%zu]=%02x", i, ct[i]);
        }
        return -1;
    }

    memcpy(plaintext, ct, pt_len);
    plaintext[pt_len] = '\0';

    ESP_LOGD(TAG, "Decrypted %zu base64 -> %zu bytes", b64_len, pt_len);
    return (int)pt_len;
}
