#include "test_framework.h"
#include "tollgate_core_session.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core_cashu.h"
#include "tollgate_core.h"
#include "../../main/config.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

static tollgate_config_t g_test_config;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

int main(void)
{
    printf("=== test_session_payment_method ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);

    printf("\n--- tollgate_core_session_create sets TG_PAYMENT_CASHU ---\n");
    tollgate_core_session_init();
    tg_session_t *s1 = tollgate_core_session_create(0x0A010001, 60000);
    ASSERT(s1 != NULL, "session created");
    ASSERT_EQ_INT(TG_PAYMENT_CASHU, (int)s1->payment_method, "cashu session has TG_PAYMENT_CASHU");

    printf("\n--- tollgate_core_session_create_bytes sets TG_PAYMENT_BYTES ---\n");
    tollgate_core_session_init();
    tg_session_t *s2 = tollgate_core_session_create_bytes(0x0A010002, 1048576);
    ASSERT(s2 != NULL, "bytes session created");
    ASSERT_EQ_INT(TG_PAYMENT_BYTES, (int)s2->payment_method, "bytes session has TG_PAYMENT_BYTES");
    ASSERT_EQ_UINT64(1048576, s2->allotment_bytes, "allotment_bytes set");
    ASSERT_EQ_UINT64(0, s2->bytes_consumed, "bytes_consumed starts at 0");

    printf("\n--- tg_payment_method_t enum values are distinct ---\n");
    ASSERT(TG_PAYMENT_CASHU != TG_PAYMENT_MINING, "CASHU != MINING");
    ASSERT(TG_PAYMENT_CASHU != TG_PAYMENT_BYTES, "CASHU != BYTES");
    ASSERT(TG_PAYMENT_MINING != TG_PAYMENT_BYTES, "MINING != BYTES");

    printf("\n--- session extend preserves payment_method ---\n");
    tollgate_core_session_init();
    tg_session_t *s3 = tollgate_core_session_create(0x0A010003, 60000);
    ASSERT_EQ_INT(TG_PAYMENT_CASHU, (int)s3->payment_method, "initially CASHU");
    tollgate_core_session_extend(s3, 30000);
    ASSERT_EQ_INT(TG_PAYMENT_CASHU, (int)s3->payment_method, "still CASHU after extend");

    printf("\n--- bytes session allotment_ms is INT64_MAX ---\n");
    tollgate_core_session_init();
    tg_session_t *s4 = tollgate_core_session_create_bytes(0x0A010004, 2097152);
    ASSERT(s4->allotment_ms == INT64_MAX, "bytes session has INT64_MAX allotment_ms");

    TEST_SUMMARY();
}
