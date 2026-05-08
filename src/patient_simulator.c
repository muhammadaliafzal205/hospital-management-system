/*
 * ============================================================
 * Project : Hospital Patient Triage & Bed Allocator
 * File    : patient_simulator.c
 * Purpose : Spawned via fork()+execv() for each admitted
 *           patient.  Simulates treatment then notifies
 *           admissions via named FIFO.
 * Compile : gcc -Wall -Wextra -o patient_simulator patient_simulator.c
 * ============================================================
 */

#include "hospital.h"

int main(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <patient_id> <triage_level> <bed_id> <bed_type>\n", argv[0]);
        return 1;
    }

    int patient_id    = atoi(argv[1]);
    int triage_level  = atoi(argv[2]);
    int bed_id        = atoi(argv[3]);
    const char *btype = argv[4];

    /* Determine treatment duration by bed type */
    int min_t, max_t;
    if (strcmp(btype, "ICU") == 0)            { min_t = 5;  max_t = 15; }
    else if (strcmp(btype, "ISOLATION") == 0) { min_t = 3;  max_t = 10; }
    else                                       { min_t = 2;  max_t = 8;  }

    srand((unsigned)(time(NULL) ^ getpid()));
    int treat_time = min_t + rand() % (max_t - min_t + 1);

    printf("[PATIENT %d] ► ARRIVED   | Triage P%d | Bed %d (%s) | PID=%d\n",
           patient_id, triage_level, bed_id, btype, getpid());
    fflush(stdout);

    printf("[PATIENT %d] ► TREATMENT START | Duration: %d s\n", patient_id, treat_time);
    fflush(stdout);

    sleep(treat_time);

    printf("[PATIENT %d] ► DISCHARGED | Treatment complete (%d s)\n",
           patient_id, treat_time);
    fflush(stdout);

    /* Notify admissions via named FIFO */
    int fd = open(DISCHARGE_FIFO, O_WRONLY);
    if (fd < 0) {
        perror("[PATIENT] open FIFO");
        return 1;
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "%d\n", patient_id);
    write(fd, msg, strlen(msg));
    close(fd);

    printf("[PATIENT %d] ► Discharge notification sent\n", patient_id);
    fflush(stdout);
    return 0;
}
