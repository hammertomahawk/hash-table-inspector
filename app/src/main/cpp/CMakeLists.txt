cmake_minimum_required(VERSION 3.22.1)

project(hash_table_inspector LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(HTI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

add_library(
    hash_table_inspector
    SHARED
    "${HTI_ROOT}/native/hash_table/src/hash_table.c"
    "${HTI_ROOT}/native/inspection/src/inspection_timeline.c"
    "${HTI_ROOT}/native/adapters/hash_table/src/hash_snapshot.c"
    "${HTI_ROOT}/native/adapters/hash_table/src/hash_inspector.c"
    "${HTI_ROOT}/native/adapters/hash_table/src/hash_json.c"
    "${HTI_ROOT}/native/jni/hash_inspector_jni.c")

target_include_directories(
    hash_table_inspector
    PRIVATE
    "${HTI_ROOT}/native/hash_table/include"
    "${HTI_ROOT}/native/hash_table/src"
    "${HTI_ROOT}/native/inspection/include"
    "${HTI_ROOT}/native/adapters/hash_table/include"
    "${HTI_ROOT}/native/adapters/hash_table/src")

target_compile_definitions(
    hash_table_inspector
    PRIVATE
    HT_ENABLE_INSPECTION=1
    HT_ENABLE_CORRUPTION=1)

target_compile_options(
    hash_table_inspector
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wformat
    -Wformat-security
    -Wundef
    -Wpointer-arith
    -Wwrite-strings
    -Wvla)
