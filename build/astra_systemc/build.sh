#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
BUILD_DIR="${SCRIPT_DIR}/build"

SYSTEMC_SWITCH_ROOT="${SCRIPT_DIR}/../../ParaDOX/cpp/systemC"

if [[ -z "${SYSTEMC_SWITCH_ROOT}" ]]; then
    echo "Set SYSTEMC_SWITCH_ROOT inside build.sh"
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. \
  -DSYSTEMC_SWITCH_ROOT="${SYSTEMC_SWITCH_ROOT}"

cmake --build . -j "$(nproc)"