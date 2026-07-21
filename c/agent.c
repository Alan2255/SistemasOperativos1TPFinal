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
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "consts.h"
#include "structures/hash.h"
#include "structures/fd_table.h"
#include "structures/local_resources.h"
#include "structures/agent_table.h"
#include "structures/job_table.h"
#include "structures/reservation_table.h"
#include "functions/functions.h"

Hash *agent_table = NULL;
Hash *job_table = NULL;
Hash *reservation_table = NULL;
Resource* resources = NULL;
int resource_count = 0;

uint16_t puerto_tcp = -1;
int epollfd = -1;
int udp_sock = -1;
int scheduler_fd = -1;
uint64_t scheduler_id = UINT64_MAX;
int time_per_request = 0;

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
    if (epollfd == -1) {
        perror("epoll_create1");
        return -1;
    }

    /* Iniciamos las tablas */
    agent_table_init();
    job_table_init();
    reservation_table_init();
    fd_table_init();

    /* Iniciamos los sockets */
    int scheduler_listen_sock = init_scheduler_listen_sock();
    int agents_listen_sock = init_agents_listen_sock();
    udp_sock = init_udp_sock();
    
    if (udp_sock == -1 || scheduler_listen_sock == -1 
                       || agents_listen_sock == -1) {
        perror("init socks");
        return -1;
    }  

    /* Mandamos el anuncio y esperamos 2 segundos */ 
    send_announce();
    sleep(2);

    /* Seteamos un timer y lo agregamos a epoll para enviar el proximo anuncio */
    if (init_announce_timer() == -1)
        return -1;

    /* Iniciamos los threads */
    pthread_t threads[N_THREADS];
    for (int i = 0; i < N_THREADS; i++)
        pthread_create(&threads[i], NULL, event_loop, NULL);

    pthread_join(threads[0], NULL);
}


