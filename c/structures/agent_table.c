#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent_table.h"

// Funcion hash.
static void make_key(const char *ip, const char *port, char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "%s:%s", ip, port);
}

// Crea la tabla de agentes.
void agent_table_init() {
    if (agent_table == NULL) {
        agent_table = hash_create();
    }
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

// Agrega un agente a la tabla.
void agent_table_add(char* ip, char* port, int count_resources, Resource* resources, int timerfd) {
    if (!agent_table) return;
    
    // Creamos la clave
    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    // Creamos el nuevo agente
    AgentNode* new_node = make_agent(ip, port, count_resources, resources, timerfd, UINT64_MAX);

    pthread_mutex_lock(&(agent_table->mutex));

    // Insertamos el agente en la tabla
    void* old_node = hash_set(agent_table, key, new_node);

    // Si pisamos una entrada de la tabla hay que liberar el nodo pisado
    if (old_node != NULL) free(old_node);

    pthread_mutex_unlock(&(agent_table->mutex));

    return;
}

// Busca un agente por su ip y puerto y guarda su identificador en el parametro 'id'.
// Devuelve 1 en caso de exito, 0 si el agente no se encuentra, o -1 en caso de error.
int agent_table_get_id(char* ip, char* port, uint64_t* id) {
    if (!agent_table) return -1;
    
    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(agent_table->mutex));

    AgentNode* agent = hash_get(agent_table, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return 0;
    }

    *id = agent->id;

    pthread_mutex_unlock(&(agent_table->mutex));
    
    return 1;
}

// Busca un agente por su ip y puerto y actualiza el identificador de su conexion.
void agent_table_set_id(const char *ip, const char *port, uint64_t id) {
    if (!agent_table) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(agent_table->mutex));

    AgentNode* agent = hash_get(agent_table, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return;
    }
    
    agent->id = id;

    pthread_mutex_unlock(&(agent_table->mutex));
}

// Busca el agente con el identificador dado y reinicia su id (UINT64_MAX).
void agent_table_clear_id(uint64_t id) {
    if (!agent_table || id == UINT64_MAX) return;

    pthread_mutex_lock(&(agent_table->mutex));

    for (int i = 0; i < agent_table->used; i++) {
        if (agent_table->entries[i].key == NULL) continue;
        AgentNode *agente = (AgentNode*)agent_table->entries[i].value;
        if (agente->id == id) {
            agente->id = UINT64_MAX;
            pthread_mutex_unlock(&(agent_table->mutex));
            return;
        }
    }

    pthread_mutex_unlock(&(agent_table->mutex));
}

// Busca un agente por su ip y puerto y actualiza sus recursos.
void agent_table_update(char* ip, char* port, Resource* resources, int count_resources) {
    if (!agent_table || resources == NULL) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(agent_table->mutex));

    AgentNode* agent = hash_get(agent_table, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return;
    }
    
    // Reemplazamos los recursos de la copia
    if (resources != NULL && count_resources > 0) {
        int a_copiar = (count_resources > MAX_RESOURCES_AGENT) ? MAX_RESOURCES_AGENT : count_resources;
        agent->count_resources = a_copiar;
        
        for (int i = 0; i < a_copiar; i++) {
            strncpy(agent->resources[i].name, resources[i].name, MAX_BYTES_NAME_RESOURCE - 1);
            agent->resources[i].name[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            agent->resources[i].total_capacity = resources[i].total_capacity;
            agent->resources[i].available = resources[i].available; 
        }
    }

    pthread_mutex_unlock(&(agent_table->mutex));
}

// Busca un agente por su ip y puerto y obtiene su timerfd. 
// Devuelve el timerfd si el agente se encuentra, 0 en caso contrario, o -1 en caso de error.
int agent_table_get_timerfd(const char *ip, const char *port) {
    if (!agent_table) return -1;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    
    pthread_mutex_lock(&(agent_table->mutex));

    AgentNode* agent = hash_get(agent_table, key, sizeof(AgentNode));
    if (!agent) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return 0;
    }

    int timer_fd = agent->timerfd;

    pthread_mutex_unlock(&(agent_table->mutex));
    
    return timer_fd;
}

// Busca un agente por su ip y puerto y actualiza su timerfd.
void agent_table_set_timerfd(const char *ip, const char *port, int newTimerfd) {
    if (!agent_table) return;

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));

    pthread_mutex_lock(&(agent_table->mutex));
    
    AgentNode* agent = hash_get(agent_table, key, sizeof(AgentNode));
    
    if (!agent) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return;
    }

    agent->timerfd = newTimerfd;

    pthread_mutex_unlock(&(agent_table->mutex));
}

// Elimina un agente.
void agent_table_delete(const char *ip, const char *port) {
    if (!agent_table) return;

    pthread_mutex_lock(&(agent_table->mutex));

    char key[KEY_LEN];
    make_key(ip, port, key, sizeof(key));
    hash_remove(agent_table, key, free);

    pthread_mutex_unlock(&(agent_table->mutex));
}

// Busca un agente por el identificador de su conexion y copia su ip y puerto.
int agent_table_get_addr_by_id(uint64_t id, char* ip, char* port) {
    if (!agent_table || !ip || !port || id == UINT64_MAX) return -1;

    pthread_mutex_lock(&(agent_table->mutex));

    for (int i = 0; i < agent_table->used; i++) {
        if (agent_table->entries[i].key == NULL) continue;
        AgentNode *agent = (AgentNode*)agent_table->entries[i].value;
        if (agent->id == id) {
            strncpy(ip, agent->ip, INET_ADDRSTRLEN - 1);
            ip[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(port, agent->port, PORTSTRLEN - 1);
            port[PORTSTRLEN - 1] = '\0';
            pthread_mutex_unlock(&(agent_table->mutex));
            return 0;
        }
    }
    pthread_mutex_unlock(&(agent_table->mutex));
    return -1;
}

// Convierte la tabla de agentes en un string con las capacidades de los recursos.
char* agent_table_get_nodes() {
    if (!agent_table || agent_table->used == 0) {
        char *vacio = malloc(6); 
        if (vacio) strcpy(vacio, "NODES");
        return vacio;
    }

    pthread_mutex_lock(&(agent_table->mutex));

    // NODES + <ip:port:resource:capacity...>
    size_t tamano_maximo = 6 + (agent_table->used * 150) + 1;
    char *resultado = malloc(tamano_maximo);
    if (!resultado) {
        pthread_mutex_unlock(&(agent_table->mutex));
        return NULL;
    }

    strcpy(resultado, "NODES ");

    int agentes_agregados = 0;

    for (int i = 0; i < agent_table->used; i++) {
        Entry entrada = agent_table->entries[i];
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
    
    pthread_mutex_unlock(&(agent_table->mutex));
    
    return resultado; 
}
