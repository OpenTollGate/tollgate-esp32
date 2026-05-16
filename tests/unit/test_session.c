#include "test_framework.h"
#include "../../main/session.h"
#include "../../main/firewall.h"
#include <string.h>
#include <stdio.h>

static uint32_t g_granted_ips[32];
static int g_granted_count = 0;
static uint32_t g_revoked_ips[32];
static int g_revoked_count = 0;

esp_err_t firewall_get_mac_for_ip(uint32_t ip, char *mac_out, size_t size) {
    (void)ip;
    snprintf(mac_out, size, "AA:BB:CC:DD:EE:FF");
    return 0;
}

void firewall_grant_access(uint32_t ip) {
    if (g_granted_count < 32) g_granted_ips[g_granted_count++] = ip;
}

void firewall_revoke_access(uint32_t ip) {
    if (g_revoked_count < 32) g_revoked_ips[g_revoked_count++] = ip;
}

int main(void)
{
    printf("=== test_session ===\n");

    g_granted_count = 0;
    g_revoked_count = 0;

    printf("\n--- session_manager_init ---\n");
    esp_err_t ret = session_manager_init();
    ASSERT_EQ_INT(0, ret, "session_manager_init succeeds");
    ASSERT_EQ_INT(0, session_active_count(), "No sessions after init");

    printf("\n--- session_create ---\n");
    const char *secrets[] = {"secret1", "secret2"};
    session_t *s = session_create(0x0A01A8C0, 60000, secrets, 2);
    ASSERT(s != NULL, "session_create returns non-NULL");
    ASSERT_EQ_INT(1, session_active_count(), "1 session after create");
    ASSERT_EQ_INT(1, g_granted_count, "firewall_grant_access was called");

    printf("\n--- session_find_by_ip ---\n");
    session_t *found = session_find_by_ip(0x0A01A8C0);
    ASSERT(found == s, "session_find_by_ip returns the created session");
    ASSERT(session_find_by_ip(0x01020304) == NULL, "session_find_by_ip returns NULL for unknown IP");

    printf("\n--- session_is_secret_spent ---\n");
    ASSERT(session_is_secret_spent("secret1"), "secret1 is marked spent");
    ASSERT(session_is_secret_spent("secret2"), "secret2 is marked spent");
    ASSERT(!session_is_secret_spent("secret_unknown"), "unknown secret is not spent");

    printf("\n--- Duplicate secret rejected ---\n");
    const char *dup_secrets[] = {"secret1"};
    g_granted_count = 0;
    session_t *dup = session_create(0x0B01A8C0, 60000, dup_secrets, 1);
    ASSERT(dup == NULL, "Duplicate secret returns NULL");
    ASSERT_EQ_INT(0, g_granted_count, "No new firewall grant for duplicate");

    printf("\n--- session_extend ---\n");
    uint64_t old_allotment = s->allotment_ms;
    session_extend(s, 30000);
    ASSERT(s->allotment_ms == old_allotment + 30000, "Allotment extended by 30000ms");

    printf("\n--- session_revoke ---\n");
    g_revoked_count = 0;
    session_revoke(s);
    ASSERT_EQ_INT(1, g_revoked_count, "firewall_revoke_access was called");
    ASSERT_EQ_INT(0, session_active_count(), "No active sessions after revoke");

    printf("\n--- session_revoke_all ---\n");
    const char *s1[] = {"s1"};
    const char *s2[] = {"s2"};
    session_create(0x01000001, 60000, s1, 1);
    session_create(0x01000002, 60000, s2, 1);
    ASSERT_EQ_INT(2, session_active_count(), "2 sessions created");

    g_revoked_count = 0;
    session_revoke_all();
    ASSERT_EQ_INT(0, session_active_count(), "No sessions after revoke_all");

    printf("\n--- session_tick does not crash ---\n");
    session_manager_init();
    const char *st[] = {"tick_secret"};
    session_create(0x0A000001, 60000, st, 1);
    session_tick();
    ASSERT_EQ_INT(1, session_active_count(), "Session still active after tick (not expired)");

    TEST_SUMMARY();
}
