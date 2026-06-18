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

#define PUERTO_TCP 8100
#define PUERTO_UDP 12529
#define TAM_BUFF 256
#define MAX_EVENTS 10
#define MAX_CLIENTS_QUEUED 10
#define MAX_LEN_REQUEST 4


void quit(char* s) {
    perror(s);
    exit(EXIT_FAILURE);
}

/**
 * Crea un socket de red, del tipo y puerto dado.
 * El parametro 'addrtype' puede ser "local", en tal caso la 
 * direccion es 127.0.0.1; o "remote", para tomar la direccion
 * correspondiente en la red.
*/
int create_sock_and_bind(char* addrtype, int type, int port) {
	struct sockaddr_in sa; 
	int sock;
	int yes = 1;

	sock = socket(AF_INET, type, 0);
	if (sock < 0)
		quit("socket");

	/* Seteamos 'SO_REUSEADDR' para bindear a la direccion si recientemente se utilizo. */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == 1)
		quit("setsockopt");

    /* Bindeamos el socket a la direccion segun 'addrtype' y al puerto 'port'. */
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (strcmp(addrtype, "local")) 
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else if (strcmp(addrtype, "remote"))
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    else 
        quit("create_sock_and_bind");

	if (bind(sock, (struct sockaddr *)&sa, sizeof sa) < 0)
		quit("bind");

	return sock;
}

void epoll_add(int epollfd, int fd, int events, void * data) {
    struct epoll_event ev;
    ev.events = events;
    if (data == NULL)
        ev.data.fd = fd;
    else 
        ev.data.ptr = data;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1)
        quit("epoll_ctl");
}

struct data {
    int fd;
    char buf[TAM_BUFF];
    int len_buf;
}

/* 
 * Acepta una nueva conexion entrante en 'listen_sock', la 
 * agrega a la instancia epoll 'epollfd' y retorna el socket
 * de conexion.
 */
int accept_and_add_to_epoll_with_data(int listen_sock, int epollfd) {
    int conn_sock = accept4(listen_sock, NULL, NULL, SOCK_NONBLOCK);
    if (conn_sock == -1)
    quit("accept4");

    struct data *data = malloc(sizeof(struct data));
    data->fd = conn_sock;
    data->len_buf = 0;
    epoll_add(epollfd, conn_sock, EPOLLIN | EPOLLET, data);
    
    return conn_sock;
}

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

void handle_agent_event(struct epoll_event *ev, int epollfd) {
    int events = ev->events;
    int socket = ((struct data*)ev->data->ptr)->fd;
    char *buf = ((struct data*)ev->data->ptr)->buf;
    int len_buf = ((struct data*)ev->data->ptr)->len_buf;

    if (events & EPOLLHUP) { 
        free(buf);
        epoll_ctl(epollfd, EPOLL_CTL_DEL, socket, NULL);
        close(socket);
        return;
    }

    int read = read(socket, buf+len_buf, (TAM_BUFF-len_buf)-1);
    buf[len_buf+read] = '\0'

    char *request[MAX_LEN_REQUEST];
    int len_request;
    
    for (char *str1 = buf, *end_of_command; ; str1 = end_of_command+1) {
        end_of_command = strchr(str1, '\n');
        /* Si no esta el comando completo (terminado en '\n') lo guardamos en el buffer. */
        if (end_of_command == NULL) {
            strcpy(buf, str1);
            printf("No hay mas comandos. buf = %s.\n", buf);
            break;
        }
        
        /* Si esta, lo parseamos y realizamos la accion correspondiente. */ 
        *end_of_command = '\0';
        char* str2 = str1;
        char *delim = " ";
        char* token = strtok(str2, delim); 
        request[0] = token;
        if (strncmp(token, "RESERVE", strlen("RESERVE")) == 0) {
            len_request = 4; // RESERVE -> 1, <job_id> -> 1, <resource> -> 1, <amount> -> 1
            int i = 1;
            for (; i < len_request; i++) {
                token = strtok(NULL, delim);
                if (token == NULL) 
                    break;
                else 
                    request[i] = token;
            }
            if (i == len_request) {// agregar '&& tabla_recursos_reservar(arg[2], arg[3])' que descuenta o no de la tabla de recursos (0 -> responder DENIED, 1 -> responder GRANTED)
                // anadir_tabla(arg[1], ...)
                printf("comando parseado: %s %s %s %s\n", request[0], request[1], request[2], request[3]);
            }
            else {
                // write(scheduler_sock, "DENIED")
            } 
        }
        else if (strncmp(token, "GRANTED", strlen("GRANTED")) == 0) {
            len_request = 2; // GRANTED -> 1, <job_id> -> 1
            int i = 1;
            for (; i < len_request; i++) {
                token = strtok(NULL, delim);
                if (token == NULL) 
                    break;
                else 
                    request[i] = token;
            }
            if (i == len_request) {
                // write(scheduler_sock, "GRANTED")
                printf("comando parseado: %s %s %s %s\n", request[0], request[1], request[2], request[3]);
            }
        }
        else {
            // anadir los demas comandos 
            printf("invalid command\n");
        }
    }
}

int main() {
    int listen_sock_schedulers = create_sock_and_bind("local", SOCK_STREAM, PUERTO_TCP);
    int listen_sock_agents = create_sock_and_bind("remote", SOCK_STREAM, PUERTO_TCP);
    int sockudp = create_sock_and_bind("remote", SOCK_DGRAM, PUERTO_UDP);
    int scheduler_sock; // socket de conexion con el job scheduler de Erlang

	if (listen(listen_sock_schedulers, MAX_CLIENTS_QUEUED) < 0)
		quit("listen");
	if (listen(listen_sock_agents, MAX_CLIENTS_QUEUED) < 0)
		quit("listen");

    /* Creamos la instancia epoll. */
    struct epoll_event events[MAX_EVENTS];
    int nfds;
    int epollfd = epoll_create1(0);
    if (epollfd == -1)
        quit("epoll_create1");

    /* Agregamos los sockets de escucha de nuevas conexiones y el socket UDP a la instacia epoll. */
    epoll_add(epollfd, listen_sock_schedulers, EPOLLIN, NULL);
    epoll_add(epollfd, listen_sock_agents, EPOLLIN, NULL);
    epoll_add(epollfd, sockudp, EPOLLIN, NULL);
    
    /* Envia el anuncio. */
    char buf[TAM_BUFF];
    char* ip = "192.168.0.1"; // como se mi ip en la red??????
    sprintf(buf, "ANNOUNCE %s %d cpu:%d mem:%d", ip, PUERTO_UDP, get_cpu_num(), get_mem_mb());
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PUERTO_UDP);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(sockudp, buf, strlen(buf), 0, (struct sockaddr*)&dest, sizeof(dest));
    
    /* Espera 2 segundos para recibir anuncios de otros nodos. */
    sleep(2);

    /* Iniciamos la instancia epoll. */ 
    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == sockudp) {
                /* handle_ announce: 
                    char buf[TAM_BUFF] (puede estar dentro de la funcion ya que no se necesita 'bufferear', el msg llega completo)
                    read(sockudp, buf, TAM_BUF),
                    parsear anuncio, strtok(buf, " "),
                    actualizar (o agregar) entrada en tabla
                */                
            }
            else if (events[n].data.fd == listen_sock_agents) 
                accept_and_add_to_epoll_with_data(listen_sock_agents, epollfd);
            else if (events[n].data.fd == listen_sock_schedulers)
                scheduler_sock = accept_and_add_to_epoll_with_data(
                                    listen_sock_schedulers, epollfd);
            else if (events[n].data.fd == scheduler_sock) {
                /*
                handle_scheduler_request(&events[n]):
                    read(buf_scheduler) // JOB_REQUEST <job_id> [ @host:res:amount ... ]
                    obtener la longitud, intentar leer eso y si estan bien parsear, si no guardar en el buf y esperar el proximo ev
                    connect(sock, ip:PUERTO_TCP), // struct {int job_id, sock }
                    write(sock, "RESERVE <job_id> <res>:<amount>")
                    epoll_add(sock, EPOLLOUT);
                */
            }
            else { /* Evento en algun agente */
                handle_agent_event(&events[n], epollfd);
            }
        }
    }
}


