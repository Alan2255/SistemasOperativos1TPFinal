#define _GNU_SOURCE    
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
#include "consts.h"
#include "structures/fdinfo.h"
#include "structures/hash.h"
#include "structures/local_resources.h"
#include "structures/table_agent.h"
#include "structures/table_job.h"
#include "structures/table_reservation.h"
#include "functions/functions.h"

Hash *table_agent = NULL;
Hash *table_job = NULL;
Hash *table_reservation = NULL;
Resource* resources = NULL;
int resource_count = 0;

uint16_t puerto_tcp = -1;
int epollfd = -1;
int udp_sock = -1;
int scheduler_fd = -1;
FdInfo *scheduler_info;

int main(int argc, char* argv[]) {
    /* Obtenemos y seteamos: el puerto y los recursos locales*/
    int num_resources = MAX_RESOURCES_AGENT;
    char* resource_names[MAX_RESOURCES_AGENT];
    int capacities[MAX_RESOURCES_AGENT];
    if (get_port_and_resources(argc, argv, &num_resources, resource_names, capacities) == -1)
        return -1;
    local_resources_init(num_resources, resource_names, capacities);

    /* Iniciamos la instancia epoll */
    epollfd = epoll_create1(0);
    if (epollfd == -1)
        return -1;

    /* Iniciamos los sockets */
    int scheduler_listen_sock = init_scheduler_listen_sock();
    int agents_listen_sock = init_agents_listen_sock();
    udp_sock = init_udp_sock();
    
    if (udp_sock == -1 || scheduler_listen_sock == -1 
                       || agents_listen_sock == -1)
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
    int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd == -1)
        return -1;

    FdInfo* timerfd_info = epoll_add(timerfd, FD_SEND_ANNOUNCE_TIMER, EPOLLIN | EPOLLET, NULL);
    if (timerfd_info == NULL)
        return -1;
    if (timerfd_start(timerfd, ANNOUNCE_SEC) == -1)
        return -1;


    /* Iniciamos los threads */
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; i++)
        pthread_create(&threads[i], NULL, event_loop, NULL);

    pthread_join(threads[0], NULL);
}


