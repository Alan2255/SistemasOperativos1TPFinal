#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "table_reservation.h"


// Función hash
static void fun_hash(int job_id, char *out_key, size_t max_len) {
    snprintf(out_key, max_len, "%d", job_id);
}

// Crea la tabla de reservas
void reservation_table_init(void) {
    if (table_reservation == NULL) {
        table_reservation = hash_create();
    }
}

// Destruye la tabla de reservas
void reservation_table_shutdown(void) {
    if (!table_reservation) return;

    pthread_mutex_lock(&(table_reservation->mutex));

    for (int i = 0; i < table_reservation->used; i++) {
        if (table_reservation->entries[i].value != NULL) {
            free(table_reservation->entries[i].value);
            table_reservation->entries[i].value = NULL;
        }
    }

    hash_destroy(table_reservation, free);

    pthread_mutex_unlock(&(table_reservation->mutex));
}

reservation_t* make_reservation(int job_id, int src_fd, const char* res_name, int amount, int granted) {
    reservation_t *new_reservation = malloc(sizeof(reservation_t));
    if (!new_reservation) {
        return NULL;
    }
    new_reservation->job_id = job_id;
    new_reservation->src_fd = src_fd;
    new_reservation->amount = amount;
    new_reservation->granted = granted;
    
    strncpy(new_reservation->res, res_name, MAX_BYTES_NAME_RESOURCE - 1);
    new_reservation->res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';

    return new_reservation;
}

// Agrega una reserva
bool reservation_table_add(int job_id, int src_fd, const char* res_name, int amount, int granted) {
    if (!table_reservation || !res_name) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    reservation_t* new_reservation = make_reservation(job_id, src_fd, res_name, amount, granted);

    pthread_mutex_lock(&(table_reservation->mutex));

    reservation_t* old_node = hash_set(table_reservation, key, new_reservation);
    if (old_node != NULL) {
        // Este free evita leaks de memoria si pisamos una entrada en la tabla de reservas
        // No deberia ocurrir nunca que se pise una entrada
        free(old_node);
    }
    
    pthread_mutex_unlock(&(table_reservation->mutex));
    return true;
}

// Elimina una reserva
bool reservation_table_release(int job_id) {
    if (!table_reservation) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    pthread_mutex_lock(&(table_reservation->mutex));

    bool result_remove = hash_remove(table_reservation, key, free);

    pthread_mutex_unlock(&(table_reservation->mutex));
    
    return result_remove;
}

// Cambia el estado de una reserva
bool reservation_table_set_granted(int job_id, int granted) {
    if (!table_reservation) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));
    
    pthread_mutex_lock(&(table_reservation->mutex));

    reservation_t *reserva = (reservation_t*)hash_get(table_reservation, key, sizeof(reservation_t));

    if (!reserva) {
        pthread_mutex_unlock(&(table_reservation->mutex));
        return false;
    }

    reserva->granted = granted;
    reservation_t* old_reservation = hash_set(table_reservation, key, reserva);
    free(old_reservation);

    pthread_mutex_unlock(&(table_reservation->mutex));
    return true;
}

// Busca una reserva por ID 
reservation_t* reservation_table_get(int job_id) {
    if (!table_reservation) return NULL;
    
    char key[32];
    fun_hash(job_id, key, sizeof(key));

    pthread_mutex_lock(&(table_reservation->mutex));

    reservation_t* reservation = hash_get(table_reservation, key, sizeof(reservation_t));

    pthread_mutex_unlock(&(table_reservation->mutex));

    return reservation;
}

// Elimina todas las reservas asociadas a un socket (src_fd)
void reservation_table_release_by_socket(int src_fd) {
    if (!table_reservation) return;

    pthread_mutex_lock(&(table_reservation->mutex));

    // Recorremos el arreglo interno de la tabla hash
    for (int i = 0; i < table_reservation->used; i++) {
        reservation_t* reserva = (reservation_t*)table_reservation->entries[i].value;
        
        // Si la entrada tiene una reserva válida y coincide con el socket
        if (reserva != NULL && reserva->src_fd == src_fd) {
            
            // Generamos la clave para eliminar la entrada de la tabla hash correctamente
            char key[32];
            fun_hash(reserva->job_id, key, sizeof(key)); 
            
            // Eliminamos la reserva de la tabla (libera su memoria)
            hash_remove(table_reservation, key, free);
        }
    }

    pthread_mutex_unlock(&(table_reservation->mutex));
}