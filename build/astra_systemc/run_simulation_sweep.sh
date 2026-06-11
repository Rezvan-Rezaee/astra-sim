#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Suppress known-benign protobuf global descriptor pool allocations so they
# don't appear as LeakSanitizer false positives.
export LSAN_OPTIONS="suppressions=${SCRIPT_DIR}/lsan_suppressions.txt"

mkdir -p test_log

NPORTS_LIST=(64)
COM_TYPE_LIST=(0)  # 0: all_reduce, 1: all_gather, 2: all_to_all
COMM_SCALE_LIST=(1)

BYTES_PER_CELL=80
EXECUTABLE="build/bin/AstraSim_SystemC"

for nports in "${NPORTS_LIST[@]}"; do
    for com_type in "${COM_TYPE_LIST[@]}"; do
        for comm_scale in "${COMM_SCALE_LIST[@]}"; do

            log_file="test_log/debug_output_nports${nports}_bytes${BYTES_PER_CELL}_com${com_type}_scale${comm_scale}.txt"

            echo "Running: ${EXECUTABLE} ${nports} ${BYTES_PER_CELL} ${com_type} ${comm_scale}"
            echo "Log: ${log_file}"

            "${EXECUTABLE}" "${nports}" "${BYTES_PER_CELL}" "${com_type}" "${comm_scale}" \
                > "${log_file}" 2>&1

            echo "Finished: nports=${nports}, com_type=${com_type}, comm_scale=${comm_scale}"
            echo
        done
    done
done

echo "All runs completed."