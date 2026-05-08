/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : scheduler.c
 * Purpose : Priority queue, FCFS, SJF, Priority, Round-Robin
 *           scheduling simulations with metrics.
 * ============================================================
 */

#include "hospital.h"
#include "scheduler.h"

/* ── Priority Queue ──────────────────────────────────── */
void pq_init(PriorityQueue *pq) { pq->head = NULL; pq->size = 0; }

void pq_enqueue(PriorityQueue *pq, PatientRecord *rec)
{
    PQNode *node = malloc(sizeof(PQNode));
    if (!node) { perror("malloc"); return; }
    node->record = *rec;
    node->next   = NULL;

    /* Insert in priority order (lower number = higher priority) */
    if (!pq->head || rec->priority < pq->head->record.priority) {
        node->next = pq->head;
        pq->head   = node;
    } else {
        PQNode *cur = pq->head;
        while (cur->next && cur->next->record.priority <= rec->priority)
            cur = cur->next;
        node->next = cur->next;
        cur->next  = node;
    }
    pq->size++;
    printf("[PQ] Enqueued patient %d (%s) priority=%d (%s) | Queue size: %d\n",
           rec->patient_id, rec->name, rec->priority,
           priority_label(rec->priority), pq->size);
}

int pq_dequeue(PriorityQueue *pq, PatientRecord *out)
{
    if (!pq->head) return 0;
    PQNode *node = pq->head;
    *out         = node->record;
    pq->head     = node->next;
    free(node);
    pq->size--;
    return 1;
}

int pq_is_empty(PriorityQueue *pq) { return pq->size == 0; }

/* ── Scheduling simulation ────────────────────────────── */
/* Returns average waiting and turnaround times */
void simulate_fcfs(PatientRecord *patients, int n, FILE *log)
{
    fprintf(log, "\n=== FCFS SCHEDULING SIMULATION ===\n");
    fprintf(log, "%-5s %-15s %-8s %-8s %-10s %-12s\n",
            "ID", "Name", "Burst", "Start", "Waiting", "Turnaround");
    fprintf(log, "──────────────────────────────────────────────────────\n");

    double total_wait = 0, total_ta = 0;
    int    current_time = 0;

    for (int i = 0; i < n; i++) {
        int wait = current_time;
        int ta   = wait + patients[i].burst_time;
        patients[i].waiting_time    = wait;
        patients[i].turnaround_time = ta;
        fprintf(log, "%-5d %-15s %-8d %-8d %-10.1f %-12.1f\n",
                patients[i].patient_id, patients[i].name,
                patients[i].burst_time, current_time,
                (double)wait, (double)ta);
        /* Gantt */
        fprintf(log, "  Gantt: |P%d", patients[i].patient_id);
        for (int t = 0; t < patients[i].burst_time; t++) fprintf(log, "-");
        fprintf(log, "| [%d-%d]\n", current_time, current_time + patients[i].burst_time);
        current_time += patients[i].burst_time;
        total_wait += wait;
        total_ta   += ta;
    }
    fprintf(log, "\nAvg Waiting Time   : %.2f s\n", total_wait / n);
    fprintf(log, "Avg Turnaround Time: %.2f s\n\n", total_ta   / n);
    printf("[SCHED] FCFS  → Avg Wait: %.2f s | Avg TAT: %.2f s\n",
           total_wait/n, total_ta/n);
}

void simulate_sjf(PatientRecord *patients, int n, FILE *log)
{
    fprintf(log, "\n=== SJF SCHEDULING SIMULATION ===\n");
    fprintf(log, "%-5s %-15s %-8s %-8s %-10s %-12s\n",
            "ID", "Name", "Burst", "Start", "Waiting", "Turnaround");
    fprintf(log, "──────────────────────────────────────────────────────\n");

    /* Copy and sort by burst time */
    PatientRecord sorted[MAX_PATIENTS];
    memcpy(sorted, patients, n * sizeof(PatientRecord));
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (sorted[i].burst_time > sorted[j].burst_time) {
                PatientRecord tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
            }

    double total_wait = 0, total_ta = 0;
    int    current_time = 0;
    for (int i = 0; i < n; i++) {
        int wait = current_time;
        int ta   = wait + sorted[i].burst_time;
        fprintf(log, "%-5d %-15s %-8d %-8d %-10.1f %-12.1f\n",
                sorted[i].patient_id, sorted[i].name,
                sorted[i].burst_time, current_time, (double)wait, (double)ta);
        fprintf(log, "  Gantt: |P%d", sorted[i].patient_id);
        for (int t = 0; t < sorted[i].burst_time; t++) fprintf(log, "-");
        fprintf(log, "| [%d-%d]\n", current_time, current_time + sorted[i].burst_time);
        current_time += sorted[i].burst_time;
        total_wait += wait; total_ta += ta;
    }
    fprintf(log, "\nAvg Waiting Time   : %.2f s\n", total_wait / n);
    fprintf(log, "Avg Turnaround Time: %.2f s\n\n", total_ta   / n);
    printf("[SCHED] SJF   → Avg Wait: %.2f s | Avg TAT: %.2f s\n",
           total_wait/n, total_ta/n);
}

void simulate_priority(PatientRecord *patients, int n, FILE *log)
{
    fprintf(log, "\n=== PRIORITY SCHEDULING SIMULATION ===\n");
    fprintf(log, "%-5s %-15s %-8s %-8s %-10s %-12s\n",
            "ID", "Name", "Burst", "Priority", "Waiting", "Turnaround");
    fprintf(log, "──────────────────────────────────────────────────────\n");

    PatientRecord sorted[MAX_PATIENTS];
    memcpy(sorted, patients, n * sizeof(PatientRecord));
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (sorted[i].priority > sorted[j].priority) {
                PatientRecord tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
            }

    double total_wait = 0, total_ta = 0;
    int    current_time = 0;
    for (int i = 0; i < n; i++) {
        int wait = current_time;
        int ta   = wait + sorted[i].burst_time;
        fprintf(log, "%-5d %-15s %-8d %-8d %-10.1f %-12.1f\n",
                sorted[i].patient_id, sorted[i].name,
                sorted[i].burst_time, sorted[i].priority, (double)wait, (double)ta);
        fprintf(log, "  Gantt: |P%d", sorted[i].patient_id);
        for (int t = 0; t < sorted[i].burst_time; t++) fprintf(log, "-");
        fprintf(log, "| [%d-%d]\n", current_time, current_time + sorted[i].burst_time);
        current_time += sorted[i].burst_time;
        total_wait += wait; total_ta += ta;
    }
    fprintf(log, "\nAvg Waiting Time   : %.2f s\n", total_wait / n);
    fprintf(log, "Avg Turnaround Time: %.2f s\n\n", total_ta   / n);
    printf("[SCHED] PRIO  → Avg Wait: %.2f s | Avg TAT: %.2f s\n",
           total_wait/n, total_ta/n);
}

void simulate_rr(PatientRecord *patients, int n, int quantum, FILE *log)
{
    fprintf(log, "\n=== ROUND ROBIN SCHEDULING (quantum=%d) ===\n", quantum);

    int remaining[MAX_PATIENTS];
    for (int i = 0; i < n; i++) remaining[i] = patients[i].burst_time;

    double total_wait = 0, total_ta = 0;
    int    current_time = 0, done = 0;
    double finish[MAX_PATIENTS];
    memset(finish, 0, sizeof(finish));

    fprintf(log, "Gantt Chart:\n|");
    while (done < n) {
        int progressed = 0;
        for (int i = 0; i < n; i++) {
            if (remaining[i] <= 0) continue;
            int slice = (remaining[i] < quantum) ? remaining[i] : quantum;
            fprintf(log, "P%d", patients[i].patient_id);
            for (int t = 0; t < slice; t++) fprintf(log, "-");
            fprintf(log, "|");
            current_time   += slice;
            remaining[i]   -= slice;
            if (remaining[i] == 0) { finish[i] = current_time; done++; }
            progressed = 1;
        }
        if (!progressed) break;
    }
    fprintf(log, "\n\n");
    fprintf(log, "%-5s %-15s %-8s %-10s %-12s\n",
            "ID", "Name", "Burst", "Waiting", "Turnaround");
    fprintf(log, "──────────────────────────────────────────────\n");
    for (int i = 0; i < n; i++) {
        double ta   = finish[i];
        double wait = ta - patients[i].burst_time;
        fprintf(log, "%-5d %-15s %-8d %-10.1f %-12.1f\n",
                patients[i].patient_id, patients[i].name,
                patients[i].burst_time, wait, ta);
        total_wait += wait; total_ta += ta;
    }
    fprintf(log, "\nAvg Waiting Time   : %.2f s\n", total_wait / n);
    fprintf(log, "Avg Turnaround Time: %.2f s\n\n", total_ta   / n);
    printf("[SCHED] RR(q=%d) → Avg Wait: %.2f s | Avg TAT: %.2f s\n",
           quantum, total_wait/n, total_ta/n);
}

void run_scheduling_simulation(PatientRecord *patients, int n, const char *log_file)
{
    if (n == 0) return;
    FILE *f = fopen(log_file, "a");
    if (!f) { perror("fopen schedule_log"); return; }

    time_t now = time(NULL);
    fprintf(f, "\n══════════════════════════════════════════════════════\n");
    fprintf(f, "Scheduling Simulation — %s", ctime(&now));
    fprintf(f, "Patients: %d\n", n);
    fprintf(f, "══════════════════════════════════════════════════════\n");

    simulate_fcfs(patients, n, f);
    simulate_sjf(patients, n, f);
    simulate_priority(patients, n, f);
    simulate_rr(patients, n, 3, f);

    fclose(f);
    printf("[SCHED] All simulations written to %s\n", log_file);
}
