#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  PASS: %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  FAIL: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_EQ_INT(expected, actual, msg) do { \
    int _e = (expected), _a = (actual); \
    if (_e == _a) { \
        printf("  PASS: %s (got %d)\n", msg, _a); \
        g_tests_passed++; \
    } else { \
        printf("  FAIL: %s (expected %d, got %d) at %s:%d\n", msg, _e, _a, __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_EQ_STR(expected, actual, msg) do { \
    const char *_e = (expected), *_a = (actual); \
    if (_e && _a && strcmp(_e, _a) == 0) { \
        printf("  PASS: %s (got \"%s\")\n", msg, _a); \
        g_tests_passed++; \
    } else { \
        printf("  FAIL: %s (expected \"%s\", got \"%s\") at %s:%d\n", msg, _e ? _e : "(null)", _a ? _a : "(null)", __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len, msg) do { \
    const uint8_t *_e = (const uint8_t *)(expected), *_a = (const uint8_t *)(actual); \
    size_t _l = (len); \
    if (_e && _a && memcmp(_e, _a, _l) == 0) { \
        printf("  PASS: %s (%zu bytes match)\n", msg, _l); \
        g_tests_passed++; \
    } else { \
        printf("  FAIL: %s (%zu bytes mismatch) at %s:%d\n", msg, _l, __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n=== Results: %d passed, %d failed ===\n", g_tests_passed, g_tests_failed); \
    return g_tests_failed > 0 ? 1 : 0; \
} while(0)

#endif
