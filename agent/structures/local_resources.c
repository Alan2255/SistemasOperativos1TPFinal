#include <stdio.h>
#include <string.h>
#include <stdbool.h>
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
int local_resorces_reserve(int job_id, int socket, char* resource_name, int amount) {
    Resource* res = find_resource(resource_name);
    if (res == NULL) -1;

    if (res->available >= amount) {
        res->available -= amount;
        return 0;
    } else {
        // No hay recursos por el momento, encola el job
        queue_push(&res->job_pendings, job_id, socket);
        return 1;
    }
}

int local_resorces_release(char* resource_name, int amount) {
    Resource* resource = find_resource(resource_name);
    if (resource == NULL) {
        return -1;
    }

    resource->available += amount;
    if (resource->available > resource->total_capacity) {
        resource->available = resource->total_capacity;
    }

    if (!queue_is_empty(&resource->job_pendings)) {
        int next_job_id = queue_top(&resource->job_pendings);
        reservation_t *reservation = reservation_manager_get(next_job_id);
        
        if (resource->available >= reservation->amount) {
            resource->available = resource->available - reservation->amount;            
            // reservation->granted = 1;
            return queue_pop(&resource->job_pendings); // Se atendió el siguiente pedido
        }
        else {
            return -2; // No pudo atenderse el siguiente pedido
        }
    }
    else {
        return 0; // No había trabajos pendientes
    }
}
