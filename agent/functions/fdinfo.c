#include <stdlib.h>
#include "fdinfo.h"
#include "../agent.h"

FdInfo *fd_info_create(int fd, fdtype type) {
    FdInfo *ret = malloc(sizeof(FdInfo));
    if (ret == NULL)
        return NULL;

    ret->fd = fd;

    ret->type = type;

    int struct_size;
    switch (type) {
        case FD_SCHEDULER:
            struct_size = sizeof(fd_scheduler_data);
            break;

        case FD_UDP:
            struct_size = sizeof(fd_udp_data);
            break;

        case FD_NODE:
            struct_size = sizeof(fd_node_data);
            break;

        case FD_NODE_TIMER:
            struct_size = sizeof(fd_node_timer_data);
            break;
        case FD_ANNOUNCE_TIMER:
            struct_size = sizeof(fd_announce_timer_data);
            break;
        case FD_LISTEN_NODE:
            struct_size = sizeof(fd_listen_node_data);
            break;

        case FD_LISTEN_SCHEDULER:
            struct_size = sizeof(fd_listen_scheduler_data);
            break;

        default:
            struct_size = 0;
            break;
    }
    if (struct_size == 0)
        ret->data = NULL;
    else 
        ret->data = malloc(struct_size);

    return ret;
}

void fd_info_destr(FdInfo* info) {
    if (info == NULL)
        return;
    if (info->data != NULL)
        free(info->data);
    free(info);
}
