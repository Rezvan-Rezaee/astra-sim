#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(dirname "$(realpath "$0")")
BUILD_DIR="${SCRIPT_DIR}/build"

SYSTEMC_SWITCH_ROOT="${SCRIPT_DIR}/../../ParaDOX/cpp/systemC"

if [[ -z "${SYSTEMC_SWITCH_ROOT}" ]]; then
    echo "Set SYSTEMC_SWITCH_ROOT inside build.sh"
    exit 1
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. \
  -DSYSTEMC_SWITCH_ROOT="${SYSTEMC_SWITCH_ROOT}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS_DEBUG="-fsanitize=address"

cmake --build . -j 4
# -j "$(nproc)" tells the build system to compile using as many parallel jobs as your machine has CPU cores.
# This overwhelemed memory and crashed build on my machine, which has 16 cores and 32GB of RAM. AddressSanitizer can consume a lot of memory, especially when compiling large codebases,
# so we are limiting the number of jobs/processes to 2 to avoid overwhelming the system, especially if it has a large number of cores.