# ============================================================
# Project : Hospital Patient Triage & Bed Allocator
# File    : Makefile
# Purpose : Build system for all project components
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
SRCDIR  = src
BINDIR  = .

ADMISSIONS_SRCS = $(SRCDIR)/admissions.c \
                  $(SRCDIR)/bed_allocator.c \
                  $(SRCDIR)/scheduler.c

PATIENT_SRCS    = $(SRCDIR)/patient_simulator.c

ADMISSIONS_BIN  = $(BINDIR)/admissions
PATIENT_BIN     = $(BINDIR)/patient_simulator

.PHONY: all clean run test stress help

# ── Default target ───────────────────────────────────────────
all: $(ADMISSIONS_BIN) $(PATIENT_BIN)
	@echo ""
	@echo "✔  Build complete."
	@echo "   Run with:  make run"
	@echo "   Test with: make test"
	@echo ""

$(ADMISSIONS_BIN): $(ADMISSIONS_SRCS) $(SRCDIR)/hospital.h \
                    $(SRCDIR)/bed_allocator.h $(SRCDIR)/scheduler.h
	@mkdir -p logs
	$(CC) $(CFLAGS) -o $@ $(ADMISSIONS_SRCS) -lpthread
	@echo "✔  admissions built"

$(PATIENT_BIN): $(PATIENT_SRCS) $(SRCDIR)/hospital.h
	$(CC) $(CFLAGS) -o $@ $(PATIENT_SRCS) -lpthread
	@echo "✔  patient_simulator built"

# ── Run: interactive mode (Best-Fit) ────────────────────────
run: all
	@mkdir -p logs
	@chmod +x scripts/*.sh
	@echo "Starting hospital (Best-Fit)..."
	@echo "Type: <name> <age> <severity>  then Enter"
	@echo "Type: quit                     to exit"
	@echo ""
	./admissions --strategy best

# ── Test: run a scripted 5-patient scenario ──────────────────
test: all
	@mkdir -p logs
	@chmod +x scripts/*.sh
	@echo "Running 5-patient test scenario..."
	@printf "Alice 30 2\nBob 45 7\nCarol 22 5\nDavid 60 9\nEve 35 3\nquit\n" \
		| ./admissions --strategy best
	@echo ""
	@echo "── Schedule Log ────────────────────────────────"
	@cat logs/schedule_log.txt 2>/dev/null || echo "(no log yet)"
	@echo "── Memory Log ──────────────────────────────────"
	@cat logs/memory_log.txt  2>/dev/null || echo "(no log yet)"

# ── Stress: 20-patient rapid arrival test ───────────────────
stress: all
	@mkdir -p logs
	@chmod +x scripts/*.sh
	@echo "Running stress test (20 patients)..."
	./scripts/stress_test.sh | ./admissions --strategy best

# ── Valgrind memory check ────────────────────────────────────
valgrind: all
	@mkdir -p logs
	@printf "Ali 25 3\nSara 40 8\nquit\n" \
		| valgrind --leak-check=full --show-leak-kinds=all \
		           --track-origins=yes --verbose \
		  ./admissions --strategy best 2>&1 | tee logs/valgrind_output.txt
	@echo ""
	@echo "Valgrind report saved to logs/valgrind_output.txt"

# ── Clean ────────────────────────────────────────────────────
clean:
	rm -f $(ADMISSIONS_BIN) $(PATIENT_BIN)
	rm -f logs/schedule_log.txt logs/memory_log.txt logs/valgrind_output.txt
	rm -f /tmp/discharge_fifo
	ipcrm -M 0xBEDF00D 2>/dev/null || true
	@echo "✔  Clean complete"

# ── Help ─────────────────────────────────────────────────────
help:
	@echo "Hospital Patient Triage & Bed Allocator — Build Targets"
	@echo ""
	@echo "  make all      Build all binaries"
	@echo "  make run      Run in interactive mode (Best-Fit)"
	@echo "  make test     Run 5-patient scripted test"
	@echo "  make stress   Run 20-patient stress test"
	@echo "  make valgrind Run with Valgrind memory check"
	@echo "  make clean    Remove binaries and logs"
	@echo ""
	@echo "  Strategy flag: ./admissions --strategy best|first|worst"
