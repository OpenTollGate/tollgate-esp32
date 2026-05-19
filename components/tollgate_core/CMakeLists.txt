idf_component_register(
    SRCS "src/tollgate_core.c"
         "src/tollgate_core_cashu.c"
         "src/tollgate_core_dns.c"
         "src/tollgate_core_firewall.c"
         "src/tollgate_core_session.c"
         "src/tollgate_core_mining.c"
         "src/tollgate_core_stratum_proxy.c"
    INCLUDE_DIRS "include" "src"
    REQUIRES lwip json esp_http_client mbedtls log esp_netif
    PRIV_REQUIRES esp_wifi)
