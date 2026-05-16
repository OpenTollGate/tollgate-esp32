idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "firewall.c"
                            "cashu.c"
                            "session.c"
                            "tollgate_api.c"
                             "wallet.c"
                            "wallet_persist.c"
                    INCLUDE_DIRS "." "${IDF_PATH}/components/lwip/include/apps"
                    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                             lwip json esp_http_client mbedtls esp-tls log spiffs
                    PRIV_REQUIRES esp-tls)
