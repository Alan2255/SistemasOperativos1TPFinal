/* ------ Tabla de fds registrados en epoll, para coordinar su manejo entre threads ------
   Es una tabla hash (clave = fd) con la particularidad de que al cerrar una
   conexion la entrada no se borra, solo se marca libre (fd = -1) y se le
   libera 'data'. Asi, si el numero de fd se reutiliza para una conexion
   nueva, se usa la misma entrada y se incrementa su contador 'reuse'.

   El identificador de una conexion (lo que epoll entrega como data de
   un evento) esta compuesto por el fd en la mitad mas significativa y reuse en la menos. 
   Con eso se puede detectar y descartar eventos viejos de una conexion que ya no es la actual. 
*/
#ifndef FD_TABLE_H
#define FD_TABLE_H

#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "../consts.h"
#include "hash.h"

typedef enum {
    FD_SCHEDULER,
    FD_UDP,
    FD_AGENT,
    FD_NODE_TIMER,
    FD_SEND_ANNOUNCE_TIMER,
    FD_AGENTS_LISTEN,
    FD_SCHEDULER_LISTEN,
} fdtype;

typedef struct {
    char buf_in[TAM_BUF];
    int len_buf_in;
    char buf_out[TAM_BUF];
    int len_buf_out;
    pthread_mutex_t mutex_in;
    pthread_mutex_t mutex_out;

    // Coordinacion para el cierre del fd
    int ref_count;         // cantidad de threads usando actualmente este fd
    int close;             // 1 si se pidio cerrar el fd
    pthread_mutex_t mutex_state;
} fd_tcp_data;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
} fd_node_timer_data;

typedef struct {
    int fd;                // -1 si no esta en uso
    unsigned int reuse;    // cantidad de veces que se reutilizo esta entrada
    fdtype type;
    void *data;
    pthread_mutex_t mutex;
} FdEntry;

extern Hash *fd_table;

/* Inicializa la tabla. */
void fd_table_init();

/* Destruye la tabla. */
void fd_table_destroy();

/* Registra fd en la tabla. Si es la primera vez que se registra,
crea la entrada con 'reuse' en 0; si ya existia, se usa esa entrada y
se incrementa 'reuse'. 
Retorna el identificador de la entrada, o UINT64_MAX en caso de error. */
uint64_t fd_table_add(int fd, fdtype type);

/* Opuesta de fd_table_add, reinicia la entrada de la tabla correspondiente 
al fd  (fd = -1, libera 'data'), no elimina la entrada de la tabla ni cierra el fd. */
void fd_table_undo_add(int fd);

/* Recibe el identificador (fd + reuse count), busca la entrada por el fd,
si la entrada tiene fd en -1 o su contador 'reuse' no coincide con el del 
identificador, retorna NULL. Si la entrada es de tipo FD_SCHEDULER o FD_AGENT
y no fue marcada para cerrar, le suma 1 a su ref_count y retorna la entrada. */
FdEntry* fd_table_get_and_inc(uint64_t id);

/* Opuesta de fd_table_get_and_inc, resta 1 al ref_count. Si llega a 0 y la entrada
estaba marcada para cerrar, lo saca de epoll, cierra el fd y libera la entrada. */
void fd_table_dec_and_release(FdEntry *entry);

/* Pide cerrar el fd. Si nadie lo esta usando (ref_count == 0, o el tipo
no tiene ref-count) lo cierra inmediatamente; si no, queda marcado y lo
cierra la ultima llamada a fd_table_dec_and_release. */
void fd_table_request_close(FdEntry *entry);

#endif /* FD_TABLE_H */
