#include "test_framework.h"
#include "../../components/tollgate_core/src/tollgate_core_portal.h"
#include "../../components/tollgate_core/src/tollgate_core_session.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("=== test_portal ===\n");

    printf("\n--- template_replace single key ---\n");
    {
        char *result = tollgate_core_portal_template_replace(
            "Hello __NAME__!", "__NAME__", "World");
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "Hello World!") == 0, "single substitution works");
        free(result);
    }

    printf("\n--- template_replace no match ---\n");
    {
        char *result = tollgate_core_portal_template_replace(
            "Hello World!", "__NAME__", "Test");
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "Hello World!") == 0, "no match returns original");
        free(result);
    }

    printf("\n--- template_replace multiple occurrences ---\n");
    {
        char *result = tollgate_core_portal_template_replace(
            "__IP__:2121 and __IP__:80", "__IP__", "10.0.0.1");
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "10.0.0.1:2121 and 10.0.0.1:80") == 0,
               "multiple occurrences replaced");
        free(result);
    }

    printf("\n--- template_replace key longer than value ---\n");
    {
        char *result = tollgate_core_portal_template_replace(
            "A __LONGKEY__ B", "__LONGKEY__", "X");
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "A X B") == 0, "shrinks correctly");
        free(result);
    }

    printf("\n--- render multi-key substitution ---\n");
    {
        tollgate_portal_sub_t subs[] = {
            { "__AP_IP__", "10.0.0.1" },
            { "__PRICE__", "21" },
            { "__MINT_URL__", "https://test.mint" },
        };
        char *result = tollgate_core_portal_render(
            "IP=__AP_IP__ price=__PRICE__ mint=__MINT_URL__", subs, 3);
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "IP=10.0.0.1 price=21 mint=https://test.mint") == 0,
               "multi-key render works");
        free(result);
    }

    printf("\n--- render with no matches ---\n");
    {
        tollgate_portal_sub_t subs[] = {
            { "__AP_IP__", "10.0.0.1" },
        };
        char *result = tollgate_core_portal_render("no placeholders", subs, 1);
        ASSERT(result != NULL, "result not NULL");
        ASSERT(strcmp(result, "no placeholders") == 0, "no matches returns original");
        free(result);
    }

    printf("\n--- calc_usage time metric ---\n");
    {
        tg_session_t session = {0};
        session.active = true;
        session.allotment_ms = 60000;
        session.start_time_ms = 10000;

        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_portal_calc_usage(&session, "milliseconds", 30000, &remaining, &total);
        ASSERT(ok, "calc_usage returns true for active session");
        ASSERT_EQ_UINT64(40000, (unsigned long long)remaining, "remaining = 60000 - 20000");
        ASSERT_EQ_UINT64(60000, (unsigned long long)total, "total = allotment_ms");
    }

    printf("\n--- calc_usage bytes metric ---\n");
    {
        tg_session_t session = {0};
        session.active = true;
        session.allotment_bytes = 1000000;
        session.bytes_consumed = 300000;

        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_portal_calc_usage(&session, "bytes", 0, &remaining, &total);
        ASSERT(ok, "calc_usage returns true for bytes");
        ASSERT_EQ_UINT64(700000, (unsigned long long)remaining, "remaining = 1000000 - 300000");
        ASSERT_EQ_UINT64(1000000, (unsigned long long)total, "total = allotment_bytes");
    }

    printf("\n--- calc_usage expired time ---\n");
    {
        tg_session_t session = {0};
        session.active = true;
        session.allotment_ms = 60000;
        session.start_time_ms = 0;

        int64_t remaining = 0, total = 0;
        tollgate_core_portal_calc_usage(&session, "milliseconds", 120000, &remaining, &total);
        ASSERT_EQ_UINT64(0, (unsigned long long)remaining, "remaining clamped to 0 when expired");
    }

    printf("\n--- calc_usage inactive session ---\n");
    {
        tg_session_t session = {0};
        session.active = false;

        int64_t remaining = 0, total = 0;
        bool ok = tollgate_core_portal_calc_usage(&session, "milliseconds", 0, &remaining, &total);
        ASSERT(!ok, "inactive session returns false");
    }

    printf("\n--- format_usage ---\n");
    {
        tg_session_t session = {0};
        session.active = true;
        session.allotment_ms = 60000;
        session.start_time_ms = 10000;

        char buf[64];
        int len = tollgate_core_portal_format_usage(&session, "milliseconds", 30000, buf, sizeof(buf));
        ASSERT(len > 0, "format_usage returns positive length");
        ASSERT(strstr(buf, "/") != NULL, "contains separator");
        ASSERT(strstr(buf, "60000") != NULL, "contains total");
    }

    printf("\n--- is_captive_uri ---\n");
    {
        ASSERT(tollgate_core_portal_is_captive_uri("/generate_204"), "generate_204 is captive");
        ASSERT(tollgate_core_portal_is_captive_uri("/hotspot-detect.html"), "hotspot-detect is captive");
        ASSERT(tollgate_core_portal_is_captive_uri("/success.txt"), "success.txt is captive");
        ASSERT(tollgate_core_portal_is_captive_uri("/ncsi.txt"), "ncsi.txt is captive");
        ASSERT(tollgate_core_portal_is_captive_uri("/connecttest.txt"), "connecttest is captive");
        ASSERT(tollgate_core_portal_is_captive_uri("/wpad.dat"), "wpad.dat is captive");
        ASSERT(!tollgate_core_portal_is_captive_uri("/"), "root is not captive");
        ASSERT(!tollgate_core_portal_is_captive_uri("/setup"), "setup is not captive");
        ASSERT(!tollgate_core_portal_is_captive_uri(NULL), "NULL is not captive");
    }

    TEST_SUMMARY();
}
