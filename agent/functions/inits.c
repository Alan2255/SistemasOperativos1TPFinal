#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <stdlib.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Inicia el socket udp para la recepcion de anuncios */
int init_sock_udp() {
    /* Creamos el socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el
    socket */
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                    sizeof(yes)) == -1)
        return -1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &yes,
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_UDP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_UDP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *)&addr,
                sizeof(addr)) == -1)
        return -1;

    /* Agregamos a la instancia epoll */
    if (epoll_add(sockfd, FD_UDP, EPOLLIN) == NULL)
        return -1;

    return sockfd;
}

/* Inicia el socket de escucha para conectar con el scheduler */
int init_listen_sock_scheduler() {
    /* Creamos el socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el
    socket */
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a localhost y puerto PUERTO_TCP_SCHED */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP_SCHED);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sockfd, (struct sockaddr *)&addr,
                sizeof(addr)) == -1)
        return -1;

    /* Lo ponemos en modo escucha */
    if (listen(sockfd, 1) == -1)
        return -1;

    /* Agregamos a la instancia epoll */
    if (epoll_add(sockfd, FD_LISTEN_SCHEDULER, EPOLLIN) == NULL)
        return -1;

    return sockfd;
}

/* Inicia el socket de escucha para conexiones con otros agentes */
int init_listen_sock_nodes() {
    /* Creamos el socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el
    socket */
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_TCP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, (struct sockaddr *)&addr,
                sizeof(addr)) == -1)
        return -1;

    /* Lo ponemos en modo escucha para nuevas conexiones*/
    if (listen(sockfd, MAX_PENDING_CONNECTIONS) == -1)
        return -1;

    /* Agregamos a la instancia epoll */
    if (epoll_add(sockfd, FD_LISTEN_NODE, EPOLLIN) == NULL)
        return -1;

    return sockfd;
}
