#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : start_hospital.sh
# Purpose : Initialize IPC resources and launch the
#           admissions manager in the background.
# Usage   : ./start_hospital.sh [--strategy best|first|worst]
# ============================================================

STRATEGY="${2:-best}"
ADMISSIONS_BIN="./admissions"
PID_FILE="/tmp/hospital_admissions.pid"

echo "╔══════════════════════════════════════════════╗"
echo "║   Hospital Patient Triage & Bed Allocator   ║"
echo "║              STARTING SYSTEM                 ║"
echo "╠══════════════════════════════════════════════╣"
echo "║  ICU Beds      : 4  (care units: 3 each)    ║"
echo "║  Isolation Beds: 4  (care units: 2 each)    ║"
echo "║  General Beds  : 12 (care units: 1 each)    ║"
echo "║  Total Units   : 32                         ║"
echo "║  Strategy      : $STRATEGY                        ║"
echo "╚══════════════════════════════════════════════╝"

# ── Check binary exists ──────────────────────────────────────
if [ ! -f "$ADMISSIONS_BIN" ]; then
    echo "[ERROR] admissions binary not found. Run 'make all' first."
    exit 1
fi

# ── Clean up any leftover IPC from previous run ──────────────
echo "[STARTUP] Cleaning up any stale IPC resources..."
ipcrm -M 0xBEDF00D 2>/dev/null || true
sem_unlink_names=("/sem_icu_limit" "/sem_isolation_limit" "/sem_queue_slots" "/sem_queue_items")
for s in "${sem_unlink_names[@]}"; do
    # Try to unlink via a small C snippet — skip silently if not found
    python3 -c "
import ctypes, ctypes.util
lib = ctypes.CDLL(ctypes.util.find_library('c'))
lib.sem_unlink('$s')
" 2>/dev/null || true
done
rm -f /tmp/discharge_fifo

# ── Create logs directory ────────────────────────────────────
mkdir -p logs

# ── Launch admissions manager ────────────────────────────────
echo "[STARTUP] Launching admissions manager (strategy: $STRATEGY)..."
"$ADMISSIONS_BIN" --strategy "$STRATEGY" &
ADMISSIONS_PID=$!
echo $ADMISSIONS_PID > "$PID_FILE"

echo "[STARTUP] Admissions manager running with PID $ADMISSIONS_PID"
echo "[STARTUP] PID saved to $PID_FILE"
echo ""
echo "[STARTUP] System ready. Use one of:"
echo "   Interactive : type directly into the terminal"
echo "   Via triage  : ./scripts/triage.sh <name> <age> <severity> | ./admissions"
echo "   Stress test : ./scripts/stress_test.sh"
echo ""
echo "[STARTUP] To stop: ./scripts/stop_hospital.sh"

wait $ADMISSIONS_PID
echo "[STARTUP] Admissions manager exited."
