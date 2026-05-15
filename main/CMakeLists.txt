idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "firewall.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                             lwip json esp_http_client esp_tls log
                    PRIV_REQUIRES lwip)
