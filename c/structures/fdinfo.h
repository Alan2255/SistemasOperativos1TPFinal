/* ------ Estructura para manejar los eventos de epoll ------ */
#ifndef FDINFO_H 
#define FDINFO_H

#include "../consts.h"
#include <pthread.h>
#include <arpa/inet.h>

typedef enum {
    FD_SCHEDULER,
    FD_UDP,
    FD_AGENT,
    FD_NODE_TIMER,
    FD_SEND_ANNOUNCE_TIMER,
    FD_AGENTS_LISTEN,
    FD_SCHEDULER_LISTEN,
} fdtype;

typedef struct {
    int fd;
    fdtype type;
    void *data;
} FdInfo;

typedef struct {
    char buf_in[TAM_BUF];
    int len_buf_in;
    char buf_out[TAM_BUF];
    int len_buf_out;
    pthread_mutex_t mutex_in;
    pthread_mutex_t mutex_out;
} fd_tcp_data;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char port[PORTSTRLEN];
} fd_node_timer_data;


FdInfo *fd_info_create(int fd, fdtype type);

void fd_info_destr(FdInfo* info);

#endif /* FDINFO_H */