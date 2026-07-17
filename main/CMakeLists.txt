# Balloon TollGate — ESP32-C3 stripped source list
# Stripped: display, font, cvm_server, mcp_handler, wifistr, tollgate_client,
#           beacon_price, market, negentropy_adapter, stratum_*, sw_miner,
#           asic_miner, faucet_client, remote_miner, touch, keyboard, wifi_setup,
#           mint_health, tls_worker, lightning_payout, lnurl_pay, nip04, local_relay,
#           relay_selector, sync_manager
# Kept: tollgate_main, config, dns_server, captive_portal, tollgate_api,
#       identity, nostr_event, geohash
idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "tollgate_api.c"
                            "identity.c"
                            "nostr_event.c"
                            "geohash.c"
                            "mint_health.c"
                            "tls_worker.c"
                    INCLUDE_DIRS "."
                     REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                               lwip json esp_http_client mbedtls esp-tls log spiffs
                               nucula_lib secp256k1 qrcode tollgate_esp
                      PRIV_REQUIRES esp-tls)
