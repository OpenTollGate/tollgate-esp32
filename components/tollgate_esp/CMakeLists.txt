idf_component_register(
    SRCS "src/tollgate_esp_platform.c"
    INCLUDE_DIRS "."
    REQUIRES json mbedtls lwip esp_http_client esp_wifi esp_netif nvs_flash log freertos nucula_lib tollgate_core
    PRIV_INCLUDE_DIRS "../../main"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-format)
