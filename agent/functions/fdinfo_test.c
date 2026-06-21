#include <stdio.h>
#include <stdlib.h>

#include "fdinfo.h"

char* type_to_str(fdtype type)
{
    switch (type) {
        case FD_SCHEDULER: return "FD_SCHEDULER";
        case FD_UDP: return "FD_UDP";
        case FD_NODE: return "FD_NODE";
        case FD_NODE_TIMER: return "FD_NODE_TIMER";
        case FD_LISTEN_NODE: return "FD_LISTEN_NODE";
        case FD_LISTEN_SCHEDULER: return "FD_LISTEN_SCHEDULER";
    }
}

void test_fd(fdtype type) {
    printf("---- %s ----\n", type_to_str(type));

    FdInfo *info = fd_info_create(42, type);

    printf("fd   = %d\n", info->fd);
    printf("type = %s\n", type_to_str(info->type));
    printf("data = %p\n", info->data);

    fd_info_destr(info);
}

int main(void){
    test_fd(FD_SCHEDULER);
    test_fd(FD_UDP);
    test_fd(FD_NODE);
    test_fd(FD_NODE_TIMER);
    test_fd(FD_LISTEN_NODE);
    test_fd(FD_LISTEN_SCHEDULER);

    return 0;
}