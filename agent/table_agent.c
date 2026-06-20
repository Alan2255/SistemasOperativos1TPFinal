#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h" 
#include "table_agent.h"

static Hash *table_agent = NULL;

// Función hash
static unsigned long fun_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Crea la tabla de agentes
void agent_manager_init() {
    if (table_agent == NULL) {
        table_agent = hash_create();
    }
}

// Destruye la tabla de agentes
void agent_manager_shutdown() {
    if (!table_agent) return;
    
    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].value != NULL) {
            free(table_agent->entries[i].value);
            table_agent->entries[i].value = NULL;
        }
    }

    hash_destroy(table_agent);
    table_agent = NULL;
}

// Agrega un agente
void agent_manager_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd) {
    if (!table_agent) return;

    AgentNode *nuevo_nodo = malloc(sizeof(AgentNode));
    if (!nuevo_nodo) return; 
    
    strncpy(nuevo_nodo->ip, ip, INET_ADDRSTRLEN - 1);
    nuevo_nodo->ip[INET_ADDRSTRLEN - 1] = '\0';
    
    strncpy(nuevo_nodo->port, port, PORTSTRLEN - 1);
    nuevo_nodo->port[PORTSTRLEN - 1] = '\0';
    
    nuevo_nodo->count_resources = count_resources;
    nuevo_nodo->timerfd = timerfd;

    if (resources != NULL && count_resources > 0) {
        int a_copiar = (count_resources > MAX_RESOURCES) ? MAX_RESOURCES : count_resources;
        memcpy(nuevo_nodo->resources, resources, a_copiar * sizeof(Resource));
    }

    hash_set(table_agent, ip, nuevo_nodo);
}

// Busca un agente por su ip y actualiza sus recursos
void agent_manager_update(char* ip, Resource* resources) {
    if (!table_agent || resources == NULL) return;

    AgentNode *agente = agent_manager_get(ip);
    if (agente != NULL) {
        memcpy(agente->resources, resources, agente->count_resources * sizeof(Resource));
    }
}

// Busca un agente por su ip y devuelve su timerfd
int agent_manager_get_timerfd(const char *ip) {
    if (!table_agent) return -2;

    AgentNode *agente = agent_manager_get((char*)ip);
    if (agente == NULL) {
        return -2; 
    }
    
    return agente->timerfd;
}

// Elimina un agente
void agent_manager_delete(const char *ip) {
    if (!table_agent) return;

    AgentNode *agente = agent_manager_get((char*)ip);
    if (agente != NULL) {
        free(agente);
    }

    hash_remove(table_agent, ip);
}

// Convierte la tabla de agentes en un string con las capacidades de los recursos
char* agent_manager_get_nodes() {
    if (!table_agent || table_agent->used == 0) {
        char *vacio = malloc(6); 
        if (vacio) strcpy(vacio, "NODES");
        return vacio;
    }

    // NODES + <ip:port:resource:capacity...>
    size_t tamano_maximo = 6 + (table_agent->used * 150) + 1;
    char *resultado = malloc(tamano_maximo);
    if (!resultado) return NULL;

    strcpy(resultado, "NODES ");

    int agentes_agregados = 0;

    for (int i = 0; i < table_agent->used; i++) {
        Entry entrada = table_agent->entries[i];
        if (entrada.key == NULL) continue;

        AgentNode *agente = (AgentNode*)entrada.value;
        
        char fragmento_agente[150]; 
        int escrito;

        if (agentes_agregados == 0) {
            escrito = snprintf(fragmento_agente, sizeof(fragmento_agente), "%s:%s", agente->ip, agente->port);
        } else {
            escrito = snprintf(fragmento_agente, sizeof(fragmento_agente), ";%s:%s", agente->ip, agente->port);
        }
        
        for (int j = 0; j < agente->count_resources; j++) {
            char fragmento_recurso[32];
            int res_escrito = snprintf(fragmento_recurso, sizeof(fragmento_recurso), ":%s:%d", 
                                       agente->resources[j].name, 
                                       agente->resources[j].total_capacity);
            
            if (escrito + res_escrito < (int)sizeof(fragmento_agente)) {
                strcat(fragmento_agente, fragmento_recurso);
                escrito += res_escrito;
            }
        }
        
        strcat(resultado, fragmento_agente);
        agentes_agregados++;
    }
    
    return resultado; 
}
