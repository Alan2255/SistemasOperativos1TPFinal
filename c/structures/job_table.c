#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "job_table.h"

static void make_key(int job_id, char *out_key, size_t max_len) {
    snprintf(out_key, max_len, "%d", job_id);
}

// Crea la tabla de jobs
void job_table_init(void) {
    if (job_table == NULL) {
        job_table = hash_create();
    }
}

job_table_t* make_job(int job_id, int nreqs, const job_req_t *reqs) {
    int cantidad_a_copiar = (nreqs > MAX_JOB_RQ) ? MAX_JOB_RQ : nreqs;

    job_table_t *new_job = malloc(sizeof(job_table_t));
    if (!new_job) {
        return NULL;
    }

    new_job->job_id = job_id;
    new_job->nreqs = cantidad_a_copiar;
    new_job->ngranted = 0;
    memcpy(new_job->reqs, reqs, cantidad_a_copiar * sizeof(job_req_t));

    return new_job;
}

// Agrega un job
bool job_table_add(int job_id, int nreqs, const job_req_t *reqs) {
    if (!job_table || !reqs || nreqs <= 0) return false;
    
    char key[32];
    make_key(job_id, key, sizeof(key));
    job_table_t* new_job = make_job(job_id, nreqs, reqs);

    pthread_mutex_lock(&(job_table->mutex));
    hash_set(job_table, key, new_job);
    pthread_mutex_unlock(&(job_table->mutex));

    return true;
}

// Elimina un job
bool job_table_release(int job_id) {
    if (!job_table) return false;

    char key[32];
    make_key(job_id, key, sizeof(key));

    pthread_mutex_lock(&(job_table->mutex));
    bool remove_result = hash_remove(job_table, key, free);
    pthread_mutex_unlock(&(job_table->mutex));

    return remove_result;
}

// Busca un job por ID y lo devuelve si existe
job_table_t* job_table_get(int job_id) {
    if (!job_table) return NULL;

    char key[32];
    make_key(job_id, key, sizeof(key));

    pthread_mutex_lock(&(job_table->mutex));
    job_table_t* job = hash_get(job_table, key, sizeof(job_table_t));
    pthread_mutex_unlock(&(job_table->mutex));

    return job;
}

// Chequea si todos los requisitos del job están concedidos
int job_table_check_granted(int job_id) {
    if (!job_table) return -2;
    
    char key[32];
    make_key(job_id, key, sizeof(key));

    pthread_mutex_lock(&(job_table->mutex));

    job_table_t* job = hash_get(job_table, key, sizeof(job_table_t));

    if (!job) {
        pthread_mutex_unlock(&(job_table->mutex));
        return -1;
    }

    pthread_mutex_unlock(&(job_table->mutex));
    return job->nreqs == job->ngranted;
}

// Devuelve un string con la tabla de jobs para imprimir.
char* job_table_to_string() {
    if (!job_table) return NULL;

    pthread_mutex_lock(&(job_table->mutex));

    size_t buf_tam =  40 + MAX_JOBS * (40 + MAX_JOB_RQ * 90);
    char *buf = malloc(buf_tam);
    if (!buf) {
        pthread_mutex_unlock(&(job_table->mutex));
        return NULL;
    }


    int written = 0;
    written += snprintf(buf + written, buf_tam - written, "{{{{{{{{{{{{{ job_table }}}}}}}}}}}}}\n");

    for (int i = 0; i < job_table->used; i++) {
        if (job_table->entries[i].value == NULL) continue;

        job_table_t *job = (job_table_t*)job_table->entries[i].value;
        written += snprintf(buf + written, buf_tam - written,
                           "{job_id %d ngranted=%d/%d: ", job->job_id, job->ngranted, job->nreqs);

        const job_req_t *r = &job->reqs[0];
        written += snprintf(buf + written, buf_tam - written,
                            "%s:%s res=%s amount=%d",
                            r->dest_ip, r->dest_port, r->res, r->amount);
        for (int j = 1; j < job->nreqs; j++) {
            r = &job->reqs[j];
            written += snprintf(buf + written, buf_tam - written,
                               ", %s:%s res=%s amount=%d",
                               r->dest_ip, r->dest_port, r->res, r->amount);
        }
        written += snprintf(buf + written, buf_tam - written, "}\n");
    }

    pthread_mutex_unlock(&(job_table->mutex));
    return buf;
}

// Incrementa la cantidad de pedidos concedidos del job
bool job_table_inc_ngranted(int job_id) {
    if (!job_table) return false;

    char key[32];
    make_key(job_id, key, sizeof(key));

    pthread_mutex_lock(&(job_table->mutex));

    job_table_t* job = hash_get(job_table, key, sizeof(job_table_t));

    if (!job) {
        pthread_mutex_unlock(&(job_table->mutex));
        return false;
    }

    job->ngranted++;

    pthread_mutex_unlock(&(job_table->mutex));
    return true;
}

// Saca de la tabla, sin liberar su memoria, el job con 
// correspondiente al job_id dado.
job_table_t* job_table_extract(int job_id) {
    if (!job_table) return NULL;

    pthread_mutex_lock(&(job_table->mutex));

    job_table_t *job = NULL;
    for (int i = 0; i < job_table->used; i++) {
        job_table_t *candidato = (job_table_t*)job_table->entries[i].value;
        if (candidato != NULL && candidato->job_id == job_id) {
            job = candidato;
            free(job_table->entries[i].key);
            job_table->entries[i].key = NULL;
            job_table->entries[i].value = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&(job_table->mutex));

    return job;
}

// Saca de la tabla, sin liberar su memoria, todos los jobs. Retorna 
// un arreglo dinamico con los jobs y terminado en NULL.
job_table_t** job_table_extract_all() {
    if (!job_table) return NULL;

    pthread_mutex_lock(&(job_table->mutex));

    if (job_table->used == 0) {
        pthread_mutex_unlock(&(job_table->mutex));
        return NULL;
    }

    job_table_t **jobs = malloc((job_table->used + 1) * sizeof(job_table_t*));
    if (jobs != NULL) {
        int n = 0;
        for (int i = 0; i < job_table->used; i++) {
            if (job_table->entries[i].value == NULL) continue;
            jobs[n++] = (job_table_t*)job_table->entries[i].value;
            free(job_table->entries[i].key);
            job_table->entries[i].key = NULL;
            job_table->entries[i].value = NULL;
        }
        jobs[n] = NULL;
    }

    pthread_mutex_unlock(&(job_table->mutex));

    return jobs;
}
