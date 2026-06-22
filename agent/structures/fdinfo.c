#include <stdlib.h>
#include "fdinfo.h"
#include "../agent.h"

FdInfo *fd_info_create(int fd, fdtype type) {
    FdInfo *ret = malloc(sizeof(FdInfo));
    if (ret == NULL)
        return NULL;

    ret->fd = fd;

    ret->type = type;

    if (type == FD_SCHEDULER || type == FD_AGENT)
        ret->data = malloc(sizeof(fd_tcp_data));
    else if (type == FD_NODE_TIMER)
        ret->data = malloc(sizeof(fd_node_timer_data));
    else 
        ret->data = NULL;

    return ret;
}

void fd_info_destr(FdInfo* info) {
    if (info == NULL)
        return;

    if (info->data != NULL)
        free(info->data);

    free(info);
}
