#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <stdbool.h>
#include <arpa/inet.h>
#include "../agent.h"

// Request de un recurso
typedef struct { 
    char dest_ip[INET_ADDRSTRLEN];
    char res[MAX_BYTES_NAME_RESOURCE];
    int amount; 
    int granted;
} job_req_t;

// Job activo
typedef struct {
    int job_id;
    job_req_t reqs[MAX_JOB_RQ];
    int nreqs;
} job_table_t;

// --- API de tabla de jobs activos ---

// Crea la tabla de jobs
void job_manager_init(void);

// Destruye la tabla de jobs
void job_manager_shutdown(void);

// Agrega un job
bool job_add(int job_id, int nreqs, const job_req_t *reqs);

// Elimina un job
bool job_release(int job_id);

// Busca un job por ID y lo devuelve si existe
const job_table_t* job_get(int job_id);

// Chequea si todos los requisitos del job están concedidos
int job_check_granted(int job_id);


#endif