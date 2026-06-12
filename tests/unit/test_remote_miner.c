#include "test_framework.h"
#include <mbedtls/sha256.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static void sha256d(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t tmp[32];
    mbedtls_sha256(data, len, tmp, 0);
    mbedtls_sha256(tmp, 32, out, 0);
}

static void nbits_to_target(uint32_t nbits, uint8_t target[32], int *target_len)
{
    *target_len = 32;
    memset(target, 0, 32);

    int exp = (int)((nbits >> 24) & 0xFF);
    uint32_t mantissa = nbits & 0x007FFFFF;

    int pos = 32 - exp;
    if (pos < 0) pos = 0;

    for (int i = 0; i < 3 && (pos + i) < 32; i++) {
        target[pos + i] = (uint8_t)((mantissa >> (8 * (2 - i))) & 0xFF);
    }
}

static void build_header(const uint8_t prevhash[32], const uint8_t merkle_root[32],
                          uint32_t version, uint32_t ntime, uint32_t nbits,
                          uint32_t nonce, uint8_t out[80])
{
    memset(out, 0, 80);
    out[0] = (version >> 0) & 0xFF;
    out[1] = (version >> 8) & 0xFF;
    out[2] = (version >> 16) & 0xFF;
    out[3] = (version >> 24) & 0xFF;
    memcpy(out + 4, prevhash, 32);
    memcpy(out + 36, merkle_root, 32);
    out[68] = (ntime >> 0) & 0xFF;
    out[69] = (ntime >> 8) & 0xFF;
    out[70] = (ntime >> 16) & 0xFF;
    out[71] = (ntime >> 24) & 0xFF;
    out[72] = (nbits >> 0) & 0xFF;
    out[73] = (nbits >> 8) & 0xFF;
    out[74] = (nbits >> 16) & 0xFF;
    out[75] = (nbits >> 24) & 0xFF;
    out[76] = (nonce >> 0) & 0xFF;
    out[77] = (nonce >> 8) & 0xFF;
    out[78] = (nonce >> 16) & 0xFF;
    out[79] = (nonce >> 24) & 0xFF;
}

static bool check_pow(const uint8_t header[80], const uint8_t *target, int target_len)
{
    uint8_t hash[32];
    sha256d(header, 80, hash);

    uint8_t hash_be[32];
    for (int i = 0; i < 32; i++) hash_be[i] = hash[31 - i];

    for (int i = 0; i < target_len && i < 32; i++) {
        if (hash_be[i] < target[i]) return true;
        if (hash_be[i] > target[i]) return false;
    }
    return true;
}

static int hex_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len && hex[i * 2] && hex[i * 2 + 1]; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

static void bytes_to_hex(const uint8_t *data, int len, char *out)
{
    for (int i = 0; i < len; i++) {
        snprintf(out + i * 2, 3, "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

static void test_nbits_genesis(void)
{
    printf("test_nbits_genesis:\n");
    uint8_t target[32];
    int target_len;
    nbits_to_target(0x1d00ffff, target, &target_len);

    char hex[65];
    bytes_to_hex(target, 32, hex);
    printf("  target: %s\n", hex);

    ASSERT_EQ_INT(32, target_len, "target_len is 32");
    ASSERT(target[3] == 0x00, "target[3] == 0x00 (high byte of mantissa)");
    ASSERT(target[4] == 0xFF, "target[4] == 0xFF");
    ASSERT(target[5] == 0xFF, "target[5] == 0xFF (genesis nbits)");
}

static void test_nbits_low_difficulty(void)
{
    printf("test_nbits_low_difficulty:\n");
    uint8_t target[32];
    int target_len;
    nbits_to_target(0x01010000, target, &target_len);

    char hex[65];
    bytes_to_hex(target, 32, hex);

    ASSERT_EQ_INT(32, target_len, "target_len is 32");
    ASSERT(target[31] == 0x01, "target[31] == 0x01 for nbits 0x01010000");
    for (int i = 0; i < 31; i++) {
        ASSERT(target[i] == 0x00, "target[0..30] == 0x00");
        if (target[i] != 0x00) break;
    }
}

static void test_nbits_zero(void)
{
    printf("test_nbits_zero:\n");
    uint8_t target[32];
    int target_len;
    nbits_to_target(0x00000000, target, &target_len);

    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (target[i] != 0) { all_zero = 0; break; }
    }
    ASSERT(all_zero, "nbits=0 produces all-zero target (impossible difficulty)");
}

static void test_sha256d_known_vector(void)
{
    printf("test_sha256d_known_vector:\n");
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t hash[32];
    sha256d(data, 4, hash);

    uint8_t hash_inner[32];
    mbedtls_sha256(data, 4, hash_inner, 0);
    uint8_t expected[32];
    mbedtls_sha256(hash_inner, 32, expected, 0);

    ASSERT_MEM_EQ(expected, hash, 32, "sha256d matches double SHA-256");
}

static void test_build_header_layout(void)
{
    printf("test_build_header_layout:\n");
    uint8_t prevhash[32];
    memset(prevhash, 0xAA, 32);
    uint8_t merkle[32];
    memset(merkle, 0xBB, 32);
    uint32_t version = 0x20000000;
    uint32_t ntime = 0x6482AB00;
    uint32_t nbits = 0x17034219;
    uint32_t nonce = 0x12345678;

    uint8_t header[80];
    build_header(prevhash, merkle, version, ntime, nbits, nonce, header);

    ASSERT(header[0] == 0x00 && header[1] == 0x00 && header[2] == 0x00 && header[3] == 0x20,
           "version bytes little-endian");
    ASSERT_MEM_EQ(prevhash, header + 4, 32, "prevhash at offset 4");
    ASSERT_MEM_EQ(merkle, header + 36, 32, "merkle_root at offset 36");
    ASSERT(header[68] == 0x00 && header[69] == 0xAB && header[70] == 0x82 && header[71] == 0x64,
           "ntime bytes little-endian");
    ASSERT(header[72] == 0x19 && header[73] == 0x42 && header[74] == 0x03 && header[75] == 0x17,
           "nbits bytes little-endian");
    ASSERT(header[76] == 0x78 && header[77] == 0x56 && header[78] == 0x34 && header[79] == 0x12,
           "nonce bytes little-endian");
}

static void test_check_pow_all_ones_target(void)
{
    printf("test_check_pow_all_ones_target:\n");
    uint8_t target[32];
    memset(target, 0xFF, 32);
    uint8_t header[80];
    memset(header, 0, 80);

    bool result = check_pow(header, target, 32);
    ASSERT(result, "all-0xFF target accepts any hash");
}

static void test_check_pow_zero_target(void)
{
    printf("test_check_pow_zero_target:\n");
    uint8_t target[32];
    memset(target, 0, 32);
    uint8_t header[80];
    memset(header, 0xFF, 80);

    bool result = check_pow(header, target, 32);
    ASSERT(!result, "all-zero target rejects any hash");
}

static void test_hex_to_bytes_roundtrip(void)
{
    printf("test_hex_to_bytes_roundtrip:\n");
    const char *hex = "0123456789abcdef";
    uint8_t bytes[8];
    int rc = hex_to_bytes(hex, bytes, 8);
    ASSERT_EQ_INT(0, rc, "hex_to_bytes returns 0");
    ASSERT(bytes[0] == 0x01, "byte[0] == 0x01");
    ASSERT(bytes[1] == 0x23, "byte[1] == 0x23");
    ASSERT(bytes[7] == 0xef, "byte[7] == 0xef");
}

static void test_nbits_28bit(void)
{
    printf("test_nbits_28bit:\n");
    uint8_t target[32];
    int target_len;
    nbits_to_target(0x05000001, target, &target_len);

    ASSERT_EQ_INT(32, target_len, "target_len is 32");
    ASSERT(target[29] == 0x01, "mantissa at byte 29 for exp=5");
    for (int i = 0; i < 32; i++) {
        if (i != 29) {
            ASSERT(target[i] == 0x00, "non-significant bytes are zero");
        }
    }
}

static void test_genesis_block_pow(void)
{
    printf("test_genesis_block_pow:\n");
    const char *genesis_hash_hex = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";

    uint8_t genesis_prevhash[32];
    uint8_t genesis_merkle[32];
    hex_to_bytes("0000000000000000000000000000000000000000000000000000000000000000", genesis_prevhash, 32);
    hex_to_bytes("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a", genesis_merkle, 32);

    uint32_t version = 1;
    uint32_t ntime = 0x495FAB29;
    uint32_t nbits = 0x1D00FFFF;
    uint32_t nonce = 2083236893;

    uint8_t header[80];
    build_header(genesis_prevhash, genesis_merkle, version, ntime, nbits, nonce, header);

    uint8_t hash[32];
    sha256d(header, 80, hash);

    uint8_t expected_internal[32];
    hex_to_bytes(genesis_hash_hex, expected_internal, 32);
    uint8_t expected[32];
    for (int i = 0; i < 32; i++) expected[i] = expected_internal[31 - i];

    char hash_hex[65];
    bytes_to_hex(hash, 32, hash_hex);
    printf("  computed: %s\n", hash_hex);
    printf("  expected: %s\n", genesis_hash_hex);

    ASSERT_MEM_EQ(expected, hash, 32, "genesis block hash matches");

    uint8_t target[32];
    int target_len;
    nbits_to_target(nbits, target, &target_len);
    ASSERT(check_pow(header, target, target_len), "genesis block passes POW check with nbits target");
}

int main(void)
{
    test_nbits_genesis();
    test_nbits_low_difficulty();
    test_nbits_zero();
    test_sha256d_known_vector();
    test_build_header_layout();
    test_check_pow_all_ones_target();
    test_check_pow_zero_target();
    test_hex_to_bytes_roundtrip();
    test_nbits_28bit();
    test_genesis_block_pow();

    TEST_SUMMARY();
}
