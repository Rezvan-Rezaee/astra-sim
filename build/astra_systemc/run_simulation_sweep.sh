#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTRA_SIM_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Suppress known-benign protobuf global descriptor pool allocations so they
# don't appear as LeakSanitizer false positives.
export LSAN_OPTIONS="suppressions=${SCRIPT_DIR}/lsan_suppressions.txt"

mkdir -p test_log

NPORTS_LIST=(64)
COM_TYPE_LIST=(2)  # 0: all_reduce, 1: all_gather, 2: all_to_all
WORKLOAD_SIZE_MB_LIST=(1)  # size suffix of the trace dir, e.g. <nports>npus_<size>MB

BYTES_PER_CELL=80
EXECUTABLE="build/bin/AstraSim_SystemC"

com_type_name() {
    case "$1" in
        0) echo "all_reduce" ;;
        1) echo "all_gather" ;;
        2) echo "all_to_all" ;;
        *) echo "Unknown com_type: $1" >&2; exit 1 ;;
    esac
}

for nports in "${NPORTS_LIST[@]}"; do
    for com_type in "${COM_TYPE_LIST[@]}"; do
        com_type_str="$(com_type_name "${com_type}")"

        for size_mb in "${WORKLOAD_SIZE_MB_LIST[@]}"; do

            workload_base="${ASTRA_SIM_ROOT}/examples/workload/microbenchmarks/${com_type_str}/${nports}npus_${size_mb}MB/${com_type_str}"

            if [ ! -f "${workload_base}.0.et" ]; then
                echo "Skipping: no workload trace at ${workload_base}.0.et"
                echo
                continue
            fi

            log_file="test_log/d/debug_output_nports${nports}_bytes${BYTES_PER_CELL}_com${com_type_str}_size${size_mb}MB.txt"

            echo "Running: ${EXECUTABLE} ${nports} ${BYTES_PER_CELL} ${workload_base} ${SCRIPT_DIR}"
            echo "Log: ${log_file}"

            "${EXECUTABLE}" "${nports}" "${BYTES_PER_CELL}" "${workload_base}" "${SCRIPT_DIR}" \
                > "${log_file}" 2>&1

            echo "Finished: nports=${nports}, com_type=${com_type_str}, size_mb=${size_mb}"
            echo
        done
    done
done

echo "All runs completed."