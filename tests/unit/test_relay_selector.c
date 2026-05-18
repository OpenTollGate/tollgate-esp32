#include "test_framework.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char url[128];
    char name[64];
    uint32_t latency_ms;
    bool supports_nip77;
    bool alive;
    int consecutive_failures;
} test_relay_t;

static int compare_relays(const void *a, const void *b)
{
    const test_relay_t *ra = (const test_relay_t *)a;
    const test_relay_t *rb = (const test_relay_t *)b;
    if (ra->alive && !rb->alive) return -1;
    if (!ra->alive && rb->alive) return 1;
    int score_a = (ra->supports_nip77 ? 1000 : 0) - ra->consecutive_failures * 100;
    int score_b = (rb->supports_nip77 ? 1000 : 0) - rb->consecutive_failures * 100;
    if (score_a != score_b) return score_b - score_a;
    return (int)ra->latency_ms - (int)rb->latency_ms;
}

int main(void)
{
    printf("=== test_relay_selector ===\n");

    printf("\n--- NIP-77 relay preferred over non-NIP-77 ---\n");
    test_relay_t relays[4] = {
        { .url = "relay_a", .latency_ms = 50, .supports_nip77 = false, .alive = true, .consecutive_failures = 0 },
        { .url = "relay_b", .latency_ms = 200, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
        { .url = "relay_c", .latency_ms = 30, .supports_nip77 = false, .alive = true, .consecutive_failures = 0 },
        { .url = "relay_d", .latency_ms = 500, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
    };
    qsort(relays, 4, sizeof(test_relay_t), compare_relays);
    ASSERT_EQ_STR("relay_b", relays[0].url, "NIP-77 relay with 200ms beats non-NIP with 50ms");

    printf("\n--- Dead relays sorted last ---\n");
    test_relay_t dead_test[2] = {
        { .url = "alive_relay", .latency_ms = 500, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
        { .url = "dead_relay", .latency_ms = 10, .supports_nip77 = true, .alive = false, .consecutive_failures = 3 },
    };
    qsort(dead_test, 2, sizeof(test_relay_t), compare_relays);
    ASSERT_EQ_STR("alive_relay", dead_test[0].url, "Alive relay sorts before dead");

    printf("\n--- Latency tiebreak when same NIP-77 status ---\n");
    test_relay_t tiebreak[3] = {
        { .url = "slow", .latency_ms = 300, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
        { .url = "fast", .latency_ms = 50, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
        { .url = "medium", .latency_ms = 150, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
    };
    qsort(tiebreak, 3, sizeof(test_relay_t), compare_relays);
    ASSERT_EQ_STR("fast", tiebreak[0].url, "Fastest NIP-77 relay sorts first");
    ASSERT_EQ_STR("medium", tiebreak[1].url, "Medium NIP-77 relay sorts second");
    ASSERT_EQ_STR("slow", tiebreak[2].url, "Slowest NIP-77 relay sorts last");

    printf("\n--- Failure penalty ---\n");
    test_relay_t failures[2] = {
        { .url = "clean", .latency_ms = 200, .supports_nip77 = true, .alive = true, .consecutive_failures = 0 },
        { .url = "flaky", .latency_ms = 50, .supports_nip77 = true, .alive = true, .consecutive_failures = 2 },
    };
    qsort(failures, 2, sizeof(test_relay_t), compare_relays);
    ASSERT_EQ_STR("clean", failures[0].url, "Clean relay beats flaky one");

    printf("\n--- Non-NIP-77 relay with failures vs alive non-NIP-77 ---\n");
    test_relay_t mixed[2] = {
        { .url = "ok_relay", .latency_ms = 100, .supports_nip77 = false, .alive = true, .consecutive_failures = 0 },
        { .url = "fail_relay", .latency_ms = 50, .supports_nip77 = false, .alive = true, .consecutive_failures = 3 },
    };
    qsort(mixed, 2, sizeof(test_relay_t), compare_relays);
    ASSERT_EQ_STR("ok_relay", mixed[0].url, "Non-failing relay beats one with failures");

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
