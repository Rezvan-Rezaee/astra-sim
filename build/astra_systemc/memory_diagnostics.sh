#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# User-controlled variables
# ============================================================

PROJECT_ROOT_DIR="/mnt/c/Users/rezva/Documents/paradox/astra-sim"
EXECUTABLE="${PROJECT_ROOT_DIR}/build/astra_systemc/build/bin/AstraSim_SystemC"

NPORTS=64
BYTES_PER_CELL=800

# 0 = all_reduce, 1 = all_gather, 2 = all_to_all
COM_TYPE=0

# Your fourth argument.
# In your current code this is named communication_scale.
# If you repurposed it as coll_size_mb, keep the value here.
COMM_SIZE_OR_SCALE=64

LOG_DIR="${PROJECT_ROOT_DIR}/build/astra_systemc/diagnostic_logs"

# Optional extra arguments passed to executable
EXTRA_ARGS=()

# Increase stack size. Use "unlimited" if you strongly suspect stack/coroutine issues.
STACK_LIMIT="unlimited"

# Increase VMA limit. Helps with SystemC/coroutine-heavy simulations.
VM_MAX_MAP_COUNT=262144

# Monitoring interval in seconds
SLEEP_INTERVAL=1

# Kill older matching processes before starting
KILL_OLD=0

# ============================================================
# Setup
# ============================================================

mkdir -p "${LOG_DIR}"

timestamp=$(date +"%Y%m%d_%H%M%S")

RUN_NAME="n${NPORTS}_cell${BYTES_PER_CELL}_com${COM_TYPE}_size${COMM_SIZE_OR_SCALE}_${timestamp}"
OUT_LOG="${LOG_DIR}/output_${RUN_NAME}.txt"
MON_LOG="${LOG_DIR}/monitor_${RUN_NAME}.txt"

echo "[diagnostic] PROJECT_ROOT_DIR=${PROJECT_ROOT_DIR}"
echo "[diagnostic] EXECUTABLE=${EXECUTABLE}"
echo "[diagnostic] OUT_LOG=${OUT_LOG}"
echo "[diagnostic] MON_LOG=${MON_LOG}"

if [ ! -x "${EXECUTABLE}" ]; then
    echo "[ERROR] Executable not found or not executable: ${EXECUTABLE}"
    exit 1
fi

if [ "${KILL_OLD}" -eq 1 ]; then
    pkill -f "${EXECUTABLE}" || true
fi

# Stack limit
echo "[diagnostic] Setting stack limit: ${STACK_LIMIT}"
ulimit -s "${STACK_LIMIT}" || true

echo "[diagnostic] Current ulimit -s: $(ulimit -s)"

# VMA limit
if command -v sudo >/dev/null 2>&1; then
    echo "[diagnostic] Setting vm.max_map_count=${VM_MAX_MAP_COUNT}"
    sudo sysctl -w vm.max_map_count="${VM_MAX_MAP_COUNT}" >/dev/null || true
fi

echo "[diagnostic] Current vm.max_map_count: $(cat /proc/sys/vm/max_map_count)"

# ============================================================
# Launch
# ============================================================

cd "${PROJECT_ROOT_DIR}"

CMD=(
    "${EXECUTABLE}"
    "${NPORTS}"
    "${BYTES_PER_CELL}"
    "${COM_TYPE}"
    "${COMM_SIZE_OR_SCALE}"
    "${EXTRA_ARGS[@]}"
)

echo "[diagnostic] Running command:" | tee -a "${MON_LOG}"
printf ' %q' "${CMD[@]}" | tee -a "${MON_LOG}"
echo | tee -a "${MON_LOG}"

"${CMD[@]}" > "${OUT_LOG}" 2>&1 &
PID=$!

echo "[diagnostic] PID=${PID}" | tee -a "${MON_LOG}"

# ============================================================
# Helpers
# ============================================================

read_proc_stat_total() {
    awk '/^cpu / {print $2+$3+$4+$5+$6+$7+$8+$9+$10}' /proc/stat
}

read_pid_jiffies() {
    awk '{print $14+$15}' "/proc/${PID}/stat" 2>/dev/null || echo ""
}

get_status_field() {
    local field="$1"
    awk -v f="${field}" '$1 == f ":" {print $2}' "/proc/${PID}/status" 2>/dev/null || echo "NA"
}

get_thread_stack_summary() {
    local total=0
    local max=0
    local count=0

    for status_file in /proc/"${PID}"/task/*/status; do
        [ -f "${status_file}" ] || continue
        val=$(awk '/^VmStk:/ {print $2}' "${status_file}" 2>/dev/null || echo 0)
        val=${val:-0}
        total=$((total + val))
        if [ "${val}" -gt "${max}" ]; then
            max="${val}"
        fi
        count=$((count + 1))
    done

    echo "ThreadVmStkTotal=${total}kB ThreadVmStkMax=${max}kB ThreadCountSeen=${count}"
}

prev_total=$(read_proc_stat_total)
prev_pid=$(read_pid_jiffies)
i=0

# ============================================================
# Monitor loop
# ============================================================

while kill -0 "${PID}" 2>/dev/null; do
    ts=$(date +"%H:%M:%S")

    vmas=$(wc -l < "/proc/${PID}/maps" 2>/dev/null || echo "NA")
    rss_kb=$(get_status_field "VmRSS")
    vmsize_kb=$(get_status_field "VmSize")
    vmstk_kb=$(get_status_field "VmStk")
    threads=$(get_status_field "Threads")
    stack_maps=$(grep -c '\[stack' "/proc/${PID}/maps" 2>/dev/null || echo "NA")

    total_now=$(read_proc_stat_total)
    pid_now=$(read_pid_jiffies)

    cpu_pct="NA"
    if [ -n "${prev_total}" ] && [ -n "${prev_pid}" ] && \
       [ -n "${total_now}" ] && [ -n "${pid_now}" ]; then
        dt=$((total_now - prev_total))
        dp=$((pid_now - prev_pid))
        if [ "${dt}" -gt 0 ]; then
            cpu_pct=$(awk -v dp="${dp}" -v dt="${dt}" 'BEGIN {printf "%.1f", (100.0 * dp / dt)}')
        fi
    fi

    prev_total=${total_now}
    prev_pid=${pid_now}

    thread_stack_summary=$(get_thread_stack_summary)

    map_summary=""
    if [ $((i % 5)) -eq 0 ]; then
        anon=$(awk 'NF < 6 {c++} END {print c+0}' "/proc/${PID}/maps" 2>/dev/null || echo "NA")
        named=$(awk 'NF >= 6 {c++} END {print c+0}' "/proc/${PID}/maps" 2>/dev/null || echo "NA")
        top_names=$(awk 'NF >= 6 {print $6}' "/proc/${PID}/maps" 2>/dev/null \
            | sort | uniq -c | sort -nr | head -5 | tr '\n' ';' || true)
        map_summary=" anon=${anon} named=${named} top=[${top_names}]"
    fi

    echo "[$ts] PID=${PID} VMAs=${vmas} Threads=${threads} StackMaps=${stack_maps} VmRSS=${rss_kb}kB VmSize=${vmsize_kb}kB VmStk=${vmstk_kb}kB CPU%=${cpu_pct} ${thread_stack_summary}${map_summary}" \
        | tee -a "${MON_LOG}"

    i=$((i + 1))
    sleep "${SLEEP_INTERVAL}"
done

# ============================================================
# Exit status
# ============================================================

set +e
wait "${PID}"
EXIT_CODE=$?
set -e

echo "[diagnostic] Process exited with code ${EXIT_CODE}" | tee -a "${MON_LOG}"

echo "[diagnostic] Last 40 lines of output log:" | tee -a "${MON_LOG}"
tail -40 "${OUT_LOG}" | tee -a "${MON_LOG}"

echo "[diagnostic] Logs saved:"
echo "  Output:  ${OUT_LOG}"
echo "  Monitor: ${MON_LOG}"

exit "${EXIT_CODE}"