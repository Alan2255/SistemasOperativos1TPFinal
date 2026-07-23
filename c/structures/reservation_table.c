#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "reservation_table.h"

// Funcion hash
static void make_key(int job_id, uint64_t src_id, const char* res_name, char *out_key, size_t max_len) {
    snprintf(out_key, max_len, "%d:%" PRIu64 ":%s", job_id, src_id, res_name);
}

// Crea la tabla de reservas
void reservation_table_init(void) {
    if (reservation_table == NULL) {
        reservation_table = hash_create();
    }
}

reservation_t* make_reservation(int job_id, uint64_t src_id, const char* res_name, int amount, int granted) {
    reservation_t *new_reservation = malloc(sizeof(reservation_t));
    if (!new_reservation) {
        return NULL;
    }
    new_reservation->job_id = job_id;
    new_reservation->src_id = src_id;
    new_reservation->amount = amount;
    new_reservation->granted = granted;
    
    strncpy(new_reservation->res, res_name, MAX_BYTES_NAME_RESOURCE - 1);
    new_reservation->res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';

    return new_reservation;
}

// Agrega una reserva
bool reservation_table_add(int job_id, uint64_t src_id, const char* res_name, int amount, int granted) {
    if (!reservation_table || !res_name) return false;

    char key[32];
    make_key(job_id, src_id, res_name, key, sizeof(key));
    reservation_t* new_reservation = make_reservation(job_id, src_id, res_name, amount, granted);

    pthread_mutex_lock(&(reservation_table->mutex));
    hash_set(reservation_table, key, new_reservation);
    pthread_mutex_unlock(&(reservation_table->mutex));

    return true;
}

// Elimina una reserva
bool reservation_table_release(int job_id, uint64_t src_id, const char* res_name) {
    if (!reservation_table) return false;

    char key[32];
    make_key(job_id, src_id, res_name, key, sizeof(key));

    pthread_mutex_lock(&(reservation_table->mutex));
    bool result_remove = hash_remove(reservation_table, key, free);
    pthread_mutex_unlock(&(reservation_table->mutex));

    return result_remove;
}

// Cambia el estado de una reserva
bool reservation_table_set_granted(int job_id, uint64_t src_id, const char* res_name, int granted) {
    if (!reservation_table) return false;

    char key[32];
    make_key(job_id, src_id, res_name, key, sizeof(key));

    pthread_mutex_lock(&(reservation_table->mutex));
    reservation_t *reserva = hash_get(reservation_table, key, sizeof(reservation_t));

    if (reserva != NULL) {
        reserva->granted = granted;
    }
    pthread_mutex_unlock(&(reservation_table->mutex));

    return true;
}

// Busca una reserva por ID
reservation_t* reservation_table_get(int job_id, uint64_t src_id, const char* res_name) {
    if (!reservation_table) return NULL;

    char key[32];
    make_key(job_id, src_id, res_name, key, sizeof(key));

    pthread_mutex_lock(&(reservation_table->mutex));
    reservation_t* reservation = hash_get(reservation_table, key, sizeof(reservation_t));
    pthread_mutex_unlock(&(reservation_table->mutex));

    return reservation;
}

// Elimina todas las reservas asociadas a una conexion (src_id)
void reservation_table_release_by_id(uint64_t src_id) {
    if (!reservation_table) return;

    pthread_mutex_lock(&(reservation_table->mutex));

    // Recorremos el arreglo interno de la tabla hash
    for (int i = 0; i < reservation_table->used; i++) {
        reservation_t* reserva = (reservation_t*)reservation_table->entries[i].value;

        // Si la entrada tiene una reserva valida y coincide con la conexion
        if (reserva != NULL && reserva->src_id == src_id) {

            // Generamos la clave para eliminar la entrada de la tabla hash correctamente
            char key[32];
            make_key(reserva->job_id, reserva->src_id, reserva->res, key, sizeof(key));

            // Eliminamos la reserva de la tabla (libera su memoria)
            hash_remove(reservation_table, key, free);
        }
    }

    pthread_mutex_unlock(&(reservation_table->mutex));
}