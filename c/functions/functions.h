#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <sys/epoll.h>
#include <pthread.h>
#include "../consts.h"
#include "../structures/fd_table.h"
#include "../structures/local_resources.h"

extern uint16_t puerto_tcp;
extern int epollfd;
extern int udp_sock;            // Socket para envio/recibo de anuncios
extern int scheduler_fd;       // Socket de conexion con el scheduler
extern uint64_t scheduler_id;

/* Obtiene el puerto (se lo asigna a 'puerto_tcp) y los recursos (los guarda en los parametros) desde la linea de comandos.
En caso de error por formato, imprime indicando el formato y devuelve -1. */
int get_port_and_resources(int argc, char **argv, int *num_resources, char **resource_names, int *capacities);

/* Registra en la instancia epoll un fd con el identificador y eventos pasados. 
Retorna -1 en caso de error. */
int epoll_add(int fd, int events, uint64_t id);

/* Modifica los eventos de un fd ya registrado en epoll.
Retorna -1 en caso de error. */
int epoll_mod(int fd, int events, uint64_t id);

/* Inicia un socket no bloqueante del tipo dado, lo bindea a la direccion dada 
y lo agrega a la instancia epoll con el tipo de dato dado. */
int init_sock(int type, int ip, int port, fdtype typedata);

/* Inicia el socket udp para la recepcion de anuncios. */
int init_udp_sock();

/* Inicia el socket de escucha para conexiones con otros agentes. */
int init_agents_listen_sock();

/* Inicia el socket de escucha para conexion con el scheduler.  */
int init_scheduler_listen_sock();

/* Inicia el timer que dispara el reenvio periodico del anuncio y lo
agrega a la instancia epoll. Retorna el timerfd creado, o -1 en caso
de error. */
int init_announce_timer(void);

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

/* Maneja la recepcion de un mensaje de un agente. */
void handle_agent(uint64_t id, FdEntry* info);

/* Maneja la desconexion inesperada de un agente. */
void handle_agent_disconnect(FdEntry* info);

/* Maneja la recepcion de un anuncion en el socket udp. */
void handle_announce(FdEntry* info);

/* Maneja la recepcion de un mensaje del scheduler. */
int handle_scheduler(uint64_t id, FdEntry *info);

/* Parsea el anuncio de un nodo y copia los datos en los parametros pasados. */
int parse_announce(char *msg, char *port, Resource *resources, int *res_count);

/* Parsea un comando enviado por otro nodo. */
int parse_node_command(char *str, char *command_name, int *job_id, char *res, int* amount);

/* Envia el anuncio. */ 
void send_announce();

/* Anade el string al buffer 'buf_out' de 'info'. */
int add_to_buffer(FdEntry* info, char* msg, int len);

/* Inicia el timer (ya creado) con una cantidad en segundos. */
int timerfd_start(int timerfd, int sec);

/* Separa el string en tokens y guarda una referencia a cada uno en 'tokens'. */
int tokenize_str(char *str, char *delim, int max_tokens, char **tokens);

#endif /* FUNCTIONS_H */