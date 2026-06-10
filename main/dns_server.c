#include "dns_server.h"
#include "tollgate_core_dns.h"

esp_err_t dns_server_start(esp_ip4_addr_t ap_ip, esp_ip4_addr_t upstream_dns)
{
    return tollgate_core_dns_start_internal(ap_ip, upstream_dns);
}

void dns_server_stop(void)
{
    tollgate_core_dns_stop();
}

void dns_server_set_client_authenticated(uint32_t client_ip, bool authenticated)
{
    tollgate_core_dns_set_authenticated(client_ip, authenticated);
}

bool dns_server_is_running(void)
{
    return tollgate_core_dns_is_running();
}
