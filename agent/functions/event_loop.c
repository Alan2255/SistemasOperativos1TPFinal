#include <sys/epoll.h>
#include <unistd.h>
#include <pthread.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"
#include <stdio.h>

/* Event loop de la instancia epoll (para correr en cada thread)*/
void* event_loop(void*) {
    struct epoll_event events[MAX_EVENTS];
    int nfds, n;
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
            return NULL;
        for (n = 0; n < nfds; ++n) {
            FdInfo* info = (FdInfo*)(events[n].data.ptr);
            if (info == NULL) {
                // printf("[ERROR STACK] Se recibio un evento con data.ptr = NULL. Saltando evento para evitar SegFault.\n");
                continue; 
            }
            switch (info->type) {
                case FD_SCHEDULER:
                    if (events[n].events & (EPOLLHUP | EPOLLERR)) {
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, info->fd, NULL);
                        close(info->fd);
                        fd_info_destr(info);
                    }
                    else if (events[n].events & EPOLLOUT)
                        handle_tcp_epollout(info);
                    else if (events[n].events & EPOLLIN)
                        handle_scheduler(info);
                    break;
    
                case FD_UDP:
                    handle_announce(info);
                    break;
    
                case FD_AGENT:
                    if (events[n].events & (EPOLLHUP | EPOLLERR))
                        handle_agent_disconnect(info);
                    else if (events[n].events & EPOLLOUT)
                        handle_tcp_epollout(info);
                    else if (events[n].events & EPOLLIN)
                        handle_agent_msg(info);
                    break;
    
                case FD_NODE_TIMER:
                    handle_node_timer(info);
                    break;
    
                case FD_SEND_ANNOUNCE_TIMER:
                    handle_announce_timer(info);
                    break;
    
                case FD_AGENTS_LISTEN:
                    handle_agent_connect(info);
                    break;
    
                case FD_LISTEN_SCHEDULER:
                    handle_listen_scheduler(info);
                    break;
            }
        }
    }
}
