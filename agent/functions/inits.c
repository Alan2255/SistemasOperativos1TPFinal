#include <sys/socket.h>
#include "../agent.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Inicia el socket udp para la recepcion de anuncios */
int init_sock_udp() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_UDP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_UDP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}

/* Inicia el socket de escucha para conectar con el scheduler */
int init_listen_sock_scheduler() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_STREAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a localhost y puerto PUERTO_TCP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Lo ponemos en modo escucha */
    if (listen(socket, 1) == -1)
        return -1;

    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}

/* Inicia el socket de escucha para conexiones con otros agentes */
int init_listen_sock_nodes() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_STREAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_TCP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Lo ponemos en modo escucha para nuevas conexiones*/
    listen(socket, MAX_PENDING_CONNECTIONS);

    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}