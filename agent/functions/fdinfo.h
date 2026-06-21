/* ------ Estructura para manejar la instancia epoll ------ */
#ifndef FDINFO_H 
#define FDINFO_H

#include <arpa/inet.h>
#include "../agent.h"

typedef enum {
    FD_SCHEDULER,
    FD_UDP,
    FD_NODE,
    FD_NODE_TIMER,
    FD_ANNOUNCE_TIMER,
    FD_LISTEN_NODE,
    FD_LISTEN_SCHEDULER,
} fdtype;

typedef struct {
    int fd;
    fdtype type;
    void *data;
} FdInfo;

typedef struct {
    char buf[TAM_BUF];
    int len_buf; 
} fd_scheduler_data;

typedef struct {
    char buf[TAM_BUF];
    int len_buf; 
} fd_node_data;

typedef struct {
    char ip[INET_ADDRSTRLEN]; 
} fd_node_timer_data;

typedef struct {
} fd_announce_timer_data;

typedef struct {
} fd_udp_data;

typedef struct {
} fd_listen_node_data;

typedef struct {
} fd_listen_scheduler_data;

FdInfo *fd_info_create(int fd, fdtype type);

void fd_info_destr(FdInfo* info);

#endif /* FDINFO_H */