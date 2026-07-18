#ifndef LOCAL_RESOURCES_H
#define LOCAL_RESOURCES_H

#include <stdint.h>
#include <pthread.h>
#include "../consts.h"
#include "reservation_table.h"

// Cola de trabajos pendientes
typedef struct {
    char name[MAX_BYTES_NAME_RESOURCE];  // Nombre del recurso de la cola
    int job_ids[MAX_RESERVATIONS];       // Array donde guardamos los IDs de los jobs en espera
    uint64_t src_ids[MAX_RESERVATIONS];  // Array donde guardamos el id (fd+reuse) de conexion de los pedidos
    int front;                           // Índice al primer elemento (para desencolar)
    int rear;                            // Índice al último elemento (para encolar)
    int count;                           // Cuántos jobs hay esperando actualmente
} JobQueue;

// Pedido encolado que quedo concedido en local_resources_release
typedef struct {
    int job_id;
    uint64_t src_id;
} pending_grant_t;

// Recursos locales
typedef struct {
    char name[MAX_BYTES_NAME_RESOURCE];
    int total_capacity;
    int available;
    JobQueue job_pendings; // La cola de jobs en espera para ESTE recurso específico
    pthread_mutex_t mutex;
} Resource;

extern Resource* resources;
extern int resource_count;

// --- API de los recursos locales ---

// Crea los recursos locales
void local_resources_init(int num_resources, char* resource_names[], int capacities[]);

// Si hay suficiente cantidad reserva los recursos, sino encola el job
int local_resources_reserve(int job_id, uint64_t src_id, char* resource_name, int amount);

// Recupera los recursos y atiende pedidos pendientes. Para los pedidos encolados que se concedieron,
// se guarda el job_id y el id de la conexion en 'grants'.
void local_resources_release(int job_id, uint64_t src_id, char* resource_name, int amount,
                              pending_grant_t* grants, int* ngrants);

// Destruye los recursos locales
void local_resources_shutdown();

// Devuelve un string con los recursos locales
void local_resources_to_str(char* buff);

#endif