#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>

#include "clock.h"

static pthread_t clock_tempo_thread;

static void *clock_tempo_run(void *p) {
    (void) p;
    struct timespec req;
    int beat = 0;
    float interval_seconds = 1;
    uint64_t interval_nsec = (uint64_t) interval_seconds * 1000000000;

    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &req);
        uint64_t current_time = (uint64_t) (1000000000 * req.tv_sec + req.tv_nsec);
        uint64_t new_time = current_time + interval_nsec;

        req.tv_sec = new_time / 1000000000;
        req.tv_nsec = new_time % 1000000000;

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL);

        beat += 1;
        clock_update_counter(beat, interval_seconds);
    }

    return NULL;
}

void clock_tempo_start() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    pthread_create(&clock_tempo_thread, &attr, &clock_tempo_run, NULL);
}
