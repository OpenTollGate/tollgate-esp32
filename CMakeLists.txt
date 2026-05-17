cmake_minimum_required(VERSION 3.16)
set(PARTITION_CSV_PATH "${CMAKE_SOURCE_DIR}/partitions.csv")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-tollgate)

idf_component_get_property(lwip lwip COMPONENT_LIB)
target_compile_options(${lwip} PRIVATE "-I${PROJECT_DIR}/main")
target_compile_definitions(${lwip} PRIVATE "-DESP_IDF_LWIP_HOOK_FILENAME=\"lwip_tollgate_hooks.h\"")
