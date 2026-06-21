#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include "functions/tokenize_str.h"
#include "functions/parse_announce.h"
#include "agent.h"

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

/* Retorna la cantidad de CPUs. */
int get_cpu_num() {
    return sysconf(_SC_NPROCESSORS_CONF);
}

/* Retorna la cantidad de memoria en MB. */
int get_mem_mb() {
    struct sysinfo info;
    sysinfo(&info);
    return (info.totalram * info.mem_unit) / (1024 * 1024);
}

/* Retorna la estructura asociada al fd en epoll, o NULL en 
caso de error. */
FdInfo* epoll_add(int fd, fdtype type, int events) {
    struct epoll_event ev;

    /* Copiamos los eventos */
    ev.events = events;

    /* Creamos la estructura de datos segun el tipo de fd */
    FdInfo *info = fd_info_create(fd, type);
    if (info == NULL)
        return NULL;
    ev.data.ptr = info;

    /* Agregamos a epoll */
    // Si no se puede anadir eliminamos la estructura 
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        fd_info_destr(info);
        return NULL;
    }

    return info;
}

void handle_node_event(int fd, FD_TCP_Data *data) { // solo maneja event = EPOLLIN
    char *buf = data->buf;
    int len_buf = data->len_buf;

    /* Leemos lo que llego al socket. */
    int read = read(fd, buf+len_buf, (TAM_BUF-len_buf)-1);
    buf[len_buf+read] = '\0';

    char *request[MAX_LEN_REQUEST];
    int len_request;
    
    for (char *str1 = buf, *end_of_command; ; str1 = end_of_command+1) {
        end_of_command = strchr(str1, '\n');
        /* Si no esta el comando completo (terminado en '\n') lo guardamos en el buffer. */
        if (end_of_command == NULL) {
            strcpy(buf, str1);
            data->len_buf = strlen(str1);
            break;
        }
        
        /* Si esta, lo parseamos y realizamos la accion correspondiente. */ 
        *end_of_command = '\0'; // ahora str1 tiene un comando valido
        char command_name[MAX_STRLEN_COMMAND_NAME_NODE];
        int job_id;
        char res[MAX_STRLEN_RES];
        int amount;
        if (parse_node_command(str1, command_name, &job_id, res, &amount) == -1)
            return;


        if (strcmp(command_name, "RESERVE") == 0) {
            /* Intentamos reservar. */
            switch (reserve_local_res(res, amount)) { // reserve_local_resource(char* resource, int amount) = -1 -> resource/amount invalido, 0 -> cant disponible de 'res' insuficiente, 1 -> reservado
                case -1: // resource/amount invalido
                    write(fd, "DENIED\n", strlen("DENIED\n"))
                    break;

                case 0: // cantidad no disponible
                    enqueue_local_res(res, amount, fd); // enqueue_local_res(char *res, int amount)
                    break;

                case 1: // concedido
                    tabla_jobs_add(job_id, res, amount, fd) // tabla_jobs_add(int job_id, char* res, int amount, int socket);
                    break;
            }
        }
        else if (strcmp(command_name, "GRANTED") == 0) {
            /* responder al scheduler */
        }
        else if (strcmp(command_name, "RELEASE") == 0) {
            if (table_reserves_contains(fd, job_id, res)) {
                table_reserves_dec(fd, job_id, res, amount); // decrementa el monto de 'res' en la tabla, si no hay otros recursos reservados con este job_id, elimina la entrada del job de la tabla 
                local_res_dec(res, amount);
            } 
            else {
                dequeue_res(res, fd, job_id);
            }    
        }
        else if (strcmp(command_name, "DENIED") == 0) {
            /* responder al scheduler */
        }
        else {
            // algo
        }
    }
}

int init_sock_udp() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_UDP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_UDP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}

int init_listen_sock_scheduler() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_STREAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a localhost y puerto PUERTO_TCP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Lo ponemos en modo escucha */
    if (listen(socket, 1) == -1)
        return -1;

    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}

int init_listen_sock_nodes() {
    /* Creamos el socket */
    int socket = socket(AF_INET, SOCK_STREAM, 0);
    if (socket == -1) 
        return -1;

    /* Seteamos opciones de manipulacion necesarias para el 
    socket */
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, 
                    sizeof(yes)) == -1)
        return -1;

    /* Bind a la direccion en la red y puerto PUERTO_TCP */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PUERTO_TCP);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, (struct sockaddr *)&addr, 
                sizeof(addr)) == -1);
        return -1;
    
    /* Lo ponemos en modo escucha para nuevas conexiones*/
    listen(socket, MAX_PENDING_CONNECTIONS);

    /* Agregamos a la instancia epoll */
    if (epoll_add(socket, FD_UDP, EPOLLIN) == -1);
        return -1;
    
    return 0;
}

/* Envia el anuncio. */
int send_announce(sockudp, ) {
    char buf[TAM_BUF];
    sprintf(buf, "ANNOUNCE %d cpu:%d mem:%d", PUERTO_UDP, 
            get_cpu_num(), get_mem_mb()); // modificar

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PUERTO_UDP);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(sockudp, buf, strlen(buf), 0, (struct sockaddr*)&dest, sizeof(dest));
}

/* Inicia el timer (ya creado) con una cantidad en segundos */
int timerfd_start(int timerfd, int sec) {
    struct itimerspec ts;

    ts.it_value.tv_sec = sec;
    ts.it_value.tv_nsec = 0;

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(timerfd, 0, &ts, NULL) == -1)
        return -1;

    return 0;
}

int epollfd;
int scheduler_sock;     // Socket de conexion con el scheduler
int sockudp;            // Socket para envio/recibo de anuncios

int main() {
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
    // COMPLETAR

    /* Mandamos el anuncio y esperamos 2 segundos */ 
    send_announce(sockudp);
    sleep(2);

    /* Seteamos un timer y lo agregamos a epoll para enviar el
    proximo */
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd == -1)
        return -1;

    FdInfo* timerfd_info = epoll_add(timerfd, FD_ANNOUNCE_TIMER,
                                        EPOLLIN);
    if (timerfd_info == NULL)
        return -1;
    if (timerfd_start(timerfd, NODE_TIMEOUT_SEC) == -1)
        return -1;

    /* Iniciamos el event loop . */ 
    struct epoll_event events[MAX_EVENTS];
    int nfds;
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) 
            return -1;
        for (n = 0; n < nfds; ++n) {
            FdInfo* info = (FdInfo*)(events[n].data.ptr);
            switch (info->type) {
                case FD_SCHEDULER:
                    if (events[i].events & (EPOLLHUP | EPOLLERR)) { // si el scheduler corto la conexion:
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, info->fd, NULL);
                        close(info->fd);
                        fd_info_destr(info);
                    }
                    else 
                        handle_scheduler(info);
                    break;
                case FD_UDP:
                    handle_announce(info);
                    break;
                case FD_NODE:
                    if (events[i].events & (EPOLLHUP | EPOLLERR)) // si un agente cerro su conexion:
                        handle_peer_disconnect(info);
                    else
                        handle_node(info);
                    break;
                case FD_NODE_TIMER:
                    handle_node_timer(info);
                    break;
                case FD_ANNOUNCE_TIMER:
                    handle_announce_timer(info);
                    break;
                case FD_LISTEN_NODE:
                    struct_size = sizeof(fd_listen_node_data);
                    break;
                case FD_LISTEN_SCHEDULER:
                    struct_size = sizeof(fd_listen_scheduler_data);
                    break;
            }
        }
    }
}


