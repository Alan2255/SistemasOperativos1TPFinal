#include "../structures/fd_table.h"
#include "../structures/table_agent.h"
#include "../structures/table_job.h"
#include "../structures/table_reservation.h"
#include "functions.h"

/* Cierra la conexion de un agente: limpia su id en table_agent, libera
las reservas de recursos locales que se le concedieron o quedaron encoladas
y pide el cierre del fd. */
void close_agent_conn(uint64_t id, FdEntry *info) {
    agent_manager_clear_id(id);
    reservation_manager_release_by_socket(info->fd);
    fd_table_request_close(info);
}

/* Cierra la conexion del scheduler: libera todos los jobs de table_job
(mandando "RELEASE" a cada agente que se le pidio), pide el cierre del fd y
reinicia scheduler_id. */
void close_scheduler_conn(FdEntry *info) {
    if (table_job) {
        for (int i = 0; i < table_job->used; i++) {
            if (table_job->entries[i].value == NULL) continue;
            job_release_((job_table_t*)table_job->entries[i].value);
        }
    }

    fd_table_request_close(info);
    scheduler_id = UINT64_MAX;
}
