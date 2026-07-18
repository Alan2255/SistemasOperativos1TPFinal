#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "local_resources.h"

// Queue
static void queue_init(JobQueue *q, char* res_name) {
    strcpy(q->name, res_name);
    q->front = 0;
    q->rear = -1;
    q->count = 0;
}

static bool queue_is_full(JobQueue *q) {return q->count == MAX_RESERVATIONS;}

static bool queue_is_empty(JobQueue *q) {return q->count == 0;}

static bool queue_push(JobQueue *q, int job_id, uint64_t src_id) {
    if (queue_is_full(q)) {
        return false;
    }
    q->rear = (q->rear + 1) % MAX_RESERVATIONS;
    q->job_ids[q->rear] = job_id;
    q->src_ids[q->rear] = src_id;
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
    return q->front;
}

// Busca un recurso y lo devuelve un puntero al mismo si existe
static Resource* find_resource(char* name) {
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
        
        queue_init(&resources[i].job_pendings, resource_names[i]);

        pthread_mutex_init(&resources[i].mutex, NULL);
    }
}

void local_resources_shutdown() {
    if (resources != NULL) {
        for (int i = 0; i < resource_count; i++) {
            pthread_mutex_destroy(&resources[i].mutex);
        }
        free(resources);
        resources = NULL;
    }
    resource_count = 0;
}

// Si hay suficiente cantidad reserva los recursos, sino encola el job
int local_resources_reserve(int job_id, uint64_t src_id, char* resource_name, int amount) {
    Resource* resource = find_resource(resource_name);
    if (resource == NULL) return -1;

    pthread_mutex_lock(&resource->mutex);

    // Reserva los recursos
    if (resource->total_capacity < amount) {
        pthread_mutex_unlock(&resource->mutex);
        return -1;
    }
    else if (resource->available >= amount) {
        resource->available -= amount;
        pthread_mutex_unlock(&resource->mutex);
        return 0;
    } else {
        // No hay recursos por el momento, encola el job
        queue_push(&resource->job_pendings, job_id, src_id);
        pthread_mutex_unlock(&resource->mutex);
        return 1;
    }
}

// Recupera los recursos y atiende pedidos pendientes. Para los pedidos encolados que se concedieron,
// se guarda el job_id y el id de la conexion en 'grants'.
void local_resources_release(int job_id, uint64_t src_id, char* resource_name, int amount,
                              pending_grant_t* grants, int* ngrants) {
    if (ngrants) *ngrants = 0;

    Resource* resource = find_resource(resource_name);
    if (!resource) return;

    reservation_t * reservation = reservation_table_get(job_id, src_id, resource_name);
    if (!reservation) return;

    pthread_mutex_lock(&resource->mutex);

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
        queue_init(&aux, resource_name);

        // Elimina un elemento de la cola (no necesariamente el primero)
        while (!queue_is_empty(&resource->job_pendings)) {
            int current_front = resource->job_pendings.front;
            int curr_id = resource->job_pendings.job_ids[current_front];
            uint64_t curr_src_id = resource->job_pendings.src_ids[current_front];

            queue_pop(&resource->job_pendings);

            if (curr_id == job_id && curr_src_id == src_id) {
                continue;
            }
            queue_push(&aux, curr_id, curr_src_id);
        }

        while (!queue_is_empty(&aux)) {
            int current_front = aux.front;
            queue_push(&resource->job_pendings, aux.job_ids[current_front], aux.src_ids[current_front]);
            queue_pop(&aux);
        }
    }

    // Atiende los pedidos pendientes
    while (!queue_is_empty(&resource->job_pendings)) {
        int next_job_idx = queue_top(&resource->job_pendings);

        int next_job_id = (resource->job_pendings).job_ids[next_job_idx];
        uint64_t next_src_id = (resource->job_pendings).src_ids[next_job_idx];
        char res_name[MAX_BYTES_NAME_RESOURCE];
        strcpy(res_name, (resource->job_pendings).name);

        reservation_t* next_res = reservation_table_get(next_job_id, next_src_id, res_name);

        if (next_res == NULL) {
            queue_pop(&resource->job_pendings);
            continue;
        }

        if (resource->available >= next_res->amount) {
            queue_pop(&resource->job_pendings);
            resource->available -= next_res->amount;

            reservation_table_set_granted(next_job_id, next_src_id, res_name, 1);

            if (grants && ngrants && *ngrants < MAX_RESERVATIONS) {
                grants[*ngrants].job_id = next_job_id;
                grants[*ngrants].src_id = next_src_id;
                (*ngrants)++;
            }
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&resource->mutex);
}

// Devuelve un string con los recursos locales
void local_resources_to_str(char* buff) {
    if (resources == NULL || resource_count == 0 || buff == NULL) {
        return;
    }

    char* ptr = buff;
    pthread_mutex_lock(&resources[0].mutex);
    int written = sprintf(ptr, "%s:%d", resources[0].name, resources[0].available);
    pthread_mutex_unlock(&resources[0].mutex);
    ptr += written;

    for (int i = 1; i < resource_count; i++) {
        pthread_mutex_lock(&resources[i].mutex);
        written = sprintf(ptr, " %s:%d", resources[i].name, resources[i].available);
        pthread_mutex_unlock(&resources[i].mutex);
        ptr += written;
    }
    *ptr = '\0';
}