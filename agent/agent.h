#ifndef AGENT_H
#define AGENT_H

#define TAM_BUF 512
#define PUERTO_UDP 12529
#define PUERTO_TCP 8100
#define PUERTO_TCP_SCHED 8101
#define PORTSTRLEN 6
#define MAX_EVENTS 10 // Maximos eventos para la instacia epoll
#define MAX_PENDING_CONNECTIONS 3 // Maxima cantidad de 'connect' pendientes en el listen sock de nodos
#define MAX_LEN_COMMAND_AGENT 7// Maxima longitud del nombre de un comando entre nodos (como RELEASE O GRANTED)
#define MAX_LEN_REQUEST 512

#define MAX_BYTES_NAME_RESOURCE 10// Maxima cantidad de bytes para el nombre de un recurso

#define RESOURCES_LOCAL 3 // cantidad de recursos locales

#define MAX_NODES 64 // Maxima cantidad de nodos en la tabla de nodos
#define MAX_RESOURCES_NODE 8 // Maxima cantidad de recursos por nodo
#define NODE_TIMEOUT_SEC 15 // Tiempo (segundos) para considerar un nodo caido 
#define ANNOUNCE_SEC 5 // Tiempo (segundos) cada cuanto se va a enviar un anuncio

#define MAX_RESERVATIONS 256 // Maxima cantidad de reservas concedidos/encolados localmente 

#define MAX_JOBS 64 // Maxima cantidad de jobs activos del scheduler
#define MAX_JOB_RQ 8 // Maxima cantidad de "@NODE:RES:AMOUNT" en un "JOB_REQUEST ..."

#define NBYTES_PACKET_ERL 2 // cantidad de bytes que indican el tamano de los paquetes enviados desde erlang


struct FdInfo_s;
typedef struct FdInfo_s FdInfo;

extern int epollfd;
extern int sockudp;            // Socket para envio/recibo de anuncios
extern int scheduler_fd;       // Socket de conexion con el scheduler
extern FdInfo *scheduler_info;

#endif /* AGENT_H */