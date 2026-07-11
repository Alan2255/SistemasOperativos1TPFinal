#ifndef JOB_table_H
#define JOB_table_H

#include <stdbool.h>
#include <arpa/inet.h>
#include "../consts.h"
#include "hash.h"

extern Hash *table_job;

// Request de un recurso
typedef struct { 
    char dest_ip[INET_ADDRSTRLEN];
    char dest_port[PORTSTRLEN];
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
void job_table_init(void);

// Destruye la tabla de jobs
void job_table_shutdown(void);

// Agrega un job
bool job_table_add(int job_id, int nreqs, const job_req_t *reqs);

// Elimina un job
bool job_table_release(int job_id);

// Busca un job por ID y lo devuelve si existe
const job_table_t* job_table_get(int job_id);

// Chequea si todos los requisitos del job están concedidos
int job_table_check_granted(int job_id);

// Marca el pedido del job correspondiente al ip y puerto como 'val'
bool job_table_set_granted(int job_id, char* ip, char* port, int val);

// Devuelve un string con la tabla de jobs para imprimir.
char* job_table_to_string();

#endif