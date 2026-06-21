#include <netinet/in.h>
#include "fdinfo.h"
#include "../structures/table_agent.h"
#include "../agent.h"

void handle_node_timer(FdInfo* info) { 
    int timerfd = info->fd;
    char* ip = ((fd_node_timer_data*)(info->data))->ip;
    
    // Eliminamos el nodo de la tabla
    agent_manager_delete(ip);
    
    // Eliminamos el timer de epoll y lo cerramos
    epoll_ctl(epollfd, EPOLL_CTL_DEL, timerfd, NULL);
    close(timerfd);
    fd_info_destr(info);
}