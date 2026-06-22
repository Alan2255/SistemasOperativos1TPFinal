#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "agent.h"
#include "structures/fdinfo.h"
#include "structures/hash.h"
#include "structures/local_resources.h"
#include "structures/table_agent.h"
#include "structures/table_job.h"
#include "structures/table_reservation.h"
#include "functions/functions.h"


int epollfd;
int sockudp;
int scheduler_fd;
FdInfo *scheduler_info;

int main(int argc, char* argv[]) {
    /* Parseamos los recursos locales y las cantidades por 
    linea de comandos */
    if (argc <= 1) {
        printf("Uso: <int_n> <name_1> ... <name_n> <amount_1> ... <amount_n>\n");
        return 1;
    }
    int num_resources = atoi(argv[1]);

    if (argc != 2 + (num_resources * 2)) {
        printf("Error: cantidad incorrecta de argumentos \n");
        printf("Uso: <int_n> <name_1> ... <name_n> <amount_1> ... <amount_n> \n");
        return 1;
    }

    char** resource_names = malloc(sizeof(char*) * 
                                            num_resources);
    int* capacities = malloc(sizeof(int) * num_resources);

    if (resource_names == NULL || capacities == NULL) {
        free(resource_names);
        free(capacities);
        return 1;
    }

    for (int i = 0; i < num_resources; i++) {
        resource_names[i] = argv[2 + i];
        capacities[i] = atoi(argv[2 + num_resources + i]);
    }

    local_resources_init(num_resources, resource_names, capacities);

    /* Iniciamos la instancia epoll */
    epollfd = epoll_create1(0);
    if (epollfd == -1)
        return -1;

    /* Iniciamos los sockets */
    sockudp = init_sock_udp();
    int listen_sock_schedulers = init_listen_sock_scheduler();
    int listen_sock_nodes = init_listen_sock_nodes();

    if (sockudp == -1 || listen_sock_schedulers == -1 
                      || listen_sock_nodes == -1)
        return -1;

    /* Iniciamos las tablas */
    agent_manager_init();
    job_manager_init();
    reservation_manager_init();

    /* Mandamos el anuncio y esperamos 2 segundos */ 
    send_announce();
    sleep(2);

    /* Seteamos un timer y lo agregamos a epoll para enviar el
    proximo */
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd == -1)
        return -1;

    FdInfo* timerfd_info = epoll_add(timerfd, 
                                FD_SEND_ANNOUNCE_TIMER, EPOLLIN);
    if (timerfd_info == NULL)
        return -1;
    if (timerfd_start(timerfd, ANNOUNCE_SEC) == -1)
        return -1;

    /* Iniciamos el event loop . */
    struct epoll_event events[MAX_EVENTS];
    int nfds, n;
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
            return -1;
        for (n = 0; n < nfds; ++n) {
            FdInfo* info = (FdInfo*)(events[n].data.ptr);
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
                    if (events[n].events & (EPOLLHUP | EPOLLERR)) // si un agente cerro su conexion:
                        handle_agent_disconnect(info); //-----------
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

                case FD_LISTEN_NODE:
                    handle_agent_connect(info);
                    break;

                case FD_LISTEN_SCHEDULER:
                    handle_listen_scheduler(info);
            }
        }
    }
}


