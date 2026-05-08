#ifndef BED_ALLOCATOR_H
#define BED_ALLOCATOR_H

#include "hospital.h"

void init_ward(SharedMemory *shm, AllocStrategy strategy);
int  allocate_bed(SharedMemory *shm, PatientRecord *rec, const char *log_file);
void free_bed(SharedMemory *shm, int patient_id, const char *log_file);
void log_fragmentation(SharedMemory *shm, const char *log_file);
void print_ward_map(SharedMemory *shm);

#endif
