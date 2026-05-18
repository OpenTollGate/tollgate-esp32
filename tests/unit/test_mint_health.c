#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mint_health.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    printf("  TEST: %s ... ", name); \
} while(0)

#define PASS() do { \
    pass_count++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_TRUE(a, msg) do { \
    if (!(a)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_FALSE(a, msg) do { \
    if ((a)) { FAIL(msg); return; } \
} while(0)

static void test_init_basic(void) {
    TEST("init with 4 mints");
    const char urls[4][256] = {
        "https://mint.minibits.cash/Bitcoin",
        "https://mint.coinos.io",
        "https://21mint.me",
        "https://mint.lnvoltz.com"
    };
    esp_err_t err = mint_health_init(urls, 4);
    ASSERT_EQ(err, 0, "init should return ESP_OK");
    PASS();
}

static void test_get_all(void) {
    TEST("get_all returns correct count");
    int count = 0;
    const mint_status_t *mints = mint_health_get_all(&count);
    ASSERT_EQ(count, 4, "should have 4 mints");
    ASSERT_TRUE(mints != NULL, "mints should not be NULL");
    PASS();
}

static void test_initial_state_unreachable(void) {
    TEST("initial state: all mints unreachable (no probes run)");
    const char *expected_urls[] = {
        "https://mint.minibits.cash/Bitcoin",
        "https://mint.coinos.io",
        "https://21mint.me",
        "https://mint.lnvoltz.com"
    };
    int count = 0;
    const mint_status_t *mints = mint_health_get_all(&count);
    ASSERT_EQ(count, 4, "should have 4 mints");
    for (int i = 0; i < count; i++) {
        ASSERT_FALSE(mints[i].reachable, "initial mint should be unreachable");
        ASSERT_EQ(mints[i].consecutive_successes, 0, "initial successes should be 0");
        ASSERT_TRUE(strcmp(mints[i].url, expected_urls[i]) == 0, "URL mismatch");
    }
    PASS();
}

static void test_is_reachable_before_probes(void) {
    TEST("is_reachable returns false before probes");
    bool r = mint_health_is_reachable("https://mint.minibits.cash/Bitcoin");
    ASSERT_FALSE(r, "should be unreachable before probes");
    PASS();
}

static void test_is_reachable_null(void) {
    TEST("is_reachable returns false for NULL");
    bool r = mint_health_is_reachable(NULL);
    ASSERT_FALSE(r, "NULL should return false");
    PASS();
}

static void test_is_reachable_unknown_url(void) {
    TEST("is_reachable returns false for unknown URL");
    bool r = mint_health_is_reachable("https://unknown.mint.example.com");
    ASSERT_FALSE(r, "unknown URL should return false");
    PASS();
}

static void test_mark_unreachable(void) {
    TEST("mark_unreachable on already-unreachable mint");
    mint_health_mark_unreachable("https://mint.coinos.io");
    bool r = mint_health_is_reachable("https://mint.coinos.io");
    ASSERT_FALSE(r, "should still be unreachable");
    PASS();
}

static void test_mark_unreachable_null(void) {
    TEST("mark_unreachable with NULL does not crash");
    mint_health_mark_unreachable(NULL);
    PASS();
}

static void test_init_overflow(void) {
    TEST("init with more than MAX mints truncates");
    const char urls[MINT_HEALTH_MAX + 2][256];
    for (int i = 0; i < MINT_HEALTH_MAX + 2; i++) {
        snprintf((char *)urls[i], 256, "https://mint%d.example.com", i);
    }
    esp_err_t err = mint_health_init(urls, MINT_HEALTH_MAX + 2);
    ASSERT_EQ(err, 0, "init should succeed");

    int count = 0;
    mint_health_get_all(&count);
    ASSERT_EQ(count, MINT_HEALTH_MAX, "should be truncated to MAX");
    PASS();
}

static void test_init_empty(void) {
    TEST("init with 0 mints");
    esp_err_t err = mint_health_init(NULL, 0);
    ASSERT_EQ(err, 0, "init with 0 should succeed");

    int count = -1;
    mint_health_get_all(&count);
    ASSERT_EQ(count, 0, "should have 0 mints");
    PASS();
}

static void dummy_cb(void) { }

static void test_register_callback(void) {
    TEST("register_callback does not crash");
    mint_health_register_callback(dummy_cb);
    PASS();
}

static void test_register_callback_null(void) {
    TEST("register_callback NULL does not crash");
    mint_health_register_callback(NULL);
    PASS();
}

static void test_reinit_resets_state(void) {
    TEST("re-init resets state");
    const char urls[2][256] = {
        "https://mint-a.example.com",
        "https://mint-b.example.com"
    };
    mint_health_init(urls, 2);

    int count = 0;
    const mint_status_t *mints = mint_health_get_all(&count);
    ASSERT_EQ(count, 2, "should have 2 mints");
    ASSERT_TRUE(strcmp(mints[0].url, "https://mint-a.example.com") == 0, "first URL");
    ASSERT_TRUE(strcmp(mints[1].url, "https://mint-b.example.com") == 0, "second URL");
    PASS();
}

static void test_start_stop(void) {
    TEST("start/stop do not crash (task stubbed)");
    mint_health_start();
    mint_health_stop();
    PASS();
}

int main(void) {
    printf("\n=== Mint Health Unit Tests ===\n\n");

    test_init_basic();
    test_get_all();
    test_initial_state_unreachable();
    test_is_reachable_before_probes();
    test_is_reachable_null();
    test_is_reachable_unknown_url();
    test_mark_unreachable();
    test_mark_unreachable_null();
    test_init_overflow();
    test_init_empty();
    test_register_callback();
    test_register_callback_null();
    test_reinit_resets_state();
    test_start_stop();

    printf("\n=== Results: %d passed, %d failed ===\n\n", pass_count, test_count - pass_count);
    return (pass_count == test_count) ? 0 : 1;
}
