#include "test_framework.h"
#include "tollgate_core_session.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core_cashu.h"
#include "tollgate_core.h"
#include "../../main/config.h"
#include <string.h>
#include <stdio.h>

static tollgate_config_t g_test_config;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

static void test_sessions(void)
{
    printf("=== test_session ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);

    printf("\n--- tollgate_core_session_init ---\n");
    esp_err_t ret = tollgate_core_session_init();
    ASSERT_EQ_INT(0, ret, "tollgate_core_session_init succeeds");
    ASSERT_EQ_INT(0, tollgate_core_session_active_count(), "No sessions after init");

    printf("\n--- tollgate_core_session_create ---\n");
    tg_session_t *s = tollgate_core_session_create(0x0A01A8C0, 60000);
    ASSERT(s != NULL, "session_create returns non-NULL");
    ASSERT_EQ_INT(1, tollgate_core_session_active_count(), "1 session after create");

    printf("\n--- tollgate_core_session_find_by_ip ---\n");
    tg_session_t *found = tollgate_core_session_find_by_ip(0x0A01A8C0);
    ASSERT(found == s, "tollgate_core_session_find_by_ip returns the created session");
    ASSERT(tollgate_core_session_find_by_ip(0x01020304) == NULL, "tollgate_core_session_find_by_ip returns NULL for unknown IP");

    printf("\n--- tollgate_core_session_extend ---\n");
    uint64_t old_allotment = s->allotment_ms;
    tollgate_core_session_extend(s, 30000);
    ASSERT(s->allotment_ms == old_allotment + 30000, "Allotment extended by 30000ms");

    printf("\n--- tollgate_core_session_extend for existing client ---\n");
    tg_session_t *s2 = tollgate_core_session_create(0x0A01A8C0, 30000);
    ASSERT(s2 == s, "same IP returns existing session");
    ASSERT(s->allotment_ms == old_allotment + 60000, "allotment extended by 30000ms on re-pay");

    printf("\n--- tollgate_core_session_revoke ---\n");
    tollgate_core_session_revoke(s);
    ASSERT_EQ_INT(0, tollgate_core_session_active_count(), "No active sessions after revoke");

    printf("\n--- tollgate_core_session_revoke_all ---\n");
    tollgate_core_session_create(0x01000001, 60000);
    tollgate_core_session_create(0x01000002, 60000);
    ASSERT_EQ_INT(2, tollgate_core_session_active_count(), "2 sessions created");

    tollgate_core_session_revoke_all();
    ASSERT_EQ_INT(0, tollgate_core_session_active_count(), "No sessions after revoke_all");

    printf("\n--- session_tick does not crash ---\n");
    tollgate_core_session_init();
    tollgate_core_session_create(0x0A000001, 60000);
    tollgate_core_session_tick();
    ASSERT_EQ_INT(1, tollgate_core_session_active_count(), "Session still active after tick (not expired)");
}

void test_bytes_sessions(void)
{
    printf("\n=== Bytes-based sessions ===\n");
    tollgate_core_session_init();
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "bytes", sizeof(g_test_config.metric) - 1);

    uint64_t allotment = 22020096;
    tg_session_t *s = tollgate_core_session_create_bytes(0x0A010001, allotment);
    ASSERT(s != NULL, "bytes session created");
    ASSERT_EQ_INT(1, tollgate_core_session_active_count(), "1 active bytes session");

    ASSERT(!tollgate_core_session_is_expired(s), "not expired at 0 consumed");

    tollgate_core_session_add_bytes(0x0A010001, 10000000);
    ASSERT(!tollgate_core_session_is_expired(s), "not expired at 10MB of 21MB");
    ASSERT_EQ_UINT64(10000000, s->bytes_consumed, "consumed 10MB");

    tollgate_core_session_add_bytes(0x0A010001, 12200996);
    ASSERT(tollgate_core_session_is_expired(s), "expired after consuming all allotment");
    ASSERT_EQ_UINT64(22200996, s->bytes_consumed, "consumed 22.2MB");

    tollgate_core_session_add_bytes(0x0A010001, 1000);
    ASSERT_EQ_UINT64(22201996, s->bytes_consumed, "consumption keeps growing past expiry");

    printf("\n--- Bytes session for unknown IP does nothing ---\n");
    tollgate_core_session_add_bytes(0x0B0B0B0B, 9999);
    ASSERT_EQ_UINT64(22201996, s->bytes_consumed, "unknown IP no effect");

    printf("\n--- Mixed metric: milliseconds still works ---\n");
    tollgate_core_session_init();
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);
    tg_session_t *ms = tollgate_core_session_create(0x0A020001, 60000);
    ASSERT(ms != NULL, "ms session created");
    ASSERT(!tollgate_core_session_is_expired(ms), "ms session not expired immediately");

    printf("\n--- tollgate_core_cashu_calculate_allotment dispatch ---\n");
    uint64_t a = tollgate_core_cashu_calculate_allotment(21, 21, 60000);
    ASSERT_EQ_UINT64(60000, a, "21 sats / 21 per step * 60000ms = 60000ms");

    printf("\n=== ALL BYTES SESSION TESTS PASSED ===\n");
}

int main(void)
{
    test_sessions();
    test_bytes_sessions();
    return g_tests_failed > 0 ? 1 : 0;
}
