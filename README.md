# Hospital Patient Triage & Bed Allocator
**CL2006 – Operating Systems Lab | Spring 2026 | FAST-NUCES CFD Campus**

---

## Project Overview
A fully functional system-level C simulation of hospital emergency-room operations demonstrating:
- **Process Management** — `fork()` / `execv()` / `SIGCHLD` zombie reaping
- **IPC** — Anonymous pipes, Named FIFO, Shared Memory (`shmget`/`shmat`)
- **CPU Scheduling** — Priority Queue, FCFS, SJF, Priority, Round-Robin
- **Synchronization** — Mutex, Condition Variables, Counting Semaphores (producer-consumer)
- **Memory Management** — Best-Fit / First-Fit / Worst-Fit, Coalescing, Fragmentation, Paging

---

## Prerequisites (Ubuntu 22.04 / 24.04)

```bash
sudo apt update
sudo apt install gcc make valgrind -y
```

No external libraries needed — only standard POSIX/glibc.

---

## Project Structure

```
hospital_project/
├── src/
│   ├── hospital.h            # Shared structs, constants, enums
│   ├── admissions.c          # Main process: threads, IPC, scheduling
│   ├── patient_simulator.c   # Child process per patient
│   ├── bed_allocator.c       # Best/First/Worst-Fit, coalescing, paging
│   ├── bed_allocator.h
│   ├── scheduler.c           # Priority queue + 4 scheduling algorithms
│   └── scheduler.h
├── scripts/
│   ├── triage.sh             # Validate input → compute priority → pipe to admissions
│   ├── start_hospital.sh     # Initialize IPC → launch admissions
│   ├── stop_hospital.sh      # SIGTERM → cleanup IPC → print summary
│   └── stress_test.sh        # 20 rapid patient arrivals
├── logs/                     # Auto-created at runtime
│   ├── schedule_log.txt      # FCFS / SJF / Priority / RR Gantt charts
│   └── memory_log.txt        # Timestamped fragmentation events
├── Makefile
└── README.md
```

---

## Build

```bash
cd hospital_project
make all
```

Expected output:
```
gcc -Wall -Wextra -pthread -g -o admissions src/admissions.c src/bed_allocator.c src/scheduler.c -lpthread
✔  admissions built
gcc -Wall -Wextra -pthread -g -o patient_simulator src/patient_simulator.c -lpthread
✔  patient_simulator built
✔  Build complete.
```

---

## How to Run

### Option 1 — Interactive Mode (recommended for demo)
```bash
make run
```
Then type patients one per line:
```
Alice 30 2
Bob 45 9
Carol 22 5
quit
```

### Option 2 — Scripted 5-patient Test
```bash
make test
```

### Option 3 — 20-Patient Stress Test
```bash
make stress
```

### Option 4 — Different Allocation Strategies
```bash
./admissions --strategy first    # First-Fit
./admissions --strategy worst    # Worst-Fit
./admissions --strategy best     # Best-Fit (default)
```

### Option 5 — Via triage.sh (validates input first)
```bash
chmod +x scripts/triage.sh
./scripts/triage.sh Alice 30 2 | ./admissions --strategy best
```

### Option 6 — Valgrind Memory Check
```bash
make valgrind
# Report saved to logs/valgrind_output.txt
```

---

## Input Format
```
<name> <age> <severity>
```
| Field | Description |
|-------|-------------|
| name | Patient name (no spaces) |
| age | Integer 0–150 |
| severity | Integer 1–10 (10 = most severe) |

### Severity → Priority Mapping
| Severity | Priority | Label | Bed Type |
|----------|----------|-------|----------|
| 1–2 | 1 | CRITICAL | ICU |
| 3–4 | 2 | URGENT | ICU |
| 5–6 | 3 | MODERATE | Isolation |
| 7–8 | 4 | MINOR | General |
| 9–10 | 5 | NON-URGENT | General |

---

## Expected Output (5-patient test)

```
╔══════════════════════════════════════════╗
║   Hospital Patient Triage & Bed Alloc   ║
║   Allocation Strategy: Best-Fit         ║
╚══════════════════════════════════════════╝

[WARD] Initialized: 4 ICU, 4 Isolation, 12 General beds (32 total care units)
[ADMISSIONS] All threads running. Type: <name> <age> <severity 1-10>

[RECEPTIONIST] Patient 1: Alice | Age:30 | Sev:2 → Priority:1 (CRITICAL)
[PQ] Enqueued patient 1 (Alice) priority=1 (CRITICAL) | Queue size: 1
[SCHEDULER] Waiting for ICU semaphore for patient 1...
[ALLOC] Patient 1 (Alice) → ICU bed (partition 0, start=0, size=3)
[FRAG] Total free: 29 units | Largest block: 8 | External frag: 72.4%
[ADMISSIONS] Spawned patient_simulator PID=12345 for patient 1
[PATIENT 1] ► ARRIVED   | Triage P1 | Bed 0 (ICU) | PID=12345
[PATIENT 1] ► TREATMENT START | Duration: 9 s
...
[PATIENT 1] ► DISCHARGED | Treatment complete (9 s)
[DISCHARGE] Patient 1 discharged — freeing bed
[COALESCE] Before: Partition 0 [ICU] start=0 size=3 (OCCUPIED by P1)
[COALESCE] After: ward freed for patient 1
[FRAG] Total free: 32 units | Largest block: 12 | External frag: 0.0%
```

### Schedule Log (logs/schedule_log.txt)
```
=== FCFS SCHEDULING SIMULATION ===
ID    Name            Burst    Start    Waiting    Turnaround
──────────────────────────────────────────────────────
1     Alice           9        0        0.0        9.0
  Gantt: |P1---------| [0-9]
2     Bob             6        9        9.0        15.0
  Gantt: |P2------| [9-15]
...
Avg Waiting Time   : 7.20 s
Avg Turnaround Time: 13.40 s

=== SJF SCHEDULING SIMULATION ===
...
=== PRIORITY SCHEDULING SIMULATION ===
...
=== ROUND ROBIN SCHEDULING (quantum=3) ===
```

### Memory Log (logs/memory_log.txt)
```
[2026-04-15 10:23:01] Free=29 Largest=8 ExtFrag=72.4%
[2026-04-15 10:23:04] Free=27 Largest=8 ExtFrag=70.4%
[2026-04-15 10:23:15] Free=32 Largest=12 ExtFrag=0.0%
```

---

## OS Concepts Demonstrated

| Concept | Where |
|---------|-------|
| `fork()` + `execv()` | `admissions.c` → `spawn_patient_process()` |
| `SIGCHLD` + `waitpid(WNOHANG)` | `admissions.c` → `sigchld_handler()` |
| Anonymous Pipe | `triage.sh` → admissions stdin |
| Named FIFO | `patient_simulator` → `discharge_listener` thread |
| Shared Memory (`shmget`/`shmat`) | `admissions.c` → `SharedMemory` struct |
| Priority Queue | `scheduler.c` → `pq_enqueue/dequeue()` |
| POSIX Threads (3 roles) | Receptionist, Scheduler, Nurse×3 |
| Mutex | `g_bed_mutex`, `g_pq_mutex` |
| Condition Variable | `g_bed_freed`, `g_pq_cond` |
| Counting Semaphore | ICU/Isolation capacity + producer-consumer |
| Best-Fit Allocator | `bed_allocator.c` → `allocate_bed()` |
| Coalescing | `bed_allocator.c` → `free_bed()` |
| Fragmentation Report | `log_fragmentation()` |
| Paging Simulation | `allocate_bed()` → page table |

---

## Clean Up

```bash
make clean
# Or manually:
./scripts/stop_hospital.sh
```

---

## Academic Integrity
All code written by group members. AI tools used only for understanding concepts, not generating entire functions.
