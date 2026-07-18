#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <stdlib.h>
#include "../consts.h"
#include "../structures/fd_table.h"
#include "functions.h"
#include <asm-generic/socket.h>
#include <bits/time.h>

/* Inicia un socket no bloqueante del tipo dado, lo bindea a la direccion dada 
y lo agrega a la instancia epoll con el tipo de dato dado. */
int init_sock(int type, int ip, int port, fdtype typedata) {
    // Creamos el socket
    int sock = socket(AF_INET, type | SOCK_NONBLOCK, 0);
    if (sock == -1)
        return -1;

    // Seteamos opcion para bindear inmediatamente el socket a
    // la direccion y puerto pasados 
    int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes,
                    sizeof(yes)) == -1)
        return -1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes,
                    sizeof(yes)) == -1)
        return -1;

    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ip);
    addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        return -1;

    // Agregamos el socket a la tabla de conexiones
    uint64_t id = fd_table_add(sock, typedata);
    if (id == UINT64_MAX)
        return -1;

    // Agregamos el socket a la instancia epoll
    if (epoll_add(sock, EPOLLET | EPOLLIN, id) == -1) {
        fd_table_undo_add(sock);
        return -1;
    }

    return sock;
}

/* Inicia el socket udp para la recepcion de anuncios. */
int init_udp_sock() {
    int sock = init_sock(SOCK_DGRAM, INADDR_ANY, PUERTO_UDP, FD_UDP);

    // Seteamos el udp_sock para enviar enviar mensajes a INADDR_BROADCAST
    int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1)
        return -1;

    return sock;
}

/* Inicia el socket de escucha para conexiones con otros agentes. */
int init_agents_listen_sock() {
    int sock = init_sock(SOCK_STREAM, INADDR_ANY, puerto_tcp, FD_AGENTS_LISTEN);

    // Lo ponemos en modo escucha para nuevas conexiones
    if (listen(sock, MAX_PENDING_CONNECTIONS) == -1)
        return -1;

    return sock;
}

/* Inicia el socket de escucha para conexion con el scheduler. */
int init_scheduler_listen_sock() {
    int sock = init_sock(SOCK_STREAM, INADDR_LOOPBACK, puerto_tcp, FD_SCHEDULER_LISTEN);

    // Lo ponemos en modo escucha para nuevas conexiones
    if (listen(sock, 1) == -1)
        return -1;

    return sock;
}

/* Inicia el timer que dispara el reenvio periodico del anuncio y lo
agrega a la instancia epoll. Retorna el timerfd creado, o -1 en caso
de error. */
int init_announce_timer(void) {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd == -1) {
        return -1;
    }

    uint64_t id = fd_table_add(timer_fd, FD_SEND_ANNOUNCE_TIMER);
    if (id == UINT64_MAX) {
        return -1;
    }
    if (epoll_add(timer_fd, EPOLLIN | EPOLLET, id) == -1) {
        return -1;
    }
    if (timer_start(timer_fd, ANNOUNCE_SEC) == -1) {
        return -1;
    }

    return timer_fd;
}
