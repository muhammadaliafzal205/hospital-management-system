/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : hospital.h
 * Purpose : Shared definitions, structs, and constants
 * ============================================================
 */

#ifndef HOSPITAL_H
#define HOSPITAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>

/* ── Constants ─────────────────────────────────────── */
#define MAX_BEDS          20
#define ICU_COUNT          4
#define ISOLATION_COUNT    4
#define GENERAL_COUNT     12

#define ICU_CARE_UNITS     3
#define ISOLATION_CARE_UNITS 2
#define GENERAL_CARE_UNITS 1

#define TOTAL_CARE_UNITS  (ICU_COUNT*ICU_CARE_UNITS + ISOLATION_COUNT*ISOLATION_CARE_UNITS + GENERAL_COUNT*GENERAL_CARE_UNITS)
/* 4*3 + 4*2 + 12*1 = 12+8+12 = 32 */

#define MAX_PATIENTS      64
#define MAX_QUEUE_SIZE    32
#define PAGE_SIZE_UNITS    2   /* for paging simulation */

#define SHM_KEY           0xBEDF00D
#define DISCHARGE_FIFO    "/tmp/discharge_fifo"
#define SEM_ICU           "/sem_icu_limit"
#define SEM_ISOLATION     "/sem_isolation_limit"
#define SEM_QUEUE_SLOTS   "/sem_queue_slots"
#define SEM_QUEUE_ITEMS   "/sem_queue_items"
#define SCHEDULE_LOG      "logs/schedule_log.txt"
#define MEMORY_LOG        "logs/memory_log.txt"
#define PATIENT_RECORDS   "logs/patient_records.dat"

/* ── Enums ──────────────────────────────────────────── */
typedef enum { BED_ICU, BED_ISOLATION, BED_GENERAL } BedType;
typedef enum { STRATEGY_BEST, STRATEGY_FIRST, STRATEGY_WORST } AllocStrategy;

/* ── Structs ────────────────────────────────────────── */

/* Patient record passed via IPC */
typedef struct {
    int     patient_id;
    char    name[64];
    int     age;
    int     severity;       /* 1-10 raw severity from triage */
    int     priority;       /* 1-5 computed triage priority  */
    int     care_units;     /* memory units required         */
    time_t  arrival_time;
    int     burst_time;     /* simulated treatment seconds   */
    double  waiting_time;
    double  turnaround_time;
    int     start_time_offset; /* seconds after sim start    */
} PatientRecord;

/* Single bed partition in the ward memory model */
typedef struct {
    int  partition_id;
    int  start_unit;        /* index in ward array           */
    int  size;              /* number of care units          */
    int  is_free;           /* 1 = FREE, 0 = OCCUPIED        */
    int  patient_id;        /* -1 if free                    */
    char bed_type[16];      /* "ICU", "GENERAL", "ISOLATION" */
} BedPartition;

/* Page table entry */
typedef struct {
    int page_id;
    int patient_id;         /* -1 if free */
    int used_units;         /* how many units the patient actually uses */
} PageEntry;

/* Shared memory layout */
typedef struct {
    BedPartition  partitions[MAX_BEDS * 2]; /* allow splits   */
    int           partition_count;
    int           ward[TOTAL_CARE_UNITS];   /* care-unit array: patient_id or -1 */
    PageEntry     page_table[TOTAL_CARE_UNITS / PAGE_SIZE_UNITS + 1];
    int           page_count;
    int           total_patients_served;
    AllocStrategy strategy;
    int           sim_start_time;
} SharedMemory;

/* Priority queue node */
typedef struct PQNode {
    PatientRecord   record;
    struct PQNode  *next;
} PQNode;

/* Priority queue */
typedef struct {
    PQNode *head;
    int     size;
} PriorityQueue;

/* ── Inline utility ─────────────────────────────────── */
static inline const char *priority_label(int p) {
    switch(p) {
        case 1: return "CRITICAL";
        case 2: return "URGENT";
        case 3: return "MODERATE";
        case 4: return "MINOR";
        case 5: return "NON-URGENT";
        default: return "UNKNOWN";
    }
}

static inline BedType bed_type_for_priority(int priority) {
    if (priority <= 2) return BED_ICU;
    if (priority == 3) return BED_ISOLATION;
    return BED_GENERAL;
}

static inline int care_units_for_bed(BedType t) {
    switch(t) {
        case BED_ICU:       return ICU_CARE_UNITS;
        case BED_ISOLATION: return ISOLATION_CARE_UNITS;
        default:            return GENERAL_CARE_UNITS;
    }
}

#endif /* HOSPITAL_H */
