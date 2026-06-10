#include "test_framework.h"
#include <mbedtls/sha256.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void build_header(const uint8_t prevhash[32], const uint8_t merkle_root[32],
                          uint32_t version, uint32_t ntime, uint32_t nbits,
                          uint32_t nonce, uint8_t out[80])
{
    memset(out, 0, 80);
    out[0] = (version >> 24) & 0xFF;
    out[1] = (version >> 16) & 0xFF;
    out[2] = (version >> 8) & 0xFF;
    out[3] = version & 0xFF;
    memcpy(out + 4, prevhash, 32);
    memcpy(out + 36, merkle_root, 32);
    out[68] = (ntime >> 24) & 0xFF;
    out[69] = (ntime >> 16) & 0xFF;
    out[70] = (ntime >> 8) & 0xFF;
    out[71] = ntime & 0xFF;
    out[72] = (nbits >> 24) & 0xFF;
    out[73] = (nbits >> 16) & 0xFF;
    out[74] = (nbits >> 8) & 0xFF;
    out[75] = nbits & 0xFF;
    out[76] = (nonce >> 24) & 0xFF;
    out[77] = (nonce >> 16) & 0xFF;
    out[78] = (nonce >> 8) & 0xFF;
    out[79] = nonce & 0xFF;
}

static void double_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t tmp[32];
    mbedtls_sha256(data, len, tmp, 0);
    mbedtls_sha256(tmp, 32, out, 0);
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len / 2 > out_len) return -1;
    for (size_t i = 0; i < hex_len / 2; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        out[i] = (uint8_t)byte;
    }
    return 0;
}

static bool check_pow(const uint8_t header[80], const uint8_t *target, int target_len)
{
    uint8_t hash[32];
    uint8_t tmp[32];
    mbedtls_sha256(header, 80, tmp, 0);
    mbedtls_sha256(tmp, 32, hash, 0);

    uint8_t hash_be[32];
    for (int i = 0; i < 32; i++) hash_be[i] = hash[31 - i];

    for (int i = 0; i < target_len && i < 32; i++) {
        if (hash_be[i] < target[i]) return true;
        if (hash_be[i] > target[i]) return false;
    }
    return true;
}

static void build_target_from_difficulty(double diff, uint8_t *target, int *target_len)
{
    *target_len = 32;
    memset(target, 0xFF, 32);
    if (diff <= 0.0 || diff > 1e15) return;
    double pdiff_max = 0x00000000FFFF0000ULL;
    if (diff >= pdiff_max) {
        memset(target, 0, 32);
        target[7] = 0xFF;
        return;
    }
    uint64_t target_val = (uint64_t)(pdiff_max / diff);
    if (target_val == 0) target_val = 1;
    memset(target, 0, 32);
    for (int i = 0; i < 8 && target_val > 0; i++) {
        target[7 - i] = (uint8_t)(target_val & 0xFF);
        target_val >>= 8;
    }
}

static void nbits_to_target(uint32_t nbits, uint8_t target[32])
{
    memset(target, 0, 32);
    uint8_t exp = (nbits >> 24) & 0xFF;
    uint32_t mantissa = nbits & 0x007FFFFF;
    if (exp <= 3) {
        mantissa >>= 8 * (3 - exp);
        target[31] = mantissa & 0xFF;
        target[30] = (mantissa >> 8) & 0xFF;
        target[29] = (mantissa >> 16) & 0xFF;
    } else {
        int pos = 32 - exp;
        for (int i = 0; i < 3 && (pos + i) < 32; i++) {
            target[pos + i] = (mantissa >> (8 * (2 - i))) & 0xFF;
        }
    }
}

int main(void)
{
    printf("=== test_stratum_pow ===\n");

    printf("\n--- Bitcoin genesis block header construction ---\n");
    {
        uint8_t prevhash[32] = {0};
        uint8_t merkle[32];
        hex_to_bytes("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a",
                     merkle, 32);
        uint32_t version = 0x01000000;
        uint32_t ntime = 0x29AB5F49;
        uint32_t nbits = 0xFFFF001D;
        uint32_t nonce = 0x1DAC2B7C;

        uint8_t header[80];
        build_header(prevhash, merkle, version, ntime, nbits, nonce, header);

        uint8_t hash[32];
        double_sha256(header, 80, hash);

        uint8_t expected[32];
        hex_to_bytes("6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000",
                     expected, 32);

        ASSERT_MEM_EQ(expected, hash, 32, "genesis block double-SHA256 matches");
    }

    printf("\n--- Bitcoin genesis block passes PoW check ---\n");
    {
        uint8_t prevhash[32] = {0};
        uint8_t merkle[32];
        hex_to_bytes("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a",
                     merkle, 32);

        uint8_t header[80];
        build_header(prevhash, merkle, 0x01000000, 0x29AB5F49, 0xFFFF001D, 0x1DAC2B7C, header);

        uint8_t target[32];
        nbits_to_target(0x1D00FFFF, target);

        bool valid = check_pow(header, target, 32);
        ASSERT(valid, "genesis block header meets target");
    }

    printf("\n--- Wrong nonce fails PoW ---\n");
    {
        uint8_t prevhash[32] = {0};
        uint8_t merkle[32];
        hex_to_bytes("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a",
                     merkle, 32);

        uint8_t header[80];
        build_header(prevhash, merkle, 0x01000000, 0x29AB5F49, 0xFFFF001D, 0xDEADBEEF, header);

        uint8_t target[32];
        nbits_to_target(0x1D00FFFF, target);

        bool valid = check_pow(header, target, 32);
        ASSERT(!valid, "wrong nonce fails PoW");
    }

    printf("\n--- build_target_from_difficulty: diff=1 (pools default) ---\n");
    {
        uint8_t target[32];
        int tlen;
        build_target_from_difficulty(1.0, target, &tlen);

        ASSERT_EQ_INT(32, tlen, "target len = 32");
        ASSERT(target[4] == 0xFF, "target[4] = 0xFF");

        uint64_t expected_val = 0x00000000FFFF0000ULL;
        uint8_t expected[32] = {0};
        for (int i = 0; i < 8 && expected_val > 0; i++) {
            expected[7 - i] = (uint8_t)(expected_val & 0xFF);
            expected_val >>= 8;
        }
        ASSERT_MEM_EQ(expected, target, 32, "diff=1 target matches");
    }

    printf("\n--- build_target_from_difficulty: diff=0 edge case ---\n");
    {
        uint8_t target[32];
        int tlen;
        build_target_from_difficulty(0.0, target, &tlen);

        uint8_t all_ff[32];
        memset(all_ff, 0xFF, 32);
        ASSERT_MEM_EQ(all_ff, target, 32, "diff=0 => all 0xFF (accept anything)");
    }

    printf("\n--- build_target_from_difficulty: diff=65536 ---\n");
    {
        uint8_t target[32];
        int tlen;
        build_target_from_difficulty(65536.0, target, &tlen);

        uint64_t expected_val = 0x00000000FFFF0000ULL / 65536ULL;
        uint8_t expected[32] = {0};
        for (int i = 0; i < 8 && expected_val > 0; i++) {
            expected[7 - i] = (uint8_t)(expected_val & 0xFF);
            expected_val >>= 8;
        }
        ASSERT_MEM_EQ(expected, target, 32, "diff=65536 target correct");
    }

    printf("\n--- build_target_from_difficulty: very high diff ---\n");
    {
        uint8_t target[32];
        int tlen;
        build_target_from_difficulty(1e14, target, &tlen);

        int nonzero = 0;
        for (int i = 0; i < 32; i++) {
            if (target[i] != 0) nonzero++;
        }
        ASSERT(nonzero <= 2, "very high diff => very few nonzero bytes");
    }

    printf("\n--- check_pow: hash exactly equals target => true ---\n");
    {
        uint8_t header[80];
        memset(header, 0, 80);

        uint8_t hash[32];
        double_sha256(header, 80, hash);

        uint8_t hash_be[32];
        for (int i = 0; i < 32; i++) hash_be[i] = hash[31 - i];

        bool valid = check_pow(header, hash_be, 32);
        ASSERT(valid, "hash == target passes");
    }

    printf("\n--- check_pow: hash > target => false ---\n");
    {
        uint8_t header[80];
        memset(header, 0xFF, 80);

        uint8_t target[32];
        memset(target, 0, 32);
        target[7] = 0x01;

        bool valid = check_pow(header, target, 32);
        ASSERT(!valid, "all-FF hash > minimal target fails");
    }

    printf("\n--- build_header: byte order verification ---\n");
    {
        uint8_t prevhash[32];
        memset(prevhash, 0xAB, 32);
        uint8_t merkle[32];
        memset(merkle, 0xCD, 32);

        uint8_t header[80];
        build_header(prevhash, merkle, 0x01020304, 0xAABBCCDD,
                     0x11223344, 0xCAFEBABE, header);

        ASSERT_EQ_INT(0x01, header[0], "version byte 0");
        ASSERT_EQ_INT(0x02, header[1], "version byte 1");
        ASSERT_EQ_INT(0x03, header[2], "version byte 2");
        ASSERT_EQ_INT(0x04, header[3], "version byte 3");
        ASSERT_EQ_INT(0xAB, header[4], "prevhash start");
        ASSERT_EQ_INT(0xCD, header[36], "merkle start");
        ASSERT_EQ_INT(0xAA, header[68], "ntime byte 0");
        ASSERT_EQ_INT(0xBB, header[69], "ntime byte 1");
        ASSERT_EQ_INT(0xCC, header[70], "ntime byte 2");
        ASSERT_EQ_INT(0xDD, header[71], "ntime byte 3");
        ASSERT_EQ_INT(0x11, header[72], "nbits byte 0");
        ASSERT_EQ_INT(0x22, header[73], "nbits byte 1");
        ASSERT_EQ_INT(0x33, header[74], "nbits byte 2");
        ASSERT_EQ_INT(0x44, header[75], "nbits byte 3");
        ASSERT_EQ_INT(0xCA, header[76], "nonce byte 0");
        ASSERT_EQ_INT(0xFE, header[77], "nonce byte 1");
        ASSERT_EQ_INT(0xBA, header[78], "nonce byte 2");
        ASSERT_EQ_INT(0xBE, header[79], "nonce byte 3");
    }

    printf("\n--- nbits_to_target: compact to 256-bit ---\n");
    {
        uint8_t target[32];
        nbits_to_target(0x1D00FFFF, target);

        ASSERT_EQ_INT(0x00, target[0], "target byte 0");
        ASSERT_EQ_INT(0xFF, target[4], "target byte 4");
        ASSERT_EQ_INT(0xFF, target[5], "target byte 5");

        bool upper_zero = true;
        for (int i = 0; i < 4; i++) {
            if (target[i] != 0) { upper_zero = false; break; }
        }
        ASSERT(upper_zero, "first 4 bytes zero for genesis nbits");
    }

    printf("\n--- nbits_to_target: higher difficulty ---\n");
    {
        uint8_t target[32];
        nbits_to_target(0x170309E2, target);

        ASSERT_EQ_INT(0x00, target[0], "target byte 0");
        bool mostly_zero = true;
        int nonzero = 0;
        for (int i = 0; i < 32; i++) {
            if (target[i] != 0) nonzero++;
        }
        ASSERT(mostly_zero && nonzero <= 4, "high diff => few nonzero bytes");
    }

    TEST_SUMMARY();
}
