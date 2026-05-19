#include "test_framework.h"
#include "../../main/session.h"
#include "../../main/firewall.h"
#include "../../main/config.h"
#include "../../main/cashu.h"
#include <string.h>
#include <stdio.h>

static tollgate_config_t g_test_config;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

esp_err_t firewall_get_mac_for_ip(uint32_t ip, char *mac_out, size_t size) {
    (void)ip;
    snprintf(mac_out, size, "AA:BB:CC:DD:EE:FF");
    return 0;
}

static uint32_t g_granted_ips[32];
static int g_granted_count = 0;

void firewall_grant_access(uint32_t ip) {
    if (g_granted_count < 32) g_granted_ips[g_granted_count++] = ip;
}

void firewall_revoke_access(uint32_t ip) {
    (void)ip;
}

int main(void)
{
    printf("=== test_session_payment_method ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);
    g_granted_count = 0;

    printf("\n--- session_create sets PAYMENT_METHOD_CASHU ---\n");
    session_manager_init();
    session_t *s1 = session_create(0x0A010001, 60000);
    ASSERT(s1 != NULL, "session created");
    ASSERT_EQ_INT(PAYMENT_METHOD_CASHU, (int)s1->payment_method, "cashu session has PAYMENT_METHOD_CASHU");

    printf("\n--- session_create_bytes sets PAYMENT_METHOD_BYTES ---\n");
    session_manager_init();
    g_granted_count = 0;
    session_t *s2 = session_create_bytes(0x0A010002, 1048576);
    ASSERT(s2 != NULL, "bytes session created");
    ASSERT_EQ_INT(PAYMENT_METHOD_BYTES, (int)s2->payment_method, "bytes session has PAYMENT_METHOD_BYTES");
    ASSERT_EQ_UINT64(1048576, s2->allotment_bytes, "allotment_bytes set");
    ASSERT_EQ_UINT64(0, s2->bytes_consumed, "bytes_consumed starts at 0");

    printf("\n--- payment_method_t enum values are distinct ---\n");
    ASSERT(PAYMENT_METHOD_CASHU != PAYMENT_METHOD_MINING, "CASHU != MINING");
    ASSERT(PAYMENT_METHOD_CASHU != PAYMENT_METHOD_BYTES, "CASHU != BYTES");
    ASSERT(PAYMENT_METHOD_MINING != PAYMENT_METHOD_BYTES, "MINING != BYTES");

    printf("\n--- session extend preserves payment_method ---\n");
    session_manager_init();
    g_granted_count = 0;
    session_t *s3 = session_create(0x0A010003, 60000);
    ASSERT_EQ_INT(PAYMENT_METHOD_CASHU, (int)s3->payment_method, "initially CASHU");
    session_extend(s3, 30000);
    ASSERT_EQ_INT(PAYMENT_METHOD_CASHU, (int)s3->payment_method, "still CASHU after extend");

    printf("\n--- bytes session allotment_ms is INT64_MAX ---\n");
    session_manager_init();
    g_granted_count = 0;
    session_t *s4 = session_create_bytes(0x0A010004, 2097152);
    ASSERT(s4->allotment_ms == INT64_MAX, "bytes session has INT64_MAX allotment_ms");

    TEST_SUMMARY();
}
