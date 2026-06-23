#ifndef AGENT_MANAGER_H
#define AGENT_MANAGER_H

#include "../consts.h"
#include "fdinfo.h"
#include "local_resources.h"
#include <arpa/inet.h>
#include "hash.h" 

extern Hash *table_agent;

// Agente
typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
    FdInfo* fdinfo;
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

// Busca un agente por su ip y devuelve su socket si existe
FdInfo* agent_manager_get_fdinfo(char* ip);

// Busca un agente por su ip y actualiza su fdinfo
void agent_manager_set_fdinfo(const char *ip, FdInfo* newFdinfo);

// Busca un agente por su ip y actualiza sus recursos
void agent_manager_update(char* ip, Resource* resources);

// Busca un agente por su ip y devuelve su timerfd si existe
int agent_manager_get_timerfd(const char *ip);

// Busca un agente por su ip y actualiza su timerfd
void agent_manager_set_timerfd(const char *ip, int newTimerfd);

// Busca un agente por su ip y devuelve su puerto si existe
char* agent_manager_get_port(const char *ip);

// Elimina un agente
void agent_manager_delete(const char *ip);

// Convierte la tabla de agentes en un string con las capacidades de los recursos
char* agent_manager_get_nodes();

// Busca un agente por el fd de su conexion y copia su IP en ip_out
int agent_manager_get_ip_by_fd(int fd, char* ip_out);

#endif