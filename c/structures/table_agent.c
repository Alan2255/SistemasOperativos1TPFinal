#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "table_agent.h"


static void make_key(const char *ip, const char *port, char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "%s:%s", ip, port);
}

// Crea la tabla de agentes
void agent_table_init() {
    if (table_agent == NULL) {
        table_agent = hash_create();
    }
}

// Destruye la tabla de agentes
void agent_table_shutdown() {
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
void agent_table_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd) {
    if (!table_agent) return;

    AgentNode *nuevo_nodo = malloc(sizeof(AgentNode));
    if (!nuevo_nodo) return; 
    
    strncpy(nuevo_nodo->ip, ip, INET_ADDRSTRLEN - 1);
    nuevo_nodo->ip[INET_ADDRSTRLEN - 1] = '\0';
    
    strncpy(nuevo_nodo->port, port, PORTSTRLEN - 1);
    nuevo_nodo->port[PORTSTRLEN - 1] = '\0';
    
    nuevo_nodo->id = UINT64_MAX;
    nuevo_nodo->count_resources = count_resources;
    nuevo_nodo->timerfd = timerfd;

    if (resources != NULL && count_resources > 0) {
        int a_copiar = (count_resources > MAX_RESOURCES_AGENT) ? MAX_RESOURCES_AGENT : count_resources;
        
        for (int i = 0; i < a_copiar; i++) {
            strncpy(nuevo_nodo->resources[i].name, resources[i].name, MAX_BYTES_NAME_RESOURCE - 1);
            nuevo_nodo->resources[i].name[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            
            // Copia directa de los enteros (valores primitivos)
            nuevo_nodo->resources[i].total_capacity = resources[i].total_capacity;
            nuevo_nodo->resources[i].available = resources[i].available; 
        }
    }

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    hash_set(table_agent, key, nuevo_nodo);
}

// Busca un agente por su ip y puerto y lo devuelve si existe
AgentNode* agent_table_get(char* ip, char* port) {
    if (!table_agent) return NULL;
    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    return hash_get(table_agent, key);
}

// Busca un agente por su ip y puerto y devuelve el identificador de su
// conexion, o UINT64_MAX si no tiene una conexion activa
uint64_t agent_table_get_id(char* ip, char* port) {
    if (!table_agent) return UINT64_MAX;
    AgentNode *node = agent_table_get(ip, port);
    if (node == NULL) return UINT64_MAX;
    return node->id;
}

// Busca un agente por su ip y puerto y actualiza el identificador de su conexion
void agent_table_set_id(const char *ip, const char *port, uint64_t id) {
    if (!table_agent) return;

    AgentNode *agente = agent_table_get((char*)ip, (char*)port);
    if (agente == NULL) return;

    agente->id = id;
}

// Busca el agente con el identificador dado y reinicia su id (UINT64_MAX)
void agent_table_clear_id(uint64_t id) {
    if (!table_agent || id == UINT64_MAX) return;

    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].key == NULL) continue;
        AgentNode *agente = (AgentNode*)table_agent->entries[i].value;
        if (agente->id == id) {
            agente->id = UINT64_MAX;
            return;
        }
    }
}

// Busca un agente por su ip y puerto y actualiza sus recursos
void agent_table_update(char* ip, char* port, Resource* resources) {
    if (!table_agent || resources == NULL) return;

    AgentNode *agente = agent_table_get(ip, port);
    if (agente != NULL) {
        memcpy(agente->resources, resources, agente->count_resources * sizeof(Resource));
    }
}

// Busca un agente por su ip y puerto y devuelve su timerfd si existe
int agent_table_get_timerfd(const char *ip, const char *port) {
    if (!table_agent) return -2;

    AgentNode *agente = agent_table_get((char*)ip, (char*)port);
    if (agente == NULL) {
        return -2;
    }

    return agente->timerfd;
}

// Busca un agente por su ip y puerto y actualiza su timerfd
void agent_table_set_timerfd(const char *ip, const char *port, int newTimerfd) {
    if (!table_agent) return;

    AgentNode *agente = agent_table_get((char*)ip, (char*)port);
    if (agente == NULL) return;

    agente->timerfd = newTimerfd;
}

// Elimina un agente
void agent_table_delete(const char *ip, const char *port) {
    if (!table_agent) return;

    AgentNode *agente = agent_table_get((char*)ip, (char*)port);
    if (agente != NULL) {
        free(agente);
    }

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    hash_remove(table_agent, key);
}

// Busca un agente por el identificador de su conexion y copia su ip y puerto
int agent_table_get_addr_by_id(uint64_t id, char* ip, char* port) {
    if (!table_agent || !ip || !port || id == UINT64_MAX) return -1;

    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].key == NULL) continue;
        AgentNode *agente = (AgentNode*)table_agent->entries[i].value;
        if (agente->id == id) {
            strncpy(ip, agente->ip, INET_ADDRSTRLEN - 1);
            ip[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(port, agente->port, PORTSTRLEN - 1);
            port[PORTSTRLEN - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

// Convierte la tabla de agentes en un string con las capacidades de los recursos
char* agent_table_get_nodes() {
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
                                       agente->resources[j].available);
            
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
