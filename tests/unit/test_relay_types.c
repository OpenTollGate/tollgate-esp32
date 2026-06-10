#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define ASSERT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); return 1; } } while(0)

#include "relay_types.h"

int main(void)
{
    int passed = 0, failed = 0;

    printf("--- relay_hex_to_bytes valid ---\n");
    {
        const char *hex = "deadbeef01020304050607080910111213141516171819202122232425262728";
        uint8_t out[32];
        int ret = relay_hex_to_bytes(hex, 64, out, 32);
        ASSERT(ret == 0, "hex_to_bytes should return 0");
        ASSERT(out[0] == 0xde, "first byte");
        ASSERT(out[1] == 0xad, "second byte");
        ASSERT(out[2] == 0xbe, "third byte");
        ASSERT(out[3] == 0xef, "fourth byte");
        ASSERT(out[31] == 0x28, "last byte");
        passed += 6;
        printf("  PASS: relay_hex_to_bytes valid\n");
    }

    printf("--- relay_hex_to_bytes length mismatch ---\n");
    {
        uint8_t out[4];
        int ret = relay_hex_to_bytes("dead", 4, out, 3);
        ASSERT(ret == -1, "wrong output length should fail");
        ret = relay_hex_to_bytes("dead", 2, out, 4);
        ASSERT(ret == -1, "short hex should fail");
        passed += 2;
        printf("  PASS: length mismatch\n");
    }

    printf("--- relay_hex_to_bytes invalid chars ---\n");
    {
        uint8_t out[2];
        int ret = relay_hex_to_bytes("zz", 2, out, 1);
        ASSERT(ret == -1, "invalid hex chars should fail");
        passed += 1;
        printf("  PASS: invalid chars\n");
    }

    printf("--- relay_hex_to_bytes zeros ---\n");
    {
        uint8_t out[4];
        int ret = relay_hex_to_bytes("00000000", 8, out, 4);
        ASSERT(ret == 0, "zeros should succeed");
        ASSERT(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0, "all zeros");
        passed += 2;
        printf("  PASS: zeros\n");
    }

    printf("--- relay_hex_to_bytes ff ---\n");
    {
        uint8_t out[2];
        int ret = relay_hex_to_bytes("ffff", 4, out, 2);
        ASSERT(ret == 0, "ff should succeed");
        ASSERT(out[0] == 0xff && out[1] == 0xff, "all ff");
        passed += 2;
        printf("  PASS: ff\n");
    }

    printf("--- relay_bytes_to_hex basic ---\n");
    {
        uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef};
        char hex[9];
        relay_bytes_to_hex(bytes, 4, hex);
        ASSERT(strcmp(hex, "deadbeef") == 0, "bytes_to_hex result");
        ASSERT(hex[8] == '\0', "null terminator");
        passed += 2;
        printf("  PASS: relay_bytes_to_hex basic\n");
    }

    printf("--- relay_bytes_to_hex zeros ---\n");
    {
        uint8_t bytes[] = {0x00, 0x00};
        char hex[5];
        relay_bytes_to_hex(bytes, 2, hex);
        ASSERT(strcmp(hex, "0000") == 0, "zeros hex");
        passed += 1;
        printf("  PASS: zeros hex\n");
    }

    printf("--- relay_bytes_to_hex single byte ---\n");
    {
        uint8_t bytes[] = {0x0a};
        char hex[3];
        relay_bytes_to_hex(bytes, 1, hex);
        ASSERT(strcmp(hex, "0a") == 0, "single byte hex");
        passed += 1;
        printf("  PASS: single byte\n");
    }

    printf("--- roundtrip hex <-> bytes ---\n");
    {
        const char *original = "0102030405060708091011121314151617181920212223242526272829303132";
        uint8_t buf[32];
        char hex[65];
        relay_hex_to_bytes(original, 64, buf, 32);
        relay_bytes_to_hex(buf, 32, hex);
        ASSERT(strcmp(hex, original) == 0, "roundtrip matches");
        passed += 1;
        printf("  PASS: roundtrip\n");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
