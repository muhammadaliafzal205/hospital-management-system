#!/bin/bash
# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# Script  : stress_test.sh
# Purpose : Spawn 20 patient arrivals in rapid succession
#           to stress-test the system.
# Usage   : ./scripts/stress_test.sh | ./admissions
# ============================================================

NAMES=("Alice" "Bob" "Carol" "David" "Eve" "Frank" "Grace"
       "Hank" "Iris" "Jack" "Karen" "Leo" "Mia" "Nora"
       "Oscar" "Pam" "Quinn" "Ray" "Sara" "Tom")

echo "[STRESS] Starting 20-patient rapid admission stress test..."
echo ""

for i in $(seq 0 19); do
    NAME="${NAMES[$i]}"
    AGE=$(( RANDOM % 80 + 1 ))
    SEVERITY=$(( RANDOM % 10 + 1 ))
    echo "$NAME $AGE $SEVERITY"
    echo "[STRESS] Sent: $NAME age=$AGE severity=$SEVERITY" >&2
    sleep 0.3
done

echo "[STRESS] All 20 patients submitted." >&2
sleep 2
echo "quit"
