#include "stratum_proxy.h"
#include "tollgate_core_stratum_proxy.h"
#include <string.h>

_Static_assert(sizeof(stratum_job_t) == sizeof(tollgate_stratum_job_t), "job struct size mismatch");
_Static_assert(sizeof(stratum_proxy_stats_t) == sizeof(tollgate_stratum_proxy_stats_t), "stats struct size mismatch");

esp_err_t stratum_proxy_init(uint16_t port, bool self_test)
{
    return tollgate_core_stratum_proxy_init(port, self_test);
}

void stratum_proxy_set_job(const stratum_job_t *job)
{
    tollgate_core_stratum_proxy_set_job((const tollgate_stratum_job_t *)job);
}

const stratum_job_t *stratum_proxy_get_current_job(void)
{
    return (const stratum_job_t *)tollgate_core_stratum_proxy_get_current_job();
}

void stratum_proxy_get_stats(stratum_proxy_stats_t *stats)
{
    tollgate_core_stratum_proxy_get_stats((tollgate_stratum_proxy_stats_t *)stats);
}

void stratum_proxy_stop(void)
{
    tollgate_core_stratum_proxy_stop();
}
