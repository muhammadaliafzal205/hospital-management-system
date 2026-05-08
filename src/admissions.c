/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : admissions.c
 * Group   : Group XX
 * Purpose : Central admissions manager - process spawning,
 *           IPC, thread pool, scheduling, and bed allocation.
 * Compile : gcc -Wall -Wextra -o admissions admissions.c \
 *             bed_allocator.c scheduler.c -lpthread
 * Run     : ./admissions [--strategy best|first|worst]
 * ============================================================
 */

#include "hospital.h"
#include "bed_allocator.h"
#include "scheduler.h"

/* ── Globals ─────────────────────────────────────────── */
static SharedMemory    *g_shm       = NULL;
static int              g_shmid     = -1;
static sem_t           *g_sem_icu   = SEM_FAILED;
static sem_t           *g_sem_iso   = SEM_FAILED;
static sem_t           *g_sem_slots = SEM_FAILED;  /* empty slots */
static sem_t           *g_sem_items = SEM_FAILED;  /* filled slots */

static pthread_mutex_t  g_bed_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_bed_freed  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t  g_pq_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_pq_cond    = PTHREAD_COND_INITIALIZER;

static PriorityQueue    g_waitlist;
static volatile int     g_running    = 1;
static int              g_patient_counter = 0;

/* Completed patients for scheduling simulation */
static PatientRecord    g_completed[MAX_PATIENTS];
static int              g_completed_count = 0;
static pthread_mutex_t  g_completed_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Pipe from triage.sh → admissions (stdin is set up by start_hospital.sh) */
/* We read from DISCHARGE_FIFO for patient exits */

/* ── SIGCHLD handler — reap zombie children ──────────── */
static void sigchld_handler(int sig)
{
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

/* ── SIGTERM handler — graceful shutdown ─────────────── */
static void sigterm_handler(int sig)
{
    (void)sig;
    g_running = 0;
    pthread_cond_broadcast(&g_pq_cond);
    pthread_cond_broadcast(&g_bed_freed);
}

/* ── Spawn patient process ────────────────────────────── */
static void spawn_patient_process(PatientRecord *rec, int partition_idx)
{
    BedPartition *bed = &g_shm->partitions[partition_idx];
    char pid_s[16], prio_s[16], bed_s[16], type_s[32];
    snprintf(pid_s,  sizeof(pid_s),  "%d", rec->patient_id);
    snprintf(prio_s, sizeof(prio_s), "%d", rec->priority);
    snprintf(bed_s,  sizeof(bed_s),  "%d", bed->partition_id);
    strncpy(type_s, bed->bed_type, sizeof(type_s)-1);

    pid_t child = fork();
    if (child < 0) { perror("fork"); return; }
    if (child == 0) {
        /* Child: exec patient_simulator */
        char *args[] = { "./patient_simulator", pid_s, prio_s, bed_s, type_s, NULL };
        execv("./patient_simulator", args);
        perror("execv");
        _exit(1);
    }
    printf("[ADMISSIONS] Spawned patient_simulator PID=%d for patient %d\n",
           child, rec->patient_id);
}

/* ── Nurse thread ────────────────────────────────────── */
typedef struct { char bed_type[16]; } NurseArg;

static void *nurse_thread(void *arg)
{
    NurseArg *na = (NurseArg *)arg;
    char      my_type[16];
    strncpy(my_type, na->bed_type, sizeof(my_type)-1);
    free(na);

    printf("[NURSE-%s] Thread started\n", my_type);

    while (g_running) {
        pthread_mutex_lock(&g_bed_mutex);
        /* Wait for a bed-freed signal */
        pthread_cond_wait(&g_bed_freed, &g_bed_mutex);
        if (!g_running) { pthread_mutex_unlock(&g_bed_mutex); break; }

        /* Check if any beds of our type were freed (already coalesced by free_bed) */
        printf("[NURSE-%s] Notified: checking ward for freed beds...\n", my_type);
        print_ward_map(g_shm);
        pthread_mutex_unlock(&g_bed_mutex);

        /* Signal scheduler that a bed may be available */
        pthread_mutex_lock(&g_pq_mutex);
        pthread_cond_signal(&g_pq_cond);
        pthread_mutex_unlock(&g_pq_mutex);
    }
    printf("[NURSE-%s] Thread exiting\n", my_type);
    return NULL;
}

/* ── Discharge listener thread ────────────────────────── */
static void *discharge_listener(void *arg)
{
    (void)arg;
    printf("[DISCHARGE-LISTENER] Thread started, reading from %s\n", DISCHARGE_FIFO);

    while (g_running) {
        int fd = open(DISCHARGE_FIFO, O_RDONLY);
        if (fd < 0) {
            if (g_running) perror("open FIFO");
            break;
        }
        char buf[64];
        int n = read(fd, buf, sizeof(buf)-1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        int patient_id = atoi(buf);
        printf("[DISCHARGE] Patient %d discharged — freeing bed\n", patient_id);

        pthread_mutex_lock(&g_bed_mutex);
        free_bed(g_shm, patient_id, MEMORY_LOG);
        pthread_cond_broadcast(&g_bed_freed);
        pthread_mutex_unlock(&g_bed_mutex);

        /* Release appropriate semaphore */
        /* (we store bed type per patient — simplified: release based on priority) */
    }
    return NULL;
}

/* ── Receptionist thread ─────────────────────────────── */
static void *receptionist_thread(void *arg)
{
    (void)arg;
    printf("[RECEPTIONIST] Thread started, reading patient records from stdin pipe\n");
    printf("[RECEPTIONIST] Send: <name> <age> <severity> (or 'quit' to stop)\n");

    char line[256];
    while (g_running && fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "quit", 4) == 0) { g_running = 0; break; }

        char  name[64];
        int   age, severity;
        if (sscanf(line, "%63s %d %d", name, &age, &severity) != 3) {
            printf("[RECEPTIONIST] Invalid input, expected: <name> <age> <severity>\n");
            continue;
        }
        if (severity < 1 || severity > 10) {
            printf("[RECEPTIONIST] Severity must be 1-10\n");
            continue;
        }

        PatientRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.patient_id   = ++g_patient_counter;
        strncpy(rec.name, name, sizeof(rec.name)-1);
        rec.age          = age;
        rec.severity     = severity;
        rec.priority     = (severity <= 2) ? 1 :
                           (severity <= 4) ? 2 :
                           (severity <= 6) ? 3 :
                           (severity <= 8) ? 4 : 5;
        rec.arrival_time = time(NULL);
        rec.burst_time   = (rec.priority <= 2) ? (5  + rand()%11) :
                           (rec.priority == 3)  ? (3  + rand()%8)  :
                                                  (2  + rand()%7);

        printf("[RECEPTIONIST] Patient %d: %s | Age:%d | Sev:%d → Priority:%d (%s)\n",
               rec.patient_id, rec.name, rec.age, rec.severity,
               rec.priority, priority_label(rec.priority));

        /* Producer: wait for queue slot, then enqueue */
        sem_wait(g_sem_slots);
        pthread_mutex_lock(&g_pq_mutex);
        pq_enqueue(&g_waitlist, &rec);
        pthread_cond_signal(&g_pq_cond);
        pthread_mutex_unlock(&g_pq_mutex);
        sem_post(g_sem_items);
    }
    printf("[RECEPTIONIST] Shutting down\n");
    g_running = 0;
    pthread_cond_broadcast(&g_pq_cond);
    return NULL;
}

/* ── Scheduler thread ────────────────────────────────── */
static void *scheduler_thread(void *arg)
{
    (void)arg;
    printf("[SCHEDULER] Thread started\n");

    while (g_running || !pq_is_empty(&g_waitlist)) {
        sem_wait(g_sem_items);
        if (!g_running && pq_is_empty(&g_waitlist)) break;

        pthread_mutex_lock(&g_pq_mutex);
        while (pq_is_empty(&g_waitlist) && g_running)
            pthread_cond_wait(&g_pq_cond, &g_pq_mutex);

        PatientRecord rec;
        if (!pq_dequeue(&g_waitlist, &rec)) {
            pthread_mutex_unlock(&g_pq_mutex);
            sem_post(g_sem_slots);
            continue;
        }
        pthread_mutex_unlock(&g_pq_mutex);
        sem_post(g_sem_slots);

        /* Determine bed type and acquire semaphore */
        BedType bt = bed_type_for_priority(rec.priority);
        if (bt == BED_ICU) {
            printf("[SCHEDULER] Waiting for ICU semaphore for patient %d...\n", rec.patient_id);
            sem_wait(g_sem_icu);
        } else if (bt == BED_ISOLATION) {
            printf("[SCHEDULER] Waiting for Isolation semaphore for patient %d...\n", rec.patient_id);
            sem_wait(g_sem_iso);
        }

        /* Allocate bed — wait if none available */
        pthread_mutex_lock(&g_bed_mutex);
        int partition_idx = -1;
        while ((partition_idx = allocate_bed(g_shm, &rec, MEMORY_LOG)) == -1 && g_running) {
            printf("[SCHEDULER] No bed for patient %d — waiting for bed_freed signal\n",
                   rec.patient_id);
            pthread_cond_wait(&g_bed_freed, &g_bed_mutex);
        }
        pthread_mutex_unlock(&g_bed_mutex);

        if (partition_idx >= 0) {
            spawn_patient_process(&rec, partition_idx);

            /* Record for scheduling simulation */
            pthread_mutex_lock(&g_completed_mutex);
            if (g_completed_count < MAX_PATIENTS)
                g_completed[g_completed_count++] = rec;
            pthread_mutex_unlock(&g_completed_mutex);
        } else {
            /* Release semaphore if we couldn't admit */
            if (bt == BED_ICU)        sem_post(g_sem_icu);
            else if (bt == BED_ISOLATION) sem_post(g_sem_iso);
        }
    }
    printf("[SCHEDULER] Thread exiting\n");
    return NULL;
}

/* ── IPC cleanup ─────────────────────────────────────── */
static void cleanup_ipc(void)
{
    if (g_shm != NULL && g_shm != (SharedMemory *)-1)
        shmdt(g_shm);
    if (g_shmid >= 0)
        shmctl(g_shmid, IPC_RMID, NULL);
    if (g_sem_icu   != SEM_FAILED) { sem_close(g_sem_icu);   sem_unlink(SEM_ICU); }
    if (g_sem_iso   != SEM_FAILED) { sem_close(g_sem_iso);   sem_unlink(SEM_ISOLATION); }
    if (g_sem_slots != SEM_FAILED) { sem_close(g_sem_slots); sem_unlink(SEM_QUEUE_SLOTS); }
    if (g_sem_items != SEM_FAILED) { sem_close(g_sem_items); sem_unlink(SEM_QUEUE_ITEMS); }
    unlink(DISCHARGE_FIFO);
    printf("[ADMISSIONS] IPC resources cleaned up\n");
}

/* ── main ────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    srand((unsigned)time(NULL));

    /* Parse --strategy */
    AllocStrategy strategy = STRATEGY_BEST;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i+1 < argc) {
            i++;
            if (strcmp(argv[i], "first") == 0) strategy = STRATEGY_FIRST;
            else if (strcmp(argv[i], "worst") == 0) strategy = STRATEGY_WORST;
            else strategy = STRATEGY_BEST;
        }
    }
    const char *strat_name[] = {"Best-Fit", "First-Fit", "Worst-Fit"};
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   Hospital Patient Triage & Bed Alloc   ║\n");
    printf("║   Allocation Strategy: %-17s║\n", strat_name[strategy]);
    printf("╚══════════════════════════════════════════╝\n\n");

    /* Signals */
    struct sigaction sa_chld = {0}, sa_term = {0};
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);
    sa_term.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT,  &sa_term, NULL);

    /* Shared memory */
    g_shmid = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (g_shmid < 0) { perror("shmget"); exit(1); }
    g_shm = (SharedMemory *)shmat(g_shmid, NULL, 0);
    if (g_shm == (void*)-1) { perror("shmat"); exit(1); }
    memset(g_shm, 0, sizeof(SharedMemory));
    init_ward(g_shm, strategy);

    /* Named semaphores */
    sem_unlink(SEM_ICU); sem_unlink(SEM_ISOLATION);
    sem_unlink(SEM_QUEUE_SLOTS); sem_unlink(SEM_QUEUE_ITEMS);

    g_sem_icu   = sem_open(SEM_ICU,          O_CREAT|O_EXCL, 0666, ICU_COUNT);
    g_sem_iso   = sem_open(SEM_ISOLATION,    O_CREAT|O_EXCL, 0666, ISOLATION_COUNT);
    g_sem_slots = sem_open(SEM_QUEUE_SLOTS,  O_CREAT|O_EXCL, 0666, MAX_QUEUE_SIZE);
    g_sem_items = sem_open(SEM_QUEUE_ITEMS,  O_CREAT|O_EXCL, 0666, 0);

    if (g_sem_icu==SEM_FAILED||g_sem_iso==SEM_FAILED||
        g_sem_slots==SEM_FAILED||g_sem_items==SEM_FAILED) {
        perror("sem_open"); cleanup_ipc(); exit(1);
    }

    /* Named FIFO for discharge notifications */
    unlink(DISCHARGE_FIFO);
    if (mkfifo(DISCHARGE_FIFO, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo"); cleanup_ipc(); exit(1);
    }

    /* Priority queue */
    pq_init(&g_waitlist);

    /* Ensure logs directory exists */
    mkdir("logs", 0755);

    /* Launch threads */
    pthread_t t_recep, t_sched, t_nurse_icu, t_nurse_iso, t_nurse_gen, t_discharge;

    NurseArg *na1 = malloc(sizeof(NurseArg)); strcpy(na1->bed_type, "ICU");
    NurseArg *na2 = malloc(sizeof(NurseArg)); strcpy(na2->bed_type, "ISOLATION");
    NurseArg *na3 = malloc(sizeof(NurseArg)); strcpy(na3->bed_type, "GENERAL");

    pthread_create(&t_discharge,  NULL, discharge_listener, NULL);
    pthread_create(&t_nurse_icu,  NULL, nurse_thread,       na1);
    pthread_create(&t_nurse_iso,  NULL, nurse_thread,       na2);
    pthread_create(&t_nurse_gen,  NULL, nurse_thread,       na3);
    pthread_create(&t_sched,      NULL, scheduler_thread,   NULL);
    pthread_create(&t_recep,      NULL, receptionist_thread, NULL);

    printf("[ADMISSIONS] All threads running. Type: <name> <age> <severity 1-10>\n");
    printf("[ADMISSIONS] Type 'quit' to shut down.\n\n");

    /* Wait for receptionist to signal shutdown */
    pthread_join(t_recep, NULL);

    g_running = 0;
    pthread_cond_broadcast(&g_pq_cond);
    pthread_cond_broadcast(&g_bed_freed);
    sem_post(g_sem_items); /* unblock scheduler */

    pthread_join(t_sched,      NULL);
    pthread_join(t_discharge,  NULL);
    pthread_join(t_nurse_icu,  NULL);
    pthread_join(t_nurse_iso,  NULL);
    pthread_join(t_nurse_gen,  NULL);

    /* Run scheduling simulation on completed patients */
    pthread_mutex_lock(&g_completed_mutex);
    if (g_completed_count > 0)
        run_scheduling_simulation(g_completed, g_completed_count, SCHEDULE_LOG);
    pthread_mutex_unlock(&g_completed_mutex);

    printf("\n[ADMISSIONS] Total patients served: %d\n", g_shm->total_patients_served);
    print_ward_map(g_shm);
    cleanup_ipc();
    return 0;
}
