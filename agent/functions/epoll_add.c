#include <stdio.h>
#include <sys/epoll.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Retorna la estructura asociada al fd en epoll, o NULL en 
caso de error. */
FdInfo* epoll_add(int fd, fdtype type, int events) {
    struct epoll_event ev;

    /* Copiamos los eventos */
    ev.events = events;

    /* Creamos la estructura de datos segun el tipo de fd */
    FdInfo *info = fd_info_create(fd, type);
    if (info == NULL)
        return NULL;
    ev.data.ptr = info;


    /* Agregamos a epoll */
    // Si no se puede anadir eliminamos la estructura 
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        fd_info_destr(info);
        // printf("Se elimina la estructura\n");
        return NULL;
    }
    // printf("Hasta aca epoll\n");

    return info;
}