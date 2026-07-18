#ifndef RESERVATION_table_H
#define RESERVATION_table_H

#include <stdbool.h>
#include <stdint.h>
#include "../consts.h"
#include "hash.h"

extern Hash *reservation_table;

// Job reservado
typedef struct {
    int job_id;
    uint64_t src_id; // Id (fd+reuse) de la conexion que hizo la reserva
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
bool reservation_table_add(int job_id, uint64_t src_id, const char* res_name, int amount, int granted);

// Elimina una reserva
bool reservation_table_release(int job_id, uint64_t src_id, const char* res_name);

// Cambia el estado de una reserva
bool reservation_table_set_granted(int job_id, uint64_t src_id, const char* res_name, int granted);

// Busca una reserva por ID y devuelve un puntero a la misma si existe
reservation_t* reservation_table_get(int job_id, uint64_t src_id, const char* res_name);

// Elimina todas las reservas asociadas a una conexion (src_id)
void reservation_table_release_by_id(uint64_t src_id);

#endif