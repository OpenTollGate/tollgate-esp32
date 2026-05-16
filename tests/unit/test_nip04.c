#include "test_framework.h"
#include "../../main/nip04.h"
#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#include <secp256k1_extrakeys.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static void test_nip04_roundtrip(void)
{
    printf("\n=== NIP-04 encrypt/decrypt roundtrip ===\n");

    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

    uint8_t alice_sec[32], bob_sec[32];
    memset(alice_sec, 0x01, 32);
    memset(bob_sec, 0x02, 32);

    secp256k1_pubkey alice_pk, bob_pk;
    secp256k1_ec_pubkey_create(ctx, &alice_pk, alice_sec);
    secp256k1_ec_pubkey_create(ctx, &bob_pk, bob_sec);

    uint8_t alice_xonly[32], bob_xonly[32];
    secp256k1_xonly_pubkey alice_xpk, bob_xpk;
    secp256k1_xonly_pubkey_from_pubkey(ctx, &alice_xpk, NULL, &alice_pk);
    secp256k1_xonly_pubkey_from_pubkey(ctx, &bob_xpk, NULL, &bob_pk);
    secp256k1_xonly_pubkey_serialize(ctx, alice_xonly, &alice_xpk);
    secp256k1_xonly_pubkey_serialize(ctx, bob_xonly, &bob_xpk);

    const char *message = "Hello, ContextVM! This is a test message.";

    uint8_t ciphertext[4096];
    size_t ct_len = 0;
    nip04_encrypt(alice_sec, bob_xonly, message, ciphertext, &ct_len);
    ASSERT(ct_len > 0, "encryption produced output");
    ASSERT(ct_len > strlen(message), "ciphertext longer than plaintext");

    char plaintext[2048];
    int pt_len = nip04_decrypt(bob_sec, alice_xonly, (const char *)ciphertext, plaintext, sizeof(plaintext));
    ASSERT(pt_len > 0, "decryption succeeded");
    ASSERT_EQ_STR(message, plaintext, "decrypted message matches original");

    printf("\n--- Different key produces garbage ---\n");
    uint8_t eve_sec[32];
    memset(eve_sec, 0x03, 32);
    pt_len = nip04_decrypt(eve_sec, alice_xonly, (const char *)ciphertext, plaintext, sizeof(plaintext));
    if (pt_len > 0) {
        ASSERT(strcmp(message, plaintext) != 0, "wrong key produces different output");
    } else {
        ASSERT(true, "wrong key fails to decrypt");
    }

    printf("\n--- Second roundtrip (different message) ---\n");
    const char *msg2 = "Short";
    nip04_encrypt(alice_sec, bob_xonly, msg2, ciphertext, &ct_len);
    pt_len = nip04_decrypt(bob_sec, alice_xonly, (const char *)ciphertext, plaintext, sizeof(plaintext));
    ASSERT(pt_len > 0, "short message decrypts");
    ASSERT_EQ_STR(msg2, plaintext, "short message matches");

    printf("\n--- Long message roundtrip ---\n");
    char long_msg[512];
    memset(long_msg, 'X', 511);
    long_msg[511] = '\0';
    nip04_encrypt(alice_sec, bob_xonly, long_msg, ciphertext, &ct_len);
    pt_len = nip04_decrypt(bob_sec, alice_xonly, (const char *)ciphertext, plaintext, sizeof(plaintext));
    ASSERT(pt_len > 0, "long message decrypts");
    ASSERT_EQ_INT(511, pt_len, "long message length matches");

    printf("\n--- Bob encrypts, Alice decrypts ---\n");
    nip04_encrypt(bob_sec, alice_xonly, message, ciphertext, &ct_len);
    pt_len = nip04_decrypt(alice_sec, bob_xonly, (const char *)ciphertext, plaintext, sizeof(plaintext));
    ASSERT(pt_len > 0, "reverse direction decrypts");
    ASSERT_EQ_STR(message, plaintext, "reverse direction matches");

    secp256k1_context_destroy(ctx);
}

static void test_nip04_invalid_input(void)
{
    printf("\n=== NIP-04 invalid inputs ===\n");
    char plaintext[256];
    int rc = nip04_decrypt(NULL, NULL, "AAAA", plaintext, sizeof(plaintext));
    ASSERT(rc < 0, "NULL keys fails");

    uint8_t dummy_sec[32];
    memset(dummy_sec, 0xAA, 32);
    rc = nip04_decrypt(dummy_sec, NULL, "AAAA", plaintext, sizeof(plaintext));
    ASSERT(rc < 0, "NULL pubkey fails");

    uint8_t dummy_pub[32];
    memset(dummy_pub, 0xBB, 32);
    rc = nip04_decrypt(dummy_sec, dummy_pub, "", plaintext, sizeof(plaintext));
    ASSERT(rc < 0, "empty ciphertext fails");

    rc = nip04_decrypt(dummy_sec, dummy_pub, "AAAA", plaintext, sizeof(plaintext));
    ASSERT(rc < 0, "garbage ciphertext fails");
}

int main(void)
{
    printf("=== test_nip04 ===\n");
    test_nip04_roundtrip();
    test_nip04_invalid_input();
    TEST_SUMMARY();
}
