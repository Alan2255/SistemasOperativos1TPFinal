#define _GNU_SOURCE    
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include "../structures/fdinfo.h"
#include "../structures/table_agent.h"
#include "../structures/table_job.h"
#include "../structures/table_reservation.h"
#include "../consts.h"
#include "functions.h"
#include <stdbool.h>

/* Maneja el evento EPOLLOUT de un socket tcp */
int handle_tcp_epollout(FdInfo* info) {
    printf("handle_tcp_epollout.\n");
    if (info == NULL)
        return -1;

    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_out);

    int n = send(info->fd, data->buf_out, data->len_buf_out, MSG_NOSIGNAL);
    if (n == -1) {
        if (errno == EPIPE) {

        }
        else {
            return -1;
        } 

    }

    // Actualizamos el buffer
    memmove(data->buf_out, data->buf_out + n, data->len_buf_out - n);
    data->len_buf_out -= n;

    if (data->len_buf_out == 0) { // Si se mando todo sacamos EPOLLOUT de los eventos
        epoll_add(info->fd, info->type, EPOLLIN | EPOLLET, info);
    }

    pthread_mutex_unlock(&data->mutex_out);
    
    return 0;
}


/* Maneja el evento en el timer para lanzar el anuncio */
void handle_announce_timer(FdInfo* info) {
    /* Vaciamos el fd */
    uint64_t nexpirations;
    read(info->fd, &nexpirations, sizeof(nexpirations));

    /* Mandamos el anuncio */
    send_announce();

    /* Iniciamos el time */
    timerfd_start(info->fd, ANNOUNCE_SEC);
}

/* Maneja el intento de conexion del scheduler */
void handle_listen_scheduler(FdInfo* info) {
    scheduler_fd = accept4(info->fd, NULL, NULL, SOCK_NONBLOCK);
    if (scheduler_fd == -1)
        return;

    scheduler_info = epoll_add(scheduler_fd, FD_SCHEDULER, 
                                EPOLLIN | EPOLLHUP | EPOLLERR |
                                EPOLLET | EPOLLONESHOT, NULL);
    if(scheduler_info == NULL) {
        close(scheduler_fd);
        return;
    }

    ((fd_tcp_data*)(scheduler_info->data))->len_buf_in = 0;
    ((fd_tcp_data*)(scheduler_info->data))->len_buf_out = 0;
}

/* Maneja un el intento de conexion de un agente */
void handle_agent_connect(FdInfo* info) {
    // Aceptamos todos los clientes que llegaron
    while (1) {
        int agentfd = accept4(info->fd, NULL, NULL, SOCK_NONBLOCK);

        if (agentfd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
    
        if (agentfd == -1)
            return;

        FdInfo* agent_info = epoll_add(agentfd, FD_AGENT, EPOLLIN | EPOLLET , NULL);
        if (agent_info == NULL) {
            close(agentfd);
            return;
        }
    
        ((fd_tcp_data*)(agent_info->data))->len_buf_in = 0;
        ((fd_tcp_data*)(agent_info->data))->len_buf_out = 0;
    }
}

int send_msg(FdInfo* info, char* msg, int len) {
    fd_tcp_data* data = info->data;

    pthread_mutex_lock(&data->mutex_out);
    if (data->len_buf_out > 0) {
        add_to_buffer(info, msg, len);
    }
    else {
        int n = send(info->fd, msg, len, MSG_NOSIGNAL);
        if (n == -1) {
            if (errno == EPIPE) {
                ;
            }
            else {
                pthread_mutex_unlock(&data->mutex_out);
                return -1;
            }
        }
        else if (n < len) {
            add_to_buffer(info, msg + n, len - n);
            epoll_add(info->fd, info->type, 
                        EPOLLOUT | EPOLLIN | EPOLLET, info);
        }
    }
    pthread_mutex_unlock(&data->mutex_out);

    return 0;
}

/* Maneja la recepcion de un mensaje de un agente */
void handle_agent_msg(FdInfo* info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_in);
    // Leemos el socket hasta EAGAIN y procesamos los mensajes leidos
    while (1) {
        int n = read(info->fd, data->buf_in + data->len_buf_in,
                        TAM_BUF - data->len_buf_in);    
        
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // Si no hay mas datos salimos del loop
                printf("handle_agent_msg: No hay mas datos en el socket.\n");
                break;
            }
            else { // Ocurrio otro error
                perror("handle_agent_msg");
                pthread_mutex_unlock(&data->mutex_in);
                return;
            }
        }
        else if (n == 0) { // El agente cerro la conexion
            reservation_manager_release_by_socket(info->fd);
            close(info->fd);
            fd_info_destr(info);
            printf("handle_agent_msg: Agent closed connection.\n");
            pthread_mutex_unlock(&data->mutex_in);
            return;
        }

        // Actualizamos la longitud del buffer
        data->len_buf_in += n;

        while (1) {
            /* Comprobamos si esta el comando completo (terminado en '\n'). */
            char* end_of_command = memchr(data->buf_in, '\n', data->len_buf_in);
            if (end_of_command == NULL) 
                break;
            
            /* Si esta, lo parseamos y realizamos la accion correspondiente. */ 
            char command_name[MAX_LEN_COMMAND_AGENT];
            int job_id;
            char res[MAX_BYTES_NAME_RESOURCE];
            int amount;
            *end_of_command = '\0';

            if (parse_node_command(data->buf_in, command_name, &job_id, res, &amount) == -1) {
                printf("handle_agent_msg (invalid request).\n");
                break;
            }
            
            // Gestionamos un comando completo
            char reply[TAM_BUF];
            int len;
            if (strcmp(command_name, "RESERVE") == 0) {
                /* Intentamos reservar. */
                switch (local_resources_reserve(job_id, info->fd, res, amount)) {
                    case -1: // Resource/amount invalido
                        // Le respondemos DENIED <job_id>
                        len = sprintf(reply, "DENIED %d\n", job_id);
                        send_msg(info, reply, len);
                        break;
    
                    case 1: // Cantidad no disponible (lo encola)
                        reservation_manager_add(job_id, info->fd, res, amount, 0);
                        break;
    
                    case 0: // Concedido
                        // Agregamos a la tabla de reservas locales
                        reservation_manager_add(job_id, info->fd, res, amount, 1);
    
                        // Le respondemos GRANTED <job_id>
                        len = sprintf(reply, "GRANTED %d\n", job_id);
                        send_msg(info, reply, len);
                        break;
                }
            }
            else if (strcmp(command_name, "GRANTED") == 0) {
                char ip[INET_ADDRSTRLEN];
                char port[PORTSTRLEN];
                agent_manager_get_addr_by_fd(info->fd, ip, port);
                job_set_granted(job_id, ip, port, 1);

                // Si todos los pedidos fueron concedidos le
                // avisamos al scheduler
                if (job_check_granted(job_id)) { 
                    len = sprintf(reply, "JOB_GRANTED %d", job_id);
                    unsigned short nlen = htons(len);

                    send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                    send_msg(scheduler_info, reply, len);
                }
            }
            else if (strcmp(command_name, "RELEASE") == 0) {
                local_resources_release(job_id, info->fd, res, amount);
                reservation_manager_release(job_id);
            }
            else if (strcmp(command_name, "DENIED") == 0) {
                // Le avisamos al scheduler
                len = sprintf(reply, "JOB_DENIED %d", job_id);
                unsigned short nlen = htons(len);
                
                send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                send_msg(scheduler_info, reply, len);

                job_release(job_id);
            }
            else {
                printf("handle_agent_msg: invalid command in request.\n");
            }

            /* Actualizamos el buffer */
            int len_command = end_of_command - data->buf_in;
            memmove(data->buf_in, end_of_command+1, data->len_buf_in - (len_command + 1));
            data->len_buf_in -= len_command + 1;
        }
    }
    pthread_mutex_unlock(&data->mutex_in);

}

/* Maneja la desconexion inesperada de un agente */
void handle_agent_disconnect(FdInfo* info) {
    printf("handle_agent_disconnect (agent closed connection).\n");
    reservation_manager_release_by_socket(info->fd);
    close(info->fd); 
    fd_info_destr(info);
}

/* Maneja el evento en el timer para considerar a un 
nodo como caido */
void handle_node_timer(FdInfo* info) { 
    int timerfd = info->fd;
    char* ip_port = ((fd_node_timer_data*)(info->data))->ip_port;
    char ip[INET_ADDRSTRLEN];
    strncpy(ip, ip_port, INET_ADDRSTRLEN);
    // Eliminamos el nodo de la tabla
    agent_manager_delete(ip, ip_port+INET_ADDRSTRLEN);
    
    // Eliminamos el timer de epoll y lo cerramos
    epoll_ctl(epollfd, EPOLL_CTL_DEL, timerfd, NULL);
    close(timerfd);
    fd_info_destr(info);
}

/* Maneja la recepcion de un anuncion es el socket udp */
void handle_announce(FdInfo* info) {

    int fd = info->fd;
    char buf[TAM_BUF];
    int len_buf;
    
    printf("Evento de agente.\n");

    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in src;
    socklen_t sa_len = sizeof(src);
    
    while (1) {
        /* Leemos el mensaje y obtenemos la IP. */
        len_buf = recvfrom(fd, buf, TAM_BUF, 0, 
                            (struct sockaddr *)&src, &sa_len);
        if (len_buf == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (len_buf == -1)
            return;

        /* Obtenemos la ip */
        inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
    
        /* Parseamos el mensaje*/
        char port[PORTSTRLEN];
        Resource resources[MAX_RESOURCES_AGENT];
        int res_count;
        if (parse_announce(buf, port, resources, &res_count) == -1)
            return;
    
        printf("ip: %s, ", ip);
        printf("puerto: %s, agent_table->timerfd=",port);

        /* Agregamos o actualizamos el nodo en la tabla */
        int timerfd = agent_manager_get_timerfd(ip, port);

        printf("%d.\n", timerfd);

        if (timerfd < 0) { // Si el nodo no se encuentra en la tabla
            // Creamos el timer
            timerfd = timerfd_create(CLOCK_MONOTONIC, 0);

            // Agregamos el nodo a la tabla
            agent_manager_add(ip, port, res_count, resources, timerfd);  

            // Agregamos el timer a la instancia epoll
            FdInfo *timer_info = epoll_add(timerfd, FD_NODE_TIMER, EPOLLIN | EPOLLET, NULL);
            char *ip_port = ((fd_node_timer_data *)(timer_info->data))->ip_port;
            snprintf(ip_port, INET_ADDRSTRLEN+PORTSTRLEN+2, "%s:%s", ip, port);

            printf("handle_announce: timer_info->ipPort=%s.\n", ((fd_node_timer_data *)(timer_info->data))->ip_port);
        }
        else {
            agent_manager_update(ip, port, resources);
        }
    
        /* Iniciamos/reiniciamos el timer */
        timerfd_start(timerfd, NODE_TIMEOUT_SEC);
    }
}

/* Maneja la recepcion de un mensaje del scheduler */
int handle_scheduler(FdInfo *info) {

    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    while (1) {
        int n = read(info->fd, 
                     data->buf_in + data->len_buf_in,
                     TAM_BUF - data->len_buf_in);
        
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            printf("ernno EAGAIN.\n");
            break;
        }

        if (n == -1)
            return -1;

        // Verificamos si el scheduler cerro la conexion
        if (n == 0) {
            reservation_manager_release_by_socket(info->fd);
            close(info->fd);
            fd_info_destr(info);
            printf("handle_scheduler: scheduler closed connection.\n");
            return -1;
        }
 
        /* Actualizamos el tamano */
        data->len_buf_in += n;

        while (1) {
            /* Verificamos si llegaron los bytes de la longitud */
            if (data->len_buf_in < NBYTES_PACKET_ERL) {
                printf("salimos del while mas interno.\n");
                break;
            }
            uint16_t len_msg;
            memcpy(&len_msg, data->buf_in, NBYTES_PACKET_ERL);
            len_msg = ntohs(len_msg);
            printf("len_msg=%u, buf+2=%s.\n", len_msg, data->buf_in+NBYTES_PACKET_ERL);

            /* Verificamos si llego el mensaje completo */
            if (data->len_buf_in < NBYTES_PACKET_ERL + len_msg)
                break;
    
            // DESDE ACA TENEMOS EL PEDIDO COMPLETO----------------------------------------------------
            // Comando completo en data->buf_in[2, len_msg]

            /* Parseamos el pedido y lo gestionamos */
            char *space = " ", *colon=":";
            char *saveptr1, *saveptr2;
            char *job_id;
            char *command = strtok_r(data->buf_in + NBYTES_PACKET_ERL, space, &saveptr1);
        
            // Buffers para contestar
            char request[TAM_BUF];
            unsigned short len;
            char reply[TAM_BUF];
            unsigned short nlen;
            
            int agent_sock;
            FdInfo* agent_fdinfo;

            if (strncmp(command, "JOB_REQUEST", strlen("JOB_REQUEST")) == 0) {
                // JOB_REQUEST [ ip:port:res:amount ... ]
                job_id = strtok_r(NULL, space, &saveptr1);

                printf("Procesando JOB_REQUEST %s.\n", job_id);
        
                job_req_t reqs[MAX_JOB_RQ];
                int nreqs = 0;

                for (char *token = strtok_r(NULL, space, &saveptr1);
                    token != NULL && nreqs < MAX_JOB_RQ;
                    token = strtok_r(NULL, space, &saveptr1), nreqs++) {

                    // Gestionamos cada 'ip:port:res:amount'
                    char *ip = strtok_r(token, colon, &saveptr2);
                    char *port = strtok_r(NULL, colon, &saveptr2);
                    char *res = strtok_r(NULL, colon, &saveptr2);
                    char *amount = strtok_r(NULL, colon, &saveptr2);                    
        
                    // Verificamos si el agente (ip:port) esta en la tabla de nodos
                    if (agent_manager_get(ip, port) == NULL) { // No se encuentra => no se puede mandar "RESERVE ..."
                        printf("No se encuentra el agente %s:%s.\n", ip, port);
                        
                        // Le avisamos al scheduler
                        len = sprintf(reply, "JOB_DENIED %s", job_id);
                        nlen = htons(len);

                        send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                        send_msg(scheduler_info, reply, len);

                        nreqs = 0;
                        break;
                    }
                    else { // Se encuentra
                        agent_fdinfo = agent_manager_get_fdinfo(ip, port);

                        if (agent_fdinfo == NULL) { // Pero no se establecio conexion
                            // Creamos el socket y nos conectamos al agente
                            agent_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK ,0);

                            struct sockaddr_in addr;
                            addr.sin_family = AF_INET;
                            inet_pton(AF_INET, ip, &addr.sin_addr);
                            addr.sin_port = htons(atoi(port));
                            connect(agent_sock, (struct sockaddr*)&addr, sizeof(addr));

                            agent_fdinfo = epoll_add(agent_sock, FD_AGENT, 
                                                    EPOLLIN | EPOLLET, NULL);
                            
                            // Seteamos la entrada fdinfo en la tabla para proximos pedidos
                            agent_manager_set_fdinfo(ip, port, agent_fdinfo);
                        }
        
                        // Establecida la conexion mandamos el pedido
                        agent_sock = agent_fdinfo->fd;
                        len = sprintf(request, "RESERVE %s %s %s\n", job_id, res, amount);
                        send_msg(agent_fdinfo, request, len);

                        // Guardamos el pedido para agregarlo a la tabla de jobs
                        strncpy(reqs[nreqs].dest_ip, ip, INET_ADDRSTRLEN - 1);
                        reqs[nreqs].dest_ip[INET_ADDRSTRLEN - 1] = '\0';
                        strncpy(reqs[nreqs].res, res, MAX_BYTES_NAME_RESOURCE - 1);
                        reqs[nreqs].res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
                        reqs[nreqs].amount = atoi(amount);
                        reqs[nreqs].granted = 0;
                    }
                }
                if (nreqs != 0) {
                    job_add(atoi(job_id), nreqs, reqs);
                }
            }
        
            // JOB_RELEASE <job_id>
            else if (strncmp(command, "JOB_RELEASE", strlen("JOB_RELEASE")) == 0) {
                job_id = strtok_r(NULL, space, &saveptr1);
                
                printf("Procesando JOB_RELEASE %s.\n", job_id);
                const job_table_t* job = job_get(atoi(job_id));
        
                // Mandamos los "RELEASE" a los ip que pedimos recursos.
                for (int i = 0; i < job->nreqs; i++) {
                    job_req_t req = job->reqs[i];
                    len = sprintf(request, "RELEASE %s %s %d\n",
                                    job_id, req.res, req.amount);
                    agent_fdinfo = agent_manager_get_fdinfo(req.dest_ip, req.dest_port);
                    if (agent_fdinfo != NULL) 
                        send_msg(agent_fdinfo, request, len);
                }
        
                // Eliminamos el job de la tabla
                job_release(atoi(job_id));
            }
            else if (strncmp(command, "GET_NODES", strlen("GET_NODES")) == 0) {
                // ----------------------------------------------------------------------------------------------------
                printf("get_nodes.");
                // ----------------------------------------------------------------------------------------------------
                                
                char *buf = agent_manager_get_nodes();
                len = sprintf(reply, "%s", buf);

                // ----------------------------------------------------------------------------------------------------
                printf("len=%d, reply=%s.\n", len, reply);
                // ----------------------------------------------------------------------------------------------------

                nlen = htons(len);
                send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                
                send_msg(scheduler_info, reply, len);

                // ----------------------------------------------------------------------------------------------------
                printf("tabla mandada.\n");
                // ----------------------------------------------------------------------------------------------------
            }
            else 
                return -1;

            /* Actualizamos el buffer */
            memmove(data->buf_in, 
                    data->buf_in + NBYTES_PACKET_ERL + len_msg, 
                    data->len_buf_in - (NBYTES_PACKET_ERL + len_msg));
            data->len_buf_in -= NBYTES_PACKET_ERL + len_msg;
            (data->buf_in)[data->len_buf_in] = '\0';
            printf("len=%d, buf=%s.\n", data->len_buf_in, data->buf_in);
        }
    }
    return 0;
}
