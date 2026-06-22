#include <stdio.h>
#include <sys/timerfd.h>
#include "../agent.h"
#include "../structures/fdinfo.h"
#include "functions.h"


/* Inicia el timer (ya creado) con una cantidad en segundos */
int timerfd_start(int timerfd, int sec) {
    struct itimerspec ts;

    ts.it_value.tv_sec = sec;
    ts.it_value.tv_nsec = 0;

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(timerfd, 0, &ts, NULL) == -1)
        return -1;

    return 0;
}