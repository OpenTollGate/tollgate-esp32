idf_component_register(
    SRCS "src/tollgate_esp_platform.c"
    INCLUDE_DIRS "."
    REQUIRES json mbedtls lwip esp_http_client esp_wifi esp_netif nvs_flash log freertos nucula_lib
    PRIV_INCLUDE_DIRS "../../tollgate_core/include" "../../tollgate_core/src" "../../main"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-format)
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tollgate_core/src/tollgate_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tollgate_core/src/tollgate_cashu.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tollgate_core/src/tollgate_session.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tollgate_core/src/tollgate_firewall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tollgate_core/src/tollgate_mining.c"
)
