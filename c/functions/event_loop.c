#include <sys/epoll.h>
#include <unistd.h>
#include <pthread.h>
#include "../consts.h"
#include "../structures/fd_table.h"
#include "functions.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

/* Event loop de la instancia epoll para correr en cada thread. */
void* event_loop(void*) {
    struct epoll_event events[MAX_EVENTS];
    int nfds, n;
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
            return NULL;
        for (n = 0; n < nfds; ++n) {
            uint64_t id = events[n].data.u64;
            
            FdEntry* info = fd_table_get_and_inc(id);
            printf("[event_loop] 0x%" PRIx64 ", info=%p\n", id, info);
            if (info == NULL) {
                continue;
            }
            switch (info->type) {
                case FD_SCHEDULER:
                    if (events[n].events & (EPOLLHUP | EPOLLERR)) {
                        close_scheduler_conn(info);
                    }
                    else if (events[n].events & EPOLLOUT)
                        handle_tcp_epollout(id, info);
                    else if (events[n].events & EPOLLIN)
                        handle_scheduler(id, info);
                    break;

                case FD_UDP:
                    handle_announce(info);
                    break;

                case FD_AGENT:
                    if (events[n].events & (EPOLLHUP | EPOLLERR))
                        close_agent_conn(id, info);
                    else if (events[n].events & EPOLLOUT)
                        handle_tcp_epollout(id, info);
                    else if (events[n].events & EPOLLIN)
                        handle_agent(id, info);
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

                case FD_SCHEDULER_LISTEN:
                    handle_listen_scheduler(info);
                    break;
            }

            // Liberamos la referencia que tomamos al principio de la iteracion
            fd_table_dec_and_release(info);
        }
    }
}
