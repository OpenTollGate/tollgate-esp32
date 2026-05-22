cmake_minimum_required(VERSION 3.16)
project(tollgate_core C)

set(TG_CORE_SOURCES
    src/tollgate_core.c
    src/tollgate_cashu.c
    src/tollgate_session.c
    src/tollgate_mining.c
)

add_library(tollgate_core STATIC ${TG_CORE_SOURCES})

target_include_directories(tollgate_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)

target_include_directories(tollgate_core PRIVATE
    ${CJSON_INCLUDE_DIRS}
    /usr/include
)

target_link_libraries(tollgate_core PUBLIC mbedcrypto cjson m)

target_compile_options(tollgate_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
