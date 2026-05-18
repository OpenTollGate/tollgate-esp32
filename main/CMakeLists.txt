idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "firewall.c"
                            "cashu.c"
                            "session.c"
                            "tollgate_api.c"
                            "identity.c"
                            "nostr_event.c"
                            "geohash.c"
                            "wifistr.c"
                            "tollgate_client.c"
                            "lnurl_pay.c"
                            "lightning_payout.c"
                            "nip04.c"
                            "mcp_handler.c"
                            "cvm_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                             lwip json esp_http_client mbedtls esp-tls log spiffs
                             nucula_lib secp256k1 esp_timer
                    PRIV_REQUIRES esp-tls)
