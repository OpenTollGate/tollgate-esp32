set(NEGENTROPY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../negentropy)

idf_component_register(
    SRCS "${NEGENTROPY_DIR}/c/negentropy_wrapper.cpp"
    INCLUDE_DIRS
        "${NEGENTROPY_DIR}/c"
        "${NEGENTROPY_DIR}/cpp"
        "compat"
    REQUIRES mbedtls
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -fexceptions
    -frtti
    -Wno-error
    -Wno-delete-non-virtual-dtor
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-catch-value
)
