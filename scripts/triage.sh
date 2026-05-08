#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
# Purpose : Compute triage priority and pipe patient data
#           to the admissions manager process.
# Usage   : ./triage.sh <name> <age> <severity 1-10>
# ============================================================

# ── Input validation ────────────────────────────────────────
if [ "$#" -ne 3 ]; then
    echo "[ERROR] Usage: $0 <name> <age> <severity 1-10>"
    exit 1
fi

NAME="$1"
AGE="$2"
SEVERITY="$3"

if [ -z "$NAME" ]; then
    echo "[ERROR] Patient name cannot be empty."
    exit 1
fi

if ! [[ "$AGE" =~ ^[0-9]+$ ]]; then
    echo "[ERROR] Age must be a positive integer."
    exit 1
fi

if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]]; then
    echo "[ERROR] Severity must be a number."
    exit 1
fi

if [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    echo "[ERROR] Severity must be between 1 and 10."
    exit 1
fi

if [ "$AGE" -lt 0 ] || [ "$AGE" -gt 150 ]; then
    echo "[ERROR] Age must be between 0 and 150."
    exit 1
fi

# ── Compute triage priority (1=critical … 5=non-urgent) ─────
if   [ "$SEVERITY" -le 2 ]; then PRIORITY=1; LABEL="CRITICAL"
elif [ "$SEVERITY" -le 4 ]; then PRIORITY=2; LABEL="URGENT"
elif [ "$SEVERITY" -le 6 ]; then PRIORITY=3; LABEL="MODERATE"
elif [ "$SEVERITY" -le 8 ]; then PRIORITY=4; LABEL="MINOR"
else                              PRIORITY=5; LABEL="NON-URGENT"
fi

# ── Print formatted record ───────────────────────────────────
echo "============================================"
echo "  TRIAGE ASSESSMENT"
echo "  Patient : $NAME"
echo "  Age     : $AGE"
echo "  Severity: $SEVERITY/10"
echo "  Priority: $PRIORITY ($LABEL)"
echo "============================================"

# ── Pipe record to admissions manager via stdin ──────────────
# Format expected by receptionist thread: <name> <age> <severity>
echo "$NAME $AGE $SEVERITY"
