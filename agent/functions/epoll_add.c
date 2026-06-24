#include <stdio.h>
#include <sys/epoll.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Si la estructura asociada al fd (parametro 'info') es NULL
agrega el fd a la instancia epoll, en caso contrario solo 
modifica los cambios asociados al fd. 
Retorna 'info' creado si no lo estaba y NULL en caso de error. */
FdInfo* epoll_add(int fd, fdtype type, int events, FdInfo* info) {
    struct epoll_event ev;

    // Copiamos los eventos
    ev.events = events;

    if (info == NULL) {
        // Creamos la estructura de datos segun el tipo de fd
        info = fd_info_create(fd, type);
        if (info == NULL)
            return NULL;
        ev.data.ptr = info;
    
        // Agregamos a epoll
        // Si no se puede anadir eliminamos la estructura 
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            fd_info_destr(info);
            return NULL;
        }
    }

    else {
        ev.data.ptr = info;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) == -1)
            return NULL;
    }

    return info;
}