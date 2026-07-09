#include <stdio.h>
#include <sys/epoll.h>
#include "../consts.h"
#include "functions.h"

/* Registra en la instancia epoll un fd con el identificador y eventos
pasados. Retorna -1 en caso de error. */
int epoll_add(int fd, int events, uint64_t id) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.u64 = id;

    return epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}

/* Modifica los eventos de un fd ya registrado en epoll.
Retorna -1 en caso de error. */
int epoll_mod(int fd, int events, uint64_t id) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.u64 = id;

    return epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}
