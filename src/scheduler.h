#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "hospital.h"

void pq_init(PriorityQueue *pq);
void pq_enqueue(PriorityQueue *pq, PatientRecord *rec);
int  pq_dequeue(PriorityQueue *pq, PatientRecord *out);
int  pq_is_empty(PriorityQueue *pq);

void simulate_fcfs(PatientRecord *patients, int n, FILE *log);
void simulate_sjf(PatientRecord *patients, int n, FILE *log);
void simulate_priority(PatientRecord *patients, int n, FILE *log);
void simulate_rr(PatientRecord *patients, int n, int quantum, FILE *log);
void run_scheduling_simulation(PatientRecord *patients, int n, const char *log_file);

#endif
