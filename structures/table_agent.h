#ifndef AGENT_MANAGER_H
#define AGENT_MANAGER_H

#include "../agent/agent.h"

// Recurso
typedef struct {
    char name[MAX_BYTES_NAME_RESOURCE];
    int total_capacity;
    int aviable;
} Resource;

// Agente
typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
    int count_resources;
    Resource resources[MAX_RESOURCES_NODE];
    int timerfd;
} AgentNode;

// --- API del tabla de agentes ---

// Crea la tabla de agentes
void agent_manager_init(void);

// Destruye la tabla de agentes
void agent_manager_shutdown(void);

// Agrega un agente
void agent_manager_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd);

// Busca un agente por su ip y devuelve un puntero al mismo si existe
AgentNode* agent_manager_get(char* ip);

// Busca un agente por su ip y actualiza sus recursos
void agent_manager_update(char* ip, Resource* resources);

// Busca un agente por su ip y devuelve su timerfd
int agent_manager_get_timerfd(const char *ip);

// Elimina un agente
void agent_manager_delete(const char *ip);

// Convierte la tabla de agentes en un string con las capacidades de los recursos
char* agent_manager_get_nodes(void);

#endif