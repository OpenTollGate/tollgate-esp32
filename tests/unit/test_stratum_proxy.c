#include "test_framework.h"
#include "../../main/stratum_proxy.h"
#include "../../main/mining_payment.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== test_stratum_proxy ===\n");

    mining_payment_init();

    printf("\n--- stratum_proxy_set_job / get_current_job ---\n");
    {
        stratum_job_t job = {0};
        job.job_id = 42;
        job.nbits = 0x170309E2;
        job.ntime = 0x6789ABCD;
        job.version = 0x20000000;
        job.valid = true;
        memset(job.prevhash, 0xAA, 32);
        memset(job.merkle_root, 0xBB, 32);

        stratum_proxy_set_job(&job);

        const stratum_job_t *cur = stratum_proxy_get_current_job();
        ASSERT(cur != NULL, "current job not NULL");
        ASSERT_EQ_INT(42, (int)cur->job_id, "job_id=42");
        ASSERT_EQ_INT(0x170309E2, (int)cur->nbits, "nbits preserved");
        ASSERT_EQ_INT(0x6789ABCD, (int)cur->ntime, "ntime preserved");
        ASSERT_EQ_INT(0x20000000, (int)cur->version, "version preserved");
        ASSERT(cur->valid, "job is valid");
        ASSERT_MEM_EQ(job.prevhash, cur->prevhash, 32, "prevhash preserved");
    }

    printf("\n--- stratum_proxy_set_job (NULL) ---\n");
    {
        stratum_proxy_set_job(NULL);
        const stratum_job_t *cur = stratum_proxy_get_current_job();
        ASSERT_EQ_INT(42, (int)cur->job_id, "job unchanged after NULL set");
    }

    printf("\n--- stratum_proxy_get_stats ---\n");
    {
        mining_set_current_nbits(0x170309E2);
        stratum_proxy_set_job(&(stratum_job_t){
            .job_id = 1,
            .nbits = 0x170309E2,
            .valid = true
        });

        stratum_proxy_stats_t stats;
        memset(&stats, 0xFF, sizeof(stats));
        stratum_proxy_get_stats(&stats);

        ASSERT(stats.current_hashprice > 0.0, "hashprice populated from mining_payment");
        ASSERT_EQ_INT(0x170309E2, (int)stats.nbits, "nbits in stats");
    }

    printf("\n--- stratum_proxy_get_stats (NULL) ---\n");
    {
        stratum_proxy_get_stats(NULL);
        ASSERT(true, "get_stats with NULL does not crash");
    }

    printf("\n--- stratum_job_t initialization ---\n");
    {
        stratum_job_t zero = {0};
        ASSERT(!zero.valid, "zero-initialized job is invalid");
        ASSERT_EQ_INT(0, (int)zero.job_id, "zero job_id");
        ASSERT_EQ_INT(0, (int)zero.nbits, "zero nbits");
    }

    printf("\n--- stratum_proxy_stats_t initialization ---\n");
    {
        stratum_proxy_stats_t zero = {0};
        ASSERT(zero.hashrate_ghs == 0.0, "zero hashrate");
        ASSERT_EQ_INT(0, (int)zero.active_miners, "zero active miners");
        ASSERT_EQ_UINT64(0, zero.total_shares, "zero total shares");
        ASSERT_EQ_UINT64(0, zero.total_accepted, "zero total accepted");
        ASSERT_EQ_UINT64(0, zero.total_rejected, "zero total rejected");
    }

    printf("\n--- STRATUM_MAX_JOBS constant ---\n");
    {
        ASSERT(STRATUM_MAX_JOBS >= 1, "STRATUM_MAX_JOBS >= 1");
    }

    printf("\n--- STRATUM_MAX_JOB_ID_LEN constant ---\n");
    {
        ASSERT(STRATUM_MAX_JOB_ID_LEN >= 16, "STRATUM_MAX_JOB_ID_LEN >= 16");
    }

    TEST_SUMMARY();
}
