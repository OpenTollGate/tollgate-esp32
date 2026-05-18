idf_component_register(
    SRCS "ws_server.c"
         "storage_engine.c"
         "sub_manager.c"
         "broadcaster.c"
         "rate_limiter.c"
         "nip11_relay.c"
         "deletion.c"
         "flash_monitor.c"
         "relay_validator.c"
         "router.c"
         "handlers.c"
         "relay_types.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_timer nvs_flash log json esp_littlefs mbedtls secp256k1
)
