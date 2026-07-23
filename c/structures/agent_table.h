#ifndef AGENT_table_H
#define AGENT_table_H

#include <stdint.h>
#include "../consts.h"
#include "local_resources.h"
#include <arpa/inet.h>
#include "hash.h"

#define KEY_LEN (INET_ADDRSTRLEN + PORTSTRLEN + 2)

extern Hash *agent_table;

// Agente
typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
    uint64_t id;
    int count_resources;
    Resource resources[MAX_RESOURCES_AGENT];
    int timerfd;
} AgentNode;

// --- API del tabla de agentes ---

// Crea la tabla de agentes.
void agent_table_init(void);

// Agrega un agente.
void agent_table_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd);

// Busca un agente por su ip y puerto y guarda su identificador en el parametro 'id'.
// Devuelve 1 en caso de exito, 0 si el agente no se encuentra, o -1 en caso de error.
int agent_table_get_id(char* ip, char* port, uint64_t* id);

// Busca un agente por su ip y puerto y actualiza el identificador de su conexion.
void agent_table_set_id(const char *ip, const char *port, uint64_t id);

// Busca el agente con el identificador dado y reinicia su id (UINT64_MAX).
void agent_table_clear_id(uint64_t id);

// Busca un agente por su ip y puerto y actualiza sus recursos.
void agent_table_update(char* ip, char* port, Resource* resources, int count_resources);

// Busca un agente por su ip y puerto y obtiene su timerfd. 
// Devuelve el timerfd si el agente se encuentra, 0 en caso contrario, o -1 en caso de error.
int agent_table_get_timerfd(const char *ip, const char *port);

// Busca un agente por su ip y puerto y actualiza su timerfd.
void agent_table_set_timerfd(const char *ip, const char *port, int newTimerfd);

// Elimina un agente.
void agent_table_delete(const char *ip, const char *port);

// Convierte la tabla de agentes en un string con las capacidades de los recursos.
char* agent_table_get_nodes();

// Busca un agente por el identificador de su conexion y copia su ip y puerto.
int agent_table_get_addr_by_id(uint64_t id, char* ip, char* port);

#endif