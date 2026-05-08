/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : bed_allocator.c
 * Purpose : Best-Fit / First-Fit / Worst-Fit bed allocation,
 *           coalescing, fragmentation reporting, paging sim.
 * Compile : (linked into admissions)
 * ============================================================
 */

#include "hospital.h"
#include "bed_allocator.h"

/* ── Initialise ward ─────────────────────────────────── */
void init_ward(SharedMemory *shm, AllocStrategy strategy)
{
    shm->strategy        = strategy;
    shm->partition_count = 0;
    shm->total_patients_served = 0;
    shm->sim_start_time  = (int)time(NULL);

    /* Fill ward array with -1 (free) */
    for (int i = 0; i < TOTAL_CARE_UNITS; i++)
        shm->ward[i] = -1;

    /* Create initial partitions: ICU block, Isolation block, General block */
    int offset = 0;

    /* ICU partitions – one per bed */
    for (int i = 0; i < ICU_COUNT; i++) {
        BedPartition *p  = &shm->partitions[shm->partition_count++];
        p->partition_id  = i;
        p->start_unit    = offset;
        p->size          = ICU_CARE_UNITS;
        p->is_free       = 1;
        p->patient_id    = -1;
        strcpy(p->bed_type, "ICU");
        offset += ICU_CARE_UNITS;
    }

    /* Isolation partitions */
    for (int i = 0; i < ISOLATION_COUNT; i++) {
        BedPartition *p  = &shm->partitions[shm->partition_count++];
        p->partition_id  = ICU_COUNT + i;
        p->start_unit    = offset;
        p->size          = ISOLATION_CARE_UNITS;
        p->is_free       = 1;
        p->patient_id    = -1;
        strcpy(p->bed_type, "ISOLATION");
        offset += ISOLATION_CARE_UNITS;
    }

    /* General partitions */
    for (int i = 0; i < GENERAL_COUNT; i++) {
        BedPartition *p  = &shm->partitions[shm->partition_count++];
        p->partition_id  = ICU_COUNT + ISOLATION_COUNT + i;
        p->start_unit    = offset;
        p->size          = GENERAL_CARE_UNITS;
        p->is_free       = 1;
        p->patient_id    = -1;
        strcpy(p->bed_type, "GENERAL");
        offset += GENERAL_CARE_UNITS;
    }

    /* Init page table */
    shm->page_count = TOTAL_CARE_UNITS / PAGE_SIZE_UNITS;
    for (int i = 0; i < shm->page_count; i++) {
        shm->page_table[i].page_id    = i;
        shm->page_table[i].patient_id = -1;
        shm->page_table[i].used_units = 0;
    }

    printf("[WARD] Initialized: %d ICU, %d Isolation, %d General beds (%d total care units)\n",
           ICU_COUNT, ISOLATION_COUNT, GENERAL_COUNT, TOTAL_CARE_UNITS);
}

/* ── Allocate bed ────────────────────────────────────── */
int allocate_bed(SharedMemory *shm, PatientRecord *rec, const char *log_file)
{
    BedType needed_type = bed_type_for_priority(rec->priority);
    int     needed_units = care_units_for_bed(needed_type);
    rec->care_units      = needed_units;

    int best_idx  = -1;
    int best_size = 0;

    for (int i = 0; i < shm->partition_count; i++) {
        BedPartition *p = &shm->partitions[i];
        if (!p->is_free) continue;

        /* Check type compatibility */
        BedType ptype;
        if (strcmp(p->bed_type, "ICU")       == 0) ptype = BED_ICU;
        else if (strcmp(p->bed_type, "ISOLATION") == 0) ptype = BED_ISOLATION;
        else ptype = BED_GENERAL;

        if (ptype != needed_type) continue;
        if (p->size < needed_units) continue;

        switch (shm->strategy) {
            case STRATEGY_BEST:
                if (best_idx == -1 || p->size < best_size) {
                    best_idx  = i;
                    best_size = p->size;
                }
                break;
            case STRATEGY_FIRST:
                if (best_idx == -1) {
                    best_idx  = i;
                    best_size = p->size;
                }
                break;
            case STRATEGY_WORST:
                if (best_idx == -1 || p->size > best_size) {
                    best_idx  = i;
                    best_size = p->size;
                }
                break;
        }
    }

    if (best_idx == -1) {
        printf("[ALLOC] No suitable %s bed for patient %d (P%d)\n",
               (needed_type==BED_ICU)?"ICU":(needed_type==BED_ISOLATION)?"Isolation":"General",
               rec->patient_id, rec->priority);
        return -1;
    }

    BedPartition *chosen = &shm->partitions[best_idx];
    chosen->is_free    = 0;
    chosen->patient_id = rec->patient_id;

    /* Mark ward array */
    for (int u = chosen->start_unit; u < chosen->start_unit + chosen->size; u++)
        shm->ward[u] = rec->patient_id;

    /* Update page table */
    int first_page = chosen->start_unit / PAGE_SIZE_UNITS;
    int pages_used = (chosen->size + PAGE_SIZE_UNITS - 1) / PAGE_SIZE_UNITS;
    int internal_frag = (pages_used * PAGE_SIZE_UNITS) - chosen->size;
    for (int pg = first_page; pg < first_page + pages_used && pg < shm->page_count; pg++) {
        shm->page_table[pg].patient_id = rec->patient_id;
        shm->page_table[pg].used_units = PAGE_SIZE_UNITS;
    }
    if (internal_frag > 0)
        printf("[PAGING] Patient %d: internal fragmentation = %d unit(s)\n",
               rec->patient_id, internal_frag);

    printf("[ALLOC] Patient %d (%s) → %s bed (partition %d, start=%d, size=%d)\n",
           rec->patient_id, rec->name, chosen->bed_type,
           chosen->partition_id, chosen->start_unit, chosen->size);

    log_fragmentation(shm, log_file);
    return best_idx;
}

/* ── Free bed + coalesce ─────────────────────────────── */
void free_bed(SharedMemory *shm, int patient_id, const char *log_file)
{
    int idx = -1;
    for (int i = 0; i < shm->partition_count; i++) {
        if (!shm->partitions[i].is_free && shm->partitions[i].patient_id == patient_id) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        printf("[FREE] Patient %d not found in ward\n", patient_id);
        return;
    }

    BedPartition *p = &shm->partitions[idx];
    printf("\n[COALESCE] Before: Partition %d [%s] start=%d size=%d (OCCUPIED by P%d)\n",
           p->partition_id, p->bed_type, p->start_unit, p->size, patient_id);

    /* Clear ward array */
    for (int u = p->start_unit; u < p->start_unit + p->size; u++)
        shm->ward[u] = -1;

    /* Clear page table */
    int first_page = p->start_unit / PAGE_SIZE_UNITS;
    int pages_used = (p->size + PAGE_SIZE_UNITS - 1) / PAGE_SIZE_UNITS;
    for (int pg = first_page; pg < first_page + pages_used && pg < shm->page_count; pg++) {
        shm->page_table[pg].patient_id = -1;
        shm->page_table[pg].used_units = 0;
    }

    p->is_free    = 1;
    p->patient_id = -1;

    /* Right-coalesce: find adjacent free partition of same type */
    int right_end = p->start_unit + p->size;
    for (int i = 0; i < shm->partition_count; i++) {
        if (i == idx) continue;
        BedPartition *r = &shm->partitions[i];
        if (r->is_free && strcmp(r->bed_type, p->bed_type) == 0
            && r->start_unit == right_end) {
            printf("[COALESCE] Right-merging partition %d (size %d) into partition %d\n",
                   r->partition_id, r->size, p->partition_id);
            p->size += r->size;
            /* Remove r by shifting */
            for (int j = i; j < shm->partition_count - 1; j++)
                shm->partitions[j] = shm->partitions[j+1];
            shm->partition_count--;
            right_end = p->start_unit + p->size;
            i = -1; /* restart scan */
        }
    }

    /* Left-coalesce */
    for (int i = 0; i < shm->partition_count; i++) {
        if (i == idx) continue;
        BedPartition *l = &shm->partitions[i];
        if (l->is_free && strcmp(l->bed_type, p->bed_type) == 0
            && l->start_unit + l->size == p->start_unit) {
            printf("[COALESCE] Left-merging partition %d (size %d) into partition %d\n",
                   p->partition_id, p->size, l->partition_id);
            l->size += p->size;
            for (int j = idx; j < shm->partition_count - 1; j++)
                shm->partitions[j] = shm->partitions[j+1];
            shm->partition_count--;
            break;
        }
    }

    printf("[COALESCE] After: ward freed for patient %d\n\n", patient_id);
    shm->total_patients_served++;
    log_fragmentation(shm, log_file);
}

/* ── Fragmentation report ────────────────────────────── */
void log_fragmentation(SharedMemory *shm, const char *log_file)
{
    int total_free = 0, largest_free = 0;
    for (int i = 0; i < shm->partition_count; i++) {
        if (shm->partitions[i].is_free) {
            total_free += shm->partitions[i].size;
            if (shm->partitions[i].size > largest_free)
                largest_free = shm->partitions[i].size;
        }
    }
    double frag_pct = 0.0;
    if (total_free > 0)
        frag_pct = (1.0 - (double)largest_free / total_free) * 100.0;

    printf("[FRAG] Total free: %d units | Largest block: %d | External frag: %.1f%%\n",
           total_free, largest_free, frag_pct);

    /* Log to file */
    FILE *f = fopen(log_file, "a");
    if (f) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] Free=%d Largest=%d ExtFrag=%.1f%%\n",
                ts, total_free, largest_free, frag_pct);
        fclose(f);
    }
}

/* ── Print ward map ──────────────────────────────────── */
void print_ward_map(SharedMemory *shm)
{
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║           WARD MEMORY MAP                ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ Unit │ Type       │ Status    │ Patient  ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    for (int i = 0; i < shm->partition_count; i++) {
        BedPartition *p = &shm->partitions[i];
        printf("║ %4d │ %-10s │ %-9s │ %-8d ║\n",
               p->start_unit,
               p->bed_type,
               p->is_free ? "FREE" : "OCCUPIED",
               p->patient_id);
    }
    printf("╚══════════════════════════════════════════╝\n\n");
}
