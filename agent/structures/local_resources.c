#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "local_resources.h"

static Resource* resources = NULL;
static int resource_count = 0;

// Queue
static void queue_init(JobQueue *q) {
    q->front = 0;
    q->rear = -1;
    q->count = 0;
}

static bool queue_is_full(JobQueue *q) {return q->count == MAX_RESERVATIONS;}

static bool queue_is_empty(JobQueue *q) {return q->count == 0;}

static bool queue_push(JobQueue *q, int job_id, int socket) {
    if (queue_is_full(q)) {
        return false;
    }
    q->rear = (q->rear + 1) % MAX_RESERVATIONS;
    q->job_ids[q->rear] = job_id;
    q->sockets[q->rear] = socket;
    q->count++;

    return true;
}

static int queue_pop(JobQueue *q) {
    if (queue_is_empty(q)) {
        return -1;
    }
    int job_id = q->job_ids[q->front];
    q->front = (q->front + 1) % MAX_RESERVATIONS;
    q->count--;
    return job_id;
}

static int queue_top(JobQueue *q) {
    if (queue_is_empty(q)) {
        return -1;
    }
    return q->job_ids[q->front];
}

// Busca un recurso y lo devuelve un puntero al mismo si existe
static Resource* find_resource(const char* name) {
    for (int i = 0; i < resource_count; i++) {
        if (strncmp(resources[i].name, name, MAX_BYTES_NAME_RESOURCE) == 0) {
            return &resources[i];
        }
    }
    return NULL;
}

void local_resources_init(int num_resources, char* resource_names[], int capacities[]) {
    if (resources != NULL) {
        local_resources_shutdown();
    }

    resource_count = num_resources;
    resources = (Resource*) malloc(sizeof(Resource) * resource_count);
    
    if (resources == NULL) {
        resource_count = 0;
        return;
    }

    for (int i = 0; i < resource_count; i++) {
        strncpy(resources[i].name, resource_names[i], MAX_BYTES_NAME_RESOURCE - 1);
        resources[i].name[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
        
        resources[i].total_capacity = capacities[i];
        resources[i].available = capacities[i];
        
        queue_init(&resources[i].job_pendings);
    }
}

void local_resources_shutdown() {
    if (resources != NULL) {
        free(resources);
        resources = NULL;
    }
    resource_count = 0;
}

// Si hay suficiente cantidad reserva los recursos, sino encola el job
int local_resources_reserve(int job_id, int socket, char* resource_name, int amount) {
    Resource* resource = find_resource(resource_name);
    if (resource == NULL) return -1;

    // Reserva los recursos
    if (resource->available >= amount) {
        resource->available -= amount;
        return 0;
    } else {
        // No hay recursos por el momento, encola el job
        queue_push(&resource->job_pendings, job_id, socket);
        return 1;
    }
}

// Recupera los recursos y atiende pedidos pendientes
void local_resources_release( int job_id, int source_fd, char* resource_name, int amount) {
    Resource* resource = find_resource(resource_name);
    if (!resource) return;

    reservation_t * reservation = reservation_manager_get(job_id);
    if (!reservation) return;

    // Recupera los recursos utilizados
    if (reservation->granted == 1) {
        resource->available += amount;
        if (resource->available > resource->total_capacity) {
            resource->available = resource->total_capacity;
        }
    }
    // No se utilizaron y se cancelo la espera
    else {
        JobQueue aux;
        queue_init(&aux);

        // Elimina un elemento de la cola (no necesariamente el primero)
        while (!queue_is_empty(&resource->job_pendings)) {
            int current_front = resource->job_pendings.front;
            int curr_id = resource->job_pendings.job_ids[current_front];
            int curr_sock = resource->job_pendings.sockets[current_front];
            
            queue_pop(&resource->job_pendings);

            if (curr_id == job_id && curr_sock == source_fd) {
                continue;
            }
            queue_push(&aux, curr_id, curr_sock);
        }

        while (!queue_is_empty(&aux)) {
            int current_front = aux.front;
            queue_push(&resource->job_pendings, aux.job_ids[current_front], aux.sockets[current_front]);
            queue_pop(&aux);
        }
    }

    // Atiende los pedidos pendientes 
    while (!queue_is_empty(&resource->job_pendings)) {
        int next_job_id = queue_top(&resource->job_pendings);
        reservation_t* next_res = reservation_manager_get(next_job_id);
        
        if (next_res == NULL) {
            queue_pop(&resource->job_pendings);
            continue;
        }

        if (resource->available >= next_res->amount) {
            queue_pop(&resource->job_pendings);
            resource->available -= next_res->amount;
            reservation_manager_set_granted(next_job_id, 1);
        } else {
            break;
        }
    }
}

// Devuelve un string con los recursos locales
void local_resources_to_str(char* buff) {
    if (resources == NULL || resource_count == 0 || buff == NULL) {
        return;
    }

    char* ptr = buff;
    for (int i = 0; i < resource_count; i++) {
        int written = sprintf(ptr, "%s:%d ", resources[i].name, resources[i].total_capacity);
        ptr += written;
    }
}