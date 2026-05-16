set(NUCULA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../nucula_src/main)

idf_component_register(
    SRCS "nucula_wallet.cpp"
         "${NUCULA_SRC}/crypto.c"
         "${NUCULA_SRC}/wallet.cpp"
         "${NUCULA_SRC}/cashu_json.cpp"
         "${NUCULA_SRC}/nut10.cpp"
         "${NUCULA_SRC}/hex.c"
         "${NUCULA_SRC}/http.c"
    INCLUDE_DIRS "."
                 "${NUCULA_SRC}"
    REQUIRES secp256k1
    PRIV_REQUIRES log mbedtls nvs_flash esp_http_client json
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
