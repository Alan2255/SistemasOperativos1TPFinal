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

uint16_t puerto_tcp;
int epollfd;
int sockudp;
int scheduler_fd;
FdInfo *scheduler_info;

int main(int argc, char* argv[]) {

    /* Obtenemos el puerto y los recursos locales por linea de comandos */
    if (argc < 5) {
        printf("Uso: <port> <n> <name_1> ... <name_n> <amount_1> ... <amount_n>\n");
        return 1;
    }

    puerto_tcp = atoi(argv[1]);
    if (puerto_tcp < 7000 || puerto_tcp > 13000) 
        printf("Error: el puerto debe estar entre 7000 y 13000 (incluidos)\n");

    int num_resources = atoi(argv[2]);
    if (argc != 3 + (num_resources * 2)) {
        printf("Error: cantidad incorrecta de argumentos \n");
        printf("Uso: <port> <n> <name_1> ... <name_n> <amount_1> ... <amount_n>\n");
        return 1;
    }

    char** resource_names = malloc(sizeof(char*) * num_resources);
    if (resource_names == NULL)
        return 1;
    int* capacities = malloc(sizeof(int) * num_resources);
    if (capacities == NULL) {
        free(resource_names);
        return 1;
    }

    for (int i = 0; i < num_resources; i++) {
        resource_names[i] = argv[3 + i];
        capacities[i] = atoi(argv[3 + num_resources + i]);
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
    int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd == -1)
        return -1;

    FdInfo* timerfd_info = epoll_add(timerfd, FD_SEND_ANNOUNCE_TIMER, 
                                    EPOLLIN | EPOLLET | EPOLLONESHOT);
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


