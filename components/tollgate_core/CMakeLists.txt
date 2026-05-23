idf_component_register(
    SRCS "src/tollgate_core.c"
         "src/tollgate_core_cashu.c"
         "src/tollgate_core_dns.c"
         "src/tollgate_core_firewall.c"
         "src/tollgate_core_session.c"
         "src/tollgate_core_mining.c"
         "src/tollgate_core_stratum_proxy.c"
         "src/tollgate_core_beacon.c"
         "src/tollgate_core_market.c"
         "src/tollgate_core_portal.c"
         "src/tollgate_core_stratum_client.c"
         "src/tollgate_core_mint_health.c"
         "src/tollgate_core_client.c"
    INCLUDE_DIRS "include" "src"
    REQUIRES lwip json esp_http_client mbedtls log esp_netif
    PRIV_REQUIRES esp_wifi)
