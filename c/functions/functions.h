#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <sys/epoll.h>
#include <pthread.h>
#include "../consts.h"
#include "../structures/fd_table.h"
#include "../structures/local_resources.h"
#include "../structures/table_job.h"

extern uint16_t puerto_tcp;
extern int epollfd;
extern int udp_sock;            // Socket para envio/recibo de anuncios
extern int scheduler_fd;       // Socket de conexion con el scheduler
extern uint64_t scheduler_id;

/* Obtiene el puerto (se lo asigna a 'puerto_tcp) y los recursos (los guarda en los parametros) desde la linea de comandos. 
En caso de error por formato, imprime indicando el formato y devuelve -1. */
int get_port_and_resources(int argc, char **argv, int *num_resources, char **resource_names, int *capacities);

/* Inicia un socket no bloqueante del tipo dado, lo bindea a la direccion dada 
y lo agrega a la instancia epoll con el tipo de dato dado. */
int init_sock(int type, int ip, int port, fdtype typedata);

/* Inicia el socket udp para la recepcion de anuncios. */
int init_udp_sock();

/* Inicia el socket de escucha para conexiones con otros agentes. */
int init_agents_listen_sock();

/* Inicia el socket de escucha para conexion con el scheduler. */
int init_scheduler_listen_sock();

/* Inicia el timer que dispara el reenvio periodico del anuncio y lo
agrega a la instancia epoll. Retorna el timerfd creado, o -1 en caso
de error. */
int init_announce_timer(void);

/* Registra en la instancia epoll un fd con el identificador y eventos pasados. 
Retorna -1 en caso de error. */
int epoll_add(int fd, int events, uint64_t id);

/* Modifica los eventos de un fd ya registrado en epoll.
Retorna -1 en caso de error. */
int epoll_mod(int fd, int events, uint64_t id);

/* Envia el anuncio. */
void send_announce();

/* Inicia el timer (ya creado) con una cantidad en segundos. */
int timer_start(int timerfd, int sec);

/* Event loop de la instancia epoll para correr en cada thread. */
void* event_loop(void*);

/* Maneja el evento EPOLLOUT de un socket tcp. */
int handle_tcp_epollout(uint64_t id, FdEntry* info);

/* Maneja el evento en el timer para considerar a un
nodo como caido. */
void handle_node_timer(FdEntry* info);

/* Maneja el evento en el timer para lanzar el anuncio. */
void handle_announce_timer(FdEntry* info);

/* Maneja un el intento de conexion del scheduler. */
void handle_listen_scheduler(FdEntry* info);

/* Maneja un el intento de conexion de un agente. */
void handle_agent_connect(FdEntry* info);

/* Maneja la recepcion de un anuncion en el socket udp. */
void handle_announce(FdEntry* info);

/* Maneja la recepcion de un mensaje de un agente. */
void handle_agent(uint64_t id, FdEntry* info);

/* Maneja la recepcion de un mensaje del scheduler. */
int handle_scheduler(uint64_t id, FdEntry *info);

/* Cierra la conexion de un agente: limpia su id en table_agent, libera
las reservas de recursos locales que se le concedieron o quedaron encoladas
y pide el cierre del fd. */
void close_agent_conn(uint64_t id, FdEntry* info);

/* Cierra la conexion del scheduler: libera todos los jobs de table_job
(mandando "RELEASE" a cada agente que se le pidio), pide el cierre del fd y
reinicia scheduler_id. */
void close_scheduler_conn(FdEntry* info);

/* Manda "RELEASE ..." a cada agente de 'job'. No toca table_job: el
llamador es responsable de haber sacado 'job' de la tabla (con
job_table_extract/job_table_extract_all) y de liberarlo despues de
llamar a esta funcion. */
void release_job(const job_table_t *job);

#endif /* FUNCTIONS_H */