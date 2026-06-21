#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"
#include "table_reservation.h"

static Hash *table_reservation = NULL;

// Función hash
static void fun_hash(int job_id, char *out_key, size_t max_len) {
    snprintf(out_key, max_len, "%d", job_id);
}

// Crea la tabla de reservas
void reservation_manager_init(void) {
    if (table_reservation == NULL) {
        table_reservation = hash_create();
    }
}

// Destruye la tabla de reservas
void reservation_manager_shutdown(void) {
    if (!table_reservation) return;

    for (int i = 0; i < table_reservation->used; i++) {
        if (table_reservation->entries[i].value != NULL) {
            free(table_reservation->entries[i].value);
            table_reservation->entries[i].value = NULL;
        }
    }

    hash_destroy(table_reservation);
    table_reservation = NULL;
}

// Agrega una reserva
bool reservation_manager_add(int job_id, int src_fd, const char* res_name, int amount, int granted) {
    if (!table_reservation || !res_name) return false;

    reservation_t *nueva_reserva = malloc(sizeof(reservation_t));
    if (!nueva_reserva) return false;

    nueva_reserva->job_id = job_id;
    nueva_reserva->src_fd = src_fd;
    nueva_reserva->amount = amount;
    nueva_reserva->granted = granted;
    
    strncpy(nueva_reserva->res, res_name, MAX_BYTES_NAME_RESOURCE - 1);
    nueva_reserva->res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    if (!hash_set(table_reservation, key, nueva_reserva)) {
        free(nueva_reserva);
        return false;
    }

    return true;
}

// Elimina una reserva
bool reservation_manager_release(int job_id) {
    if (!table_reservation) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    reservation_t *reserva = (reservation_t*)hash_get(table_reservation, key);
    if (reserva != NULL) {
        free(reserva);
    }

    return hash_remove(table_reservation, key);
}

// Cambia el estado de una reserva
bool reservation_manager_set_granted(int job_id, int granted) {
    if (!table_reservation) return false;

    char key[32];
    fun_hash(job_id, key, sizeof(key));
    
    reservation_t *reserva = (reservation_t*)hash_get(table_reservation, key);
    if (!reserva) return false;

    reserva->granted = granted;
    return true;
}

// Busca una reserva por ID 
const reservation_t* reservation_manager_get(int job_id) {
    if (!table_reservation) return NULL;

    char key[32];
    fun_hash(job_id, key, sizeof(key));

    return (const reservation_t*)hash_get(table_reservation, key);
}
