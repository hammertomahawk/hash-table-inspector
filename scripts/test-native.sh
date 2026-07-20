#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/hash-table-inspector-native"
CC_BIN="${CC:-clang}"
TEST_BINARY="${BUILD_DIR}/hash-table-inspector-tests"
ASAN_DETECT_LEAKS=1

if [[ "$(uname -s)" == "Darwin" ]]; then
    ASAN_DETECT_LEAKS=0
fi

if ! command -v "${CC_BIN}" >/dev/null 2>&1; then
    echo "C compiler not found: ${CC_BIN}" >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"

SOURCES=(
    "${ROOT_DIR}/native/hash_table/src/hash_table.c"
    "${ROOT_DIR}/native/inspection/src/inspection_timeline.c"
    "${ROOT_DIR}/native/adapters/hash_table/src/hash_snapshot.c"
    "${ROOT_DIR}/native/adapters/hash_table/src/hash_inspector.c"
    "${ROOT_DIR}/native/adapters/hash_table/src/hash_json.c"
    "${ROOT_DIR}/native/tests/test_main.c"
)

INCLUDES=(
    -I "${ROOT_DIR}/native/hash_table/include"
    -I "${ROOT_DIR}/native/hash_table/src"
    -I "${ROOT_DIR}/native/inspection/include"
    -I "${ROOT_DIR}/native/adapters/hash_table/include"
    -I "${ROOT_DIR}/native/adapters/hash_table/src"
)

WARNINGS=(
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
    -Wvla
)

"${CC_BIN}" \
    -std=c11 \
    -O1 \
    -g \
    -DHT_ENABLE_INSPECTION=1 \
    -DHT_ENABLE_CORRUPTION=1 \
    "${WARNINGS[@]}" \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "${INCLUDES[@]}" \
    "${SOURCES[@]}" \
    -o "${TEST_BINARY}"

ASAN_OPTIONS="detect_leaks=${ASAN_DETECT_LEAKS}:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
"${TEST_BINARY}"
