#include <sys/timerfd.h>
#include <netinet/in.h>
#include "parse_announce.h"
#include "fdinfo.h"
#include "../structures/table_agent.h"
#include "../agent.h"

void handle_announce(int fd) {
    char buf[TAM_BUF];
    int len_buf;
    
    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in src;
    socklen_t sa_len = sizeof(src);
    
    /* Leemos el mensaje y obtenemos la IP. */
    len_buf = recvfrom(fd, buf, TAM_BUF, 0, (struct sockaddr *)&src, &sa_len);
    if (len_buf < 0)
        return;
    inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));

    /* Parseamos el mensaje*/
    char port[PORTSTRLEN];
    Resource resources[MAX_RESOURCES_NODE];
    int res_count;
    if (parse_announce(buf, port, resources, &res_count) == -1)
        return;

    /* Agregamos o actualizamos el nodo en la tabla */
    int timerfd = agent_manager_get_timerfd(ip);
    if (timerfd < 0) { // Si el nodo no se encuentra en la tabla
        // Creamos el timer
        timerfd = timerfd_create(CLOCK_MONOTONIC, 0);

        // Agregamos la entrada a la tabla
        agent_manager_add(ip, port, res_count, resources, timerfd);
        
        // Agregamos el timer a la instancia epoll 
        FdInfo *info = epoll_add(timerfd, FD_NODE_TIMER, EPOLLIN);
        strncpy(((fd_node_timer_data *)(info->data))->ip, ip,
                INET_ADDRSTRLEN);
    }
    else {
        agent_manager_update(ip, resources);
    }

    /* Iniciamos/reiniciamos el timer */
    timerfd_start(timerfd, NODE_TIMEOUT_SEC);
}
