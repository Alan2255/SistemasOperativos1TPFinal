#ifndef LOCAL_RESOURCES_H
#define LOCAL_RESOURCES_H

#include "../agent/agent.h"
#include "table_reservation.h"

// Cola de trabajos pendientes
typedef struct {
    int job_ids[MAX_RESERVATIONS]; // Array donde guardamos los IDs de los jobs en espera
    int sockets[MAX_RESERVATIONS]; // Array donde guardamos los sockets de los pedidos
    int front;                     // Índice al primer elemento (para desencolar)
    int rear;                      // Índice al último elemento (para encolar)
    int count;                     // Cuántos jobs hay esperando actualmente
} JobQueue;

// Recursos locales
typedef struct {
    char name[MAX_BYTES_NAME_RESOURCE];
    int total_capacity;
    int available;
    JobQueue job_pendings; // La cola de jobs en espera para ESTE recurso específico
} Resource;

// --- API de los recursos locales ---

// Crea los recursos locales
void local_resources_init(int num_resources, char* resource_names[], int capacities[]);

// Si hay suficiente cantidad reserva los recursos, sino encola el job
int local_resorces_reserve(int job_id, int socket, char* resource_name, int amount);

// Recupera la cantidad de recursos que el job estaba usando y atiende pedidos pendientes
// ¿En qué consiste atender los pedidos pendientes?
void local_resorces_release(int job_id, char* resource_name, int amount);

// Destruye los recursos locales
void local_resources_shutdown();

#endif