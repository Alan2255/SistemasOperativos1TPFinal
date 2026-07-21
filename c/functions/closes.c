#include <stdlib.h>
#include <stdio.h>
#include "../structures/fd_table.h"
#include "../structures/agent_table.h"
#include "../structures/job_table.h"
#include "../structures/reservation_table.h"
#include "functions.h"

/* Cierra la conexion de un agente: limpia su id en agent_table, libera
las reservas de recursos locales que se le concedieron o quedaron encoladas
y pide el cierre del fd. */
void close_agent_conn(uint64_t id, FdEntry *info) {
    agent_table_clear_id(id);
    reservation_table_release_by_id(id);
    fd_table_request_close(info);
}

/* Cierra la conexion del scheduler: libera todos los jobs de job_table
(mandando "RELEASE" a cada agente que se le pidio), pide el cierre del fd y
reinicia scheduler_id. */
void close_scheduler_conn(FdEntry *info) {
    job_table_t **jobs = job_table_extract_all();
    if (jobs != NULL) {
        for (int i = 0; jobs[i] != NULL; i++) {
            release_job(jobs[i]);
            free(jobs[i]);
        }
        free(jobs);
    }

    fd_table_request_close(info);
    printf("Conexion con cliente cerrada\n");
    scheduler_id = UINT64_MAX;
}
