#ifndef AGENT_H
#define AGENT_H

#include <arpa/inet.h>

/* ---------------------- Constantes ---------------------- */
#define TAM_BUF 512
#define PUERTO_UDP 12529
#define PUERTO_TCP 8100
#define PORTSTRLEN 6
#define MAX_EVENTS 10 // Maximos eventos para la instacia epoll
#define MAX_PENDING_CONNECTIONS // Maxima cantidad de 'connect' pendientes en el listen sock de nodos

#define MAX_BYTES_NAME_RESOURCE 10// Maxima cantidad de bytes para el nombre de un recurso

#define RESOURCES_LOCAL 3 // cantidad de recursos locales

#define MAX_NODES 64 // Maxima cantidad de nodos en la tabla de nodos
#define MAX_RESOURCES_NODE 8 // Maxima cantidad de recursos por nodo
#define NODE_TIMEOUT_SEC 15 // Tiempo (segundos) para considerar un nodo caido 

#define MAX_RESERVATIONS 256 // Maxima cantidad de reservas concedidos/encolados localmente 

#define MAX_JOBS 64 // Maxima cantidad de jobs activos del scheduler
#define MAX_JOB_RQ 8 // Maxima cantidad de "@NODE:RES:AMOUNT" en un "JOB_REQUEST ..."


/* ---------- Definiciones para recursos locales ---------- */
// FALTA LA DEFINICION DE COLAS O INCLUIR EN EL HEADER
typedef struct {
    char name[MAX_BYTES_NAME_RESOURCE];
    int total;
    int available;
    // Queue queue;
} local_resource_t;

// local_resource_t local_resources[RESOURCES_LOCAL];


/* ---------- Definiciones para la tabla de nodos ---------- */
struct resource_node { 
    char name[MAX_BYTES_NAME_RESOURCE];
    int amount; 
};

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
    struct resource_node resources[MAX_RESOURCES_NODE];
    int nresources; // cantidad de recursos
    int timerfd;
} node_table_t; // entrada de la tabla de nodos

// node_table_t node_table[MAX_NODES];

/* ---- Definiciones para la tabla de reservas activas ---- */
// Reservas activas: reservas que se recibieron desde otro agente y se concedieron/encolaron */
typedef struct {
    int job_id; 
    int src_fd; // socket de conexion del nodo que hizo la reserva 
    char res[MAX_BYTES_NAME_RESOURCE]; 
    int amount; 
    int granted; // 0->encolado, 1->concedido
} reservation_t;

// reservation_t reservation_table[MAX_RESERVATIONS]; 

/* ---- Definiciones para la tabla de jobs activos ---- */
// Jobs activos: peticiones que se recibieron del scheduler y no se liberaron  */
typedef struct { 
    int dest_fd; // socket de conexion del nodo al que va la peticion
    char res[MAX_BYTES_NAME_RESOURCE];
    int amount; 
} job_req_t;

typedef struct {
    int job_id;
    job_req_t reqs[MAX_JOB_RQ];
    int nreqs;
} job_table_t; // entrada de la tabla de jobs

// job_table_t job_table[MAX_JOBS];


#endif /* AGENT_H */