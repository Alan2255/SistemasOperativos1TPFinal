#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "../agent.h"
#include "../structures/fdinfo.h"
#include "../structures/local_resources.h"

/* Retorna la estructura asociada al fd en epoll, o NULL en 
caso de error. */
FdInfo* epoll_add(int fd, fdtype type, int events);

/* Maneja el evento EPOLLOUT de un socket tcp */
int handle_tcp_epollout(FdInfo* info);

/* Maneja el evento en el timer para considerar a un 
nodo como caido */
void handle_node_timer(FdInfo* info);

/* Maneja el evento en el timer para lanzar el anuncio */
void handle_announce_timer(FdInfo* info);

/* Maneja un el intento de conexion del scheduler */
void handle_listen_scheduler(FdInfo* info);

/* Maneja un el intento de conexion de un agente */
void handle_agent_connect(FdInfo* info);

/* Maneja la recepcion de un mensaje de un agente */
void handle_agent_msg(FdInfo* info);

/* Maneja la desconexion inesperada de un agente */
void handle_agent_disconnect(FdInfo* info);

/* Maneja la recepcion de un anuncion es el socket udp */
void handle_announce(FdInfo* info);

/* Maneja la recepcion de un mensaje del scheduler */
int handle_scheduler(FdInfo *info);

/* Inicia el socket udp para la recepcion de anuncios */
int init_sock_udp();

/* Inicia el socket de escucha para conectar con el scheduler */
int init_listen_sock_scheduler();

/* Inicia el socket de escucha para conexiones con otros agentes */
int init_listen_sock_nodes();

/* Parsea el anuncio de un nodo y copia los datos en los parametros pasados. */
int parse_announce(char *msg, char *port, Resource *resources, int *res_count);

/* Parsea un comando enviado por otro nodo */
int parse_node_command(char *str, char *command_name, int *job_id, char *res, int* amount);

/* Envia el anuncio */ 
void send_announce();

/* Manda un mensaje un socket tcp, en caso de error retorna -1
    si se mando todo retorna 0, si no, guarda lo que queda en 'info',
    setea 'EPOLLOUT' en 'epollfd' y retorna 1*/
int send_msg_tcp(int fd, char *msg, int len_msg, FdInfo* info);

/* Inicia el timer (ya creado) con una cantidad en segundos */
int timerfd_start(int timerfd, int sec);

/* Separa el string en tokens y guarda una referencia a cada uno en 'tokens' */
int tokenize_str(char *str, char *delim, int max_tokens, char **tokens);

#endif /* FUNCTIONS_H */