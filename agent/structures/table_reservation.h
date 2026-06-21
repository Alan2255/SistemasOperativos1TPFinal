#ifndef RESERVATION_MANAGER_H
#define RESERVATION_MANAGER_H

#include <stdbool.h>
#include "../agent.h"

// Job reservado
typedef struct {
    int job_id;
    int src_fd;      // Socket de conexión del nodo que hizo la reserva 
    char res[MAX_BYTES_NAME_RESOURCE]; 
    int amount; 
    int granted;     // 0 -> encolado, 1 -> concedido
} reservation_t;

// --- API de tabla de jobs reservados ---

// Crea la tabla de reservas
void reservation_manager_init(void);

// Destruye la tabla de reservas
void reservation_manager_shutdown(void);

// Agrega una reserva
bool reservation_manager_add(int job_id, int src_fd, const char* res_name, int amount, int granted);

// Elimina una reserva
bool reservation_manager_release(int job_id);

// Cambia el estado de una reserva
bool reservation_manager_set_granted(int job_id, int granted);

// Busca una reserva por ID 
const reservation_t* reservation_manager_get(int job_id);

#endif