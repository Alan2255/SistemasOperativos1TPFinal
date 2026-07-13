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

    pthread_mutex_lock(&(table_agent->mutex));

    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].value != NULL) {
            free(table_agent->entries[i].value);
            table_agent->entries[i].value = NULL;
        }
    }

    hash_destroy(table_agent, free);
    table_agent = NULL;

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Crea un agente, retorna NULL en caso en error.
static AgentNode* make_agent(char* ip, char* port, int count_resources, Resource* resources, int timerfd, uint64_t id) {
    AgentNode *new_node = malloc(sizeof(AgentNode));
    if (!new_node) {
        return NULL; 
    }

    strncpy(new_node->ip, ip, INET_ADDRSTRLEN - 1);
    new_node->ip[INET_ADDRSTRLEN - 1] = '\0';
    
    strncpy(new_node->port, port, PORTSTRLEN - 1);
    new_node->port[PORTSTRLEN - 1] = '\0';
    
    new_node->id = id;
    new_node->count_resources = count_resources;
    new_node->timerfd = timerfd;

    if (resources != NULL && count_resources > 0) {
        int a_copiar = (count_resources > MAX_RESOURCES_AGENT) ? MAX_RESOURCES_AGENT : count_resources;
        
        for (int i = 0; i < a_copiar; i++) {
            strncpy(new_node->resources[i].name, resources[i].name, MAX_BYTES_NAME_RESOURCE - 1);
            new_node->resources[i].name[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            
            // Copia directa de los enteros (valores primitivos)
            new_node->resources[i].total_capacity = resources[i].total_capacity;
            new_node->resources[i].available = resources[i].available; 
        }
    }

    return new_node;
}

// Agrega un agente a la tabla
void agent_table_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd) {
    if (!table_agent) return;
    
    // Creamos la clave
    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    // Creamos el nuevo agente
    AgentNode* new_node = make_agent(ip, port, count_resources, resources, timerfd, UINT64_MAX);

    pthread_mutex_lock(&(table_agent->mutex));

    // Insertamos el agente en la tabla
    void* old_node = hash_set(table_agent, key, new_node);

    // Si pisamos una entrada de la tabla hay que liberar el nodo pisado
    if (old_node != NULL) free(old_node);

    pthread_mutex_unlock(&(table_agent->mutex));

    return;
}

// Busca un agente por su ip y puerto y devuelve una copia del mismo si existe,
// en caso contrario o error devuelve NULL.
AgentNode* agent_table_get(char* ip, char* port) {
    if (!table_agent) return NULL;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(table_agent->mutex));

    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode));

    pthread_mutex_unlock(&(table_agent->mutex));

    return agent;
}

// Busca un agente por su ip y puerto y guarda su identificador en el parametro 'id'.
// Devuelve 1 en caso de exito, 0 si el agente no se encuentra, o -1 en caso de error.
int agent_table_get_id(char* ip, char* port, uint64_t* id) {
    if (!table_agent) return -1;
    
    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(table_agent->mutex));

    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return 0;
    }

    pthread_mutex_unlock(&(table_agent->mutex));
    
    *id = agent->id;
    free(agent);

    return 1;
}

// Busca un agente por su ip y puerto y actualiza el identificador de su conexion
void agent_table_set_id(const char *ip, const char *port, uint64_t id) {
    if (!table_agent) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(table_agent->mutex));

    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode)); // copia
    
    if (!agent) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return;
    }
    
    agent->id = id;

    AgentNode* old_agent = hash_set(table_agent, key, agent);
    free(old_agent);

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Busca el agente con el identificador dado y reinicia su id (UINT64_MAX)
void agent_table_clear_id(uint64_t id) {
    if (!table_agent || id == UINT64_MAX) return;

    pthread_mutex_lock(&(table_agent->mutex));

    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].key == NULL) continue;
        AgentNode *agente = (AgentNode*)table_agent->entries[i].value;
        if (agente->id == id) {
            agente->id = UINT64_MAX;
            pthread_mutex_unlock(&(table_agent->mutex));
            return;
        }
    }

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Busca un agente por su ip y puerto y actualiza sus recursos
void agent_table_update(char* ip, char* port, Resource* resources, int count_resources) {
    if (!table_agent || resources == NULL) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(table_agent->mutex));

    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode)); // copia
    
    if (!agent) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return;
    }
    
    // Reemplazamos los recursos de la copia
    if (resources != NULL && count_resources > 0) {
        int a_copiar = (count_resources > MAX_RESOURCES_AGENT) ? MAX_RESOURCES_AGENT : count_resources;
        
        for (int i = 0; i < a_copiar; i++) {
            strncpy(agent->resources[i].name, resources[i].name, MAX_BYTES_NAME_RESOURCE - 1);
            agent->resources[i].name[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            agent->resources[i].total_capacity = resources[i].total_capacity;
            agent->resources[i].available = resources[i].available; 
        }
    }

    AgentNode* old_agent = hash_set(table_agent, key, agent);
    free(old_agent);

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Busca un agente por su ip y puerto y obtiene su timerfd. 
// Devuelve el timerfd si el agente se encuentra, 0 en caso contrario, o -1 en caso de error.
int agent_table_get_timerfd(const char *ip, const char *port) {
    if (!table_agent) return -1;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    
    pthread_mutex_lock(&(table_agent->mutex));

    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode));
    if (!agent) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return 0;
    }

    int timer_fd = agent->timerfd;
    free(agent);

    pthread_mutex_unlock(&(table_agent->mutex));
    
    return timer_fd;
}

// Busca un agente por su ip y puerto y actualiza su timerfd
void agent_table_set_timerfd(const char *ip, const char *port, int newTimerfd) {
    if (!table_agent) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(table_agent->mutex));
    
    AgentNode* agent = hash_get(table_agent, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return;
    }

    agent->timerfd = newTimerfd;

    AgentNode* old_agent = hash_set(table_agent, key, agent);
    free(old_agent);

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Elimina un agente
void agent_table_delete(const char *ip, const char *port) {
    if (!table_agent) return;

    pthread_mutex_lock(&(table_agent->mutex));

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    hash_remove(table_agent, key, free);

    pthread_mutex_unlock(&(table_agent->mutex));
}

// Busca un agente por el identificador de su conexion y copia su ip y puerto
int agent_table_get_addr_by_id(uint64_t id, char* ip, char* port) {
    if (!table_agent || !ip || !port || id == UINT64_MAX) return -1;

    pthread_mutex_lock(&(table_agent->mutex));

    for (int i = 0; i < table_agent->used; i++) {
        if (table_agent->entries[i].key == NULL) continue;
        AgentNode *agent = (AgentNode*)table_agent->entries[i].value;
        if (agent->id == id) {
            strncpy(ip, agent->ip, INET_ADDRSTRLEN - 1);
            ip[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(port, agent->port, PORTSTRLEN - 1);
            port[PORTSTRLEN - 1] = '\0';
            pthread_mutex_unlock(&(table_agent->mutex));
            return 0;
        }
    }
    pthread_mutex_unlock(&(table_agent->mutex));
    return -1;
}

// Convierte la tabla de agentes en un string con las capacidades de los recursos
char* agent_table_get_nodes() {
    if (!table_agent || table_agent->used == 0) {
        char *vacio = malloc(6); 
        if (vacio) strcpy(vacio, "NODES");
        return vacio;
    }

    pthread_mutex_lock(&(table_agent->mutex));

    // NODES + <ip:port:resource:capacity...>
    size_t tamano_maximo = 6 + (table_agent->used * 150) + 1;
    char *resultado = malloc(tamano_maximo);
    if (!resultado) {
        pthread_mutex_unlock(&(table_agent->mutex));
        return NULL;
    }

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
    
    pthread_mutex_unlock(&(table_agent->mutex));
    
    return resultado; 
}
