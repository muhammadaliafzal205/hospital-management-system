#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stop_hospital.sh
# Purpose : Gracefully shut down the hospital system,
#           clean up all IPC resources, print summary.
# Usage   : ./stop_hospital.sh
# ============================================================

PID_FILE="/tmp/hospital_admissions.pid"

echo "╔══════════════════════════════════════════════╗"
echo "║         SHUTTING DOWN HOSPITAL SYSTEM        ║"
echo "╚══════════════════════════════════════════════╝"

# ── Send SIGTERM to admissions process ───────────────────────
if [ -f "$PID_FILE" ]; then
    ADMISSIONS_PID=$(cat "$PID_FILE")
    if kill -0 "$ADMISSIONS_PID" 2>/dev/null; then
        echo "[SHUTDOWN] Sending SIGTERM to admissions (PID $ADMISSIONS_PID)..."
        kill -SIGTERM "$ADMISSIONS_PID"
        sleep 2
        # Force kill if still alive
        if kill -0 "$ADMISSIONS_PID" 2>/dev/null; then
            echo "[SHUTDOWN] Process still alive — sending SIGKILL..."
            kill -SIGKILL "$ADMISSIONS_PID" 2>/dev/null
        fi
        echo "[SHUTDOWN] Admissions manager stopped."
    else
        echo "[SHUTDOWN] Admissions process (PID $ADMISSIONS_PID) not running."
    fi
    rm -f "$PID_FILE"
else
    echo "[SHUTDOWN] No PID file found. Attempting to find process..."
    pkill -f "admissions" 2>/dev/null && echo "[SHUTDOWN] Killed admissions process."
fi

# ── Kill any remaining patient_simulator children ───────────
echo "[SHUTDOWN] Killing any patient_simulator processes..."
pkill -f "patient_simulator" 2>/dev/null && echo "[SHUTDOWN] Patient simulators stopped." || true

# ── Clean up shared memory ───────────────────────────────────
echo "[SHUTDOWN] Removing shared memory segment (key 0xBEDF00D)..."
ipcrm -M 0xBEDF00D 2>/dev/null && echo "[SHUTDOWN] Shared memory removed." || echo "[SHUTDOWN] Shared memory already clean."

# ── Remove named semaphores ──────────────────────────────────
echo "[SHUTDOWN] Unlinking named semaphores..."
for sem in /sem_icu_limit /sem_isolation_limit /sem_queue_slots /sem_queue_items; do
    python3 -c "
import ctypes, ctypes.util
lib = ctypes.CDLL(ctypes.util.find_library('c'))
ret = lib.sem_unlink('$sem')
" 2>/dev/null || true
done
echo "[SHUTDOWN] Semaphores cleaned."

# ── Remove named FIFO ────────────────────────────────────────
rm -f /tmp/discharge_fifo
echo "[SHUTDOWN] Named FIFO removed."

# ── Print final summary ──────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║              FINAL WARD SUMMARY              ║"
echo "╠══════════════════════════════════════════════╣"

if [ -f "logs/memory_log.txt" ]; then
    EVENTS=$(wc -l < logs/memory_log.txt)
    echo "║  Memory log events    : $EVENTS"
fi
if [ -f "logs/schedule_log.txt" ]; then
    echo "║  Schedule log         : logs/schedule_log.txt"
fi

echo "║  All IPC resources    : CLEANED"
echo "║  System status        : OFFLINE"
echo "╚══════════════════════════════════════════════╝"
echo ""
echo "[SHUTDOWN] Hospital system stopped successfully."
