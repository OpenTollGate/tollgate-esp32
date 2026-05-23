#include "test_framework.h"
#include "../../components/tollgate_core/src/tollgate_core_mint_health.h"
#include <string.h>
#include <stdio.h>

int main(void)
{
    printf("=== test_mint_health_core ===\n");

    printf("\n--- update: probe success ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;

        tollgate_core_mint_health_update(&state, 0, true, 200, 0, 1000);
        ASSERT_EQ_INT(1, state.mints[0].consecutive_successes, "1st success");
        ASSERT(!state.mints[0].reachable, "not yet reachable (threshold=3)");

        tollgate_core_mint_health_update(&state, 0, true, 200, 0, 2000);
        ASSERT_EQ_INT(2, state.mints[0].consecutive_successes, "2nd success");
        ASSERT(!state.mints[0].reachable, "still not reachable");

        tollgate_core_mint_health_update(&state, 0, true, 200, 0, 3000);
        ASSERT_EQ_INT(3, state.mints[0].consecutive_successes, "3rd success");
        ASSERT(state.mints[0].reachable, "now reachable after threshold");
    }

    printf("\n--- update: failure resets ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;
        state.mints[0].reachable = true;
        state.mints[0].consecutive_successes = 5;

        tollgate_core_mint_health_update(&state, 0, false, 0, -1, 1000);
        ASSERT(!state.mints[0].reachable, "failure marks unreachable");
        ASSERT_EQ_INT(0, state.mints[0].consecutive_successes, "successes reset to 0");
        ASSERT_EQ_INT(-1, state.mints[0].last_err, "error code recorded");
    }

    printf("\n--- update_initial: success ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;

        tollgate_core_mint_health_update_initial(&state, 0, true, 200, 0, 1000);
        ASSERT(state.mints[0].reachable, "immediately reachable on initial success");
        ASSERT_EQ_INT(TG_MINT_HEALTH_RECOVERY_THRESHOLD,
                      state.mints[0].consecutive_successes, "successes set to threshold");
    }

    printf("\n--- update_initial: failure ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;

        tollgate_core_mint_health_update_initial(&state, 0, false, 0, -2, 1000);
        ASSERT(!state.mints[0].reachable, "not reachable on initial failure");
        ASSERT_EQ_INT(0, state.mints[0].consecutive_successes, "successes is 0");
    }

    printf("\n--- is_reachable ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;
        state.mints[0].reachable = true;

        ASSERT(tollgate_core_mint_health_is_reachable(&state, "https://test.mint"),
               "exact URL match");
        ASSERT(tollgate_core_mint_health_is_reachable(&state, "https://test.mint/v1/info"),
               "substring match");
        ASSERT(!tollgate_core_mint_health_is_reachable(&state, "https://other.mint"),
               "no match for other URL");
        ASSERT(!tollgate_core_mint_health_is_reachable(NULL, "https://test.mint"),
               "NULL state");
        ASSERT(!tollgate_core_mint_health_is_reachable(&state, NULL),
               "NULL url");
    }

    printf("\n--- mark_unreachable ---\n");
    {
        tollgate_mint_health_t state = {0};
        strncpy(state.mints[0].url, "https://test.mint", sizeof(state.mints[0].url) - 1);
        state.count = 1;
        state.mints[0].reachable = true;
        state.mints[0].consecutive_successes = 5;

        tollgate_core_mint_health_mark_unreachable(&state, "https://test.mint");
        ASSERT(!state.mints[0].reachable, "marked unreachable");
        ASSERT_EQ_INT(0, state.mints[0].consecutive_successes, "successes cleared");

        tollgate_core_mint_health_mark_unreachable(&state, "https://test.mint");
        ASSERT(!state.mints[0].reachable, "idempotent");
    }

    printf("\n--- count_reachable ---\n");
    {
        tollgate_mint_health_t state = {0};
        state.count = 3;
        strncpy(state.mints[0].url, "https://a.mint", sizeof(state.mints[0].url) - 1);
        strncpy(state.mints[1].url, "https://b.mint", sizeof(state.mints[1].url) - 1);
        strncpy(state.mints[2].url, "https://c.mint", sizeof(state.mints[2].url) - 1);
        state.mints[0].reachable = true;
        state.mints[1].reachable = false;
        state.mints[2].reachable = true;

        ASSERT_EQ_INT(2, tollgate_core_mint_health_count_reachable(&state),
                      "2 out of 3 reachable");
        ASSERT_EQ_INT(0, tollgate_core_mint_health_count_reachable(NULL),
                      "NULL returns 0");
    }

    printf("\n--- update with invalid index ---\n");
    {
        tollgate_mint_health_t state = {0};
        state.count = 1;

        tollgate_core_mint_health_update(&state, -1, true, 200, 0, 1000);
        tollgate_core_mint_health_update(&state, 5, true, 200, 0, 1000);
        tollgate_core_mint_health_update(NULL, 0, true, 200, 0, 1000);
        ASSERT(true, "no crash on invalid inputs");
    }

    TEST_SUMMARY();
}
