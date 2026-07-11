#ifndef RESERVATION_table_H
#define RESERVATION_table_H

#include <stdbool.h>
#include "../consts.h"
#include "hash.h"

extern Hash *table_reservation;

// Job reservado
typedef struct {
    int job_id;
    int src_fd;      // Socket de conexion del agente que hizo la reserva
    char res[MAX_BYTES_NAME_RESOURCE]; 
    int amount; 
    int granted;     // 0 -> encolado, 1 -> concedido
} reservation_t;

// --- API de tabla de jobs reservados ---

// Crea la tabla de reservas
void reservation_table_init(void);

// Destruye la tabla de reservas
void reservation_table_shutdown(void);

// Agrega una reserva
bool reservation_table_add(int job_id, int src_fd, const char* res_name, int amount, int granted);

// Elimina una reserva
bool reservation_table_release(int job_id);

// Cambia el estado de una reserva
bool reservation_table_set_granted(int job_id, int granted);

// Busca una reserva por ID 
reservation_t* reservation_table_get(int job_id);

// Elimina todas las reservas asociadas a un socket (src_fd)
void reservation_table_release_by_socket(int src_fd);

#endif