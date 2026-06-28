#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "table_job.h"

// Función hash
static void fun_hash(int job_id, char *out_key, size_t max_len) {
    snprintf(out_key, max_len, "%d", job_id);
}

// Crea la tabla de jobs
void job_manager_init(void) {
    if (table_job == NULL) {
        table_job = hash_create();
    }
}

// Destruye la tabla de jobs
void job_manager_shutdown(void) {
    if (!table_job) return;

    for (int i = 0; i < table_job->used; i++) {
        if (table_job->entries[i].value != NULL) {
            free(table_job->entries[i].value);
            table_job->entries[i].value = NULL;
        }
    }

    hash_destroy(table_job);
    table_job = NULL;
}

// Agrega un job
bool job_add(int job_id, int nreqs, const job_req_t *reqs) {
    if (!table_job || !reqs || nreqs <= 0) return false;

    int cantidad_a_copiar = (nreqs > MAX_JOB_RQ) ? MAX_JOB_RQ : nreqs;

    job_table_t *nuevo_job = malloc(sizeof(job_table_t));
    if (!nuevo_job) return false;

    nuevo_job->job_id = job_id;
    nuevo_job->nreqs = cantidad_a_copiar;
    memcpy(nuevo_job->reqs, reqs, cantidad_a_copiar * sizeof(job_req_t));

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    if (!hash_set(table_job, key, nuevo_job)) {
        free(nuevo_job);
        return false;
    }

    return true;
}

// Elimina un job
bool job_release(int job_id) {
    if (!table_job) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    job_table_t *job = (job_table_t*)hash_get(table_job, key);
    if (job != NULL) {
        free(job);
    }

    return hash_remove(table_job, key);
}

// Busca un job por ID y lo devuelve si existe
const job_table_t* job_get(int job_id) {
    if (!table_job) return NULL;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    return (const job_table_t*)hash_get(table_job, key);
}

// Chequea si todos los requisitos del job están concedidos
int job_check_granted(int job_id) {

    job_table_t* job = (job_table_t*)job_get(job_id);
    if (!job) return -1;

    for (int i = 0; i < job->nreqs; i++) {
        if (!job->reqs[i].granted) return 0;
    }

    return 1;
}

// Devuelve un string con la tabla de jobs para imprimir.
char* job_table_to_string() {
    if (!table_job) return NULL;

    size_t buf_tam =  40 + MAX_JOBS * (40 + MAX_JOB_RQ * 90);
    char *buf = malloc(buf_tam);
    if (!buf) return NULL;


    int written = 0;
    written += snprintf(buf + written, buf_tam - written, "{{{{{{{{{{{{{ job_table }}}}}}}}}}}}}\n");

    for (int i = 0; i < table_job->used; i++) {
        if (table_job->entries[i].value == NULL) continue;

        const job_table_t *job = (const job_table_t*)table_job->entries[i].value;
        written += snprintf(buf + written, buf_tam - written,
                           "{job_id %d: ", job->job_id);

        const job_req_t *r = &job->reqs[0];
        written += snprintf(buf + written, buf_tam - written,
                            "%s:%s res=%s amount=%d granted=%d",
                            r->dest_ip, r->dest_port, r->res, r->amount, r->granted);
        for (int j = 1; j < job->nreqs; j++) {
            r = &job->reqs[j];
            written += snprintf(buf + written, buf_tam - written,
                               ", %s:%s res=%s amount=%d granted=%d",
                               r->dest_ip, r->dest_port, r->res, r->amount, r->granted);
        }
        written += snprintf(buf + written, buf_tam - written, "}\n");
    }

    return buf;
}

// Marca el pedido del job correspondiente a 'ip' como 'val'
bool job_set_granted(int job_id, char* ip, char* port, int val) {
    job_table_t* job = (job_table_t*)job_get(job_id);
    if (!job || !ip) return false;

    for (int i = 0; i < job->nreqs; i++) {
        int same_ip = strncmp(job->reqs[i].dest_ip, ip, INET_ADDRSTRLEN) == 0;
        int same_port = strncmp(job->reqs[i].dest_port, port, PORTSTRLEN) == 0;
        if (same_ip && same_port) {
            job->reqs[i].granted = val;
            return true;
        }
    }
    return false;
}

