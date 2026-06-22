#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include "../structures/fdinfo.h"
#include "../structures/table_agent.h"
#include "../structures/table_job.h"
#include "../structures/table_reservation.h"
#include "../consts.h"
#include "functions.h"

/* Maneja el evento EPOLLOUT de un socket tcp */
int handle_tcp_epollout(FdInfo* info) {
    int fd = info->fd;
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    int n = send_msg_tcp(fd, data->buf_out, data->len_buf_out, 
                            info);
    if (n == -1)
        return -1;

    if (n == 0){ // Si mando todo el buffer:
        struct epoll_event ev;
        data->len_buf_out = 0;

        // Sacamos EPOLLOUT de los eventos
        ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
        ev.data.ptr = info;
        if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) == -1)
            return -1;
    } 

    return 0;
}

/* Maneja el evento en el timer para considerar a un 
nodo como caido */
void handle_node_timer(FdInfo* info) { 
    int timerfd = info->fd;
    char* ip = ((fd_node_timer_data*)(info->data))->ip;
    
    // Eliminamos el nodo de la tabla
    agent_manager_delete(ip);
    
    // Eliminamos el timer de epoll y lo cerramos
    epoll_ctl(epollfd, EPOLL_CTL_DEL, timerfd, NULL);
    close(timerfd);
    fd_info_destr(info);
}

/* Maneja el evento en el timer para lanzar el anuncio */
void handle_announce_timer(FdInfo* info) {
    send_announce();
    timerfd_start(info->fd, ANNOUNCE_SEC);
}

/* Maneja un el intento de conexion del scheduler */
void handle_listen_scheduler(FdInfo* info) {
    scheduler_fd = accept(info->fd, NULL, NULL);
    if (scheduler_fd == -1)
        return;

    scheduler_info = epoll_add(scheduler_fd, FD_SCHEDULER, 
                                EPOLLIN | EPOLLHUP | EPOLLERR);
    if(scheduler_info == NULL) {
        close(scheduler_fd);
        return;
    }

    ((fd_tcp_data*)(scheduler_info->data))->len_buf_in = 0;
    ((fd_tcp_data*)(scheduler_info->data))->len_buf_out = 0;

}

/* Maneja un el intento de conexion de un agente */
void handle_agent_connect(FdInfo* info) {
    int agentfd = accept(info->fd, NULL, NULL);
    if (agentfd == -1)
        return;

    FdInfo* agent_info = epoll_add(agentfd, FD_AGENT, 
                                EPOLLIN | EPOLLHUP | EPOLLERR);
    if (agent_info == NULL) {
        close(agentfd);
        return;
    }

    ((fd_tcp_data*)(agent_info->data))->len_buf_in = 0;
    ((fd_tcp_data*)(agent_info->data))->len_buf_out = 0;
}

/* Maneja la recepcion de un mensaje de un agente */
void handle_agent_msg(FdInfo* info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);
    char* buf = data->buf_in;
    int len_buf = data->len_buf_in;

    /* Leemos lo que llego al socket. */
    int n = read(info->fd, buf+len_buf, (TAM_BUF-len_buf)-1);
    buf[len_buf+n] = '\0';

  
    for (char *str1 = buf, *end_of_command; ; str1 = end_of_command+1) {
        end_of_command = strchr(str1, '\n');
        /* Si no esta el comando completo (terminado en '\n') lo guardamos en el buffer. */
        if (end_of_command == NULL) {
            strcpy(buf, str1);
            data->len_buf_in = strlen(str1);
            break;
        }
        
        /* Si esta, lo parseamos y realizamos la accion correspondiente. */ 
        *end_of_command = '\0'; // ahora str1 tiene un comando valido
        char command_name[MAX_LEN_COMMAND_AGENT];
        int job_id;
        char res[MAX_BYTES_NAME_RESOURCE];
        int amount;
        if (parse_node_command(str1, command_name, &job_id, res, &amount) == -1)
            return;

        char reply[TAM_BUF];
        int len;
        unsigned short nlen;

        if (strcmp(command_name, "RESERVE") == 0) {
            /* Intentamos reservar. */
            switch (local_resources_reserve(job_id, info->fd, res, amount)) {
                case -1: // Resource/amount invalido
                    // Le respondemos DENIED <job_id>
                    len = sprintf(reply, "DENIED %d\n", job_id);
                    send_msg_tcp(info->fd, reply, len, info);
                    break;

                case 1: // Cantidad no disponible (lo encola)
                    reservation_manager_add(job_id, info->fd, res, amount, 0);
                    break;

                case 0: // Concedido
                    // Agregamos a la tabla de reservas locales
                    reservation_manager_add(job_id, info->fd, res, amount, 1);

                    // Le respondemos GRANTED <job_id>
                    len = sprintf(reply, "GRANTED %d\n", job_id);
                    send_msg_tcp(info->fd, reply, len, info);
                    break;
            }
        }

        else if (strcmp(command_name, "GRANTED") == 0) {
            char src_ip[INET_ADDRSTRLEN];
            agent_manager_get_ip_by_fd(info->fd, src_ip);
            job_set_granted(job_id, src_ip, 1);
            if (job_check_granted(job_id)) {
                // Le avisamos al scheduler
                len = sprintf(reply, "JOB_GRANTED %d", job_id);
                nlen = htons(len);
                if (send_msg_tcp(scheduler_fd, (char*)&nlen,
                                    NBYTES_PACKET_ERL,
                                    scheduler_info)
                    == -1)
                    return;
                if (send_msg_tcp(scheduler_fd, reply, len,
                                    scheduler_info) == -1)
                    return;
            }

        }
        else if (strcmp(command_name, "RELEASE") == 0) {
            local_resources_release(job_id, info->fd, res, amount);
        }
        else if (strcmp(command_name, "DENIED") == 0) {
                // Le avisamos al scheduler
                len = sprintf(reply, "JOB_DENIED %d", job_id);
                nlen = htons(len);
                if (send_msg_tcp(scheduler_fd, (char*)&nlen,
                                    NBYTES_PACKET_ERL,
                                    scheduler_info)
                    == -1)
                    return;
                if (send_msg_tcp(scheduler_fd, reply, len,
                                    scheduler_info) == -1)
                    return;
        }
    }
}

/* Maneja la desconexion inesperada de un agente */
void handle_agent_disconnect(FdInfo* info) {
    reservation_manager_release_by_socket(info->fd);
    close(info->fd); 
    fd_info_destr(info);
}

/* Maneja la recepcion de un anuncion es el socket udp */
void handle_announce(FdInfo* info) {
    int fd = info->fd;
    char buf[TAM_BUF];
    int len_buf;
    
    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in src;
    socklen_t sa_len = sizeof(src);
    
    /* Leemos el mensaje y obtenemos la IP. */
    len_buf = recvfrom(fd, buf, TAM_BUF, 0, 
                        (struct sockaddr *)&src, &sa_len);
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

        // Agregamos el nodo a la tabla
        agent_manager_add(ip, port, res_count, resources, timerfd);
        
        // Agregamos el timer a la instancia epoll
        FdInfo *timer_info = epoll_add(timerfd, FD_NODE_TIMER, EPOLLIN);
        strncpy(((fd_node_timer_data *)(timer_info->data))->ip, ip,
                INET_ADDRSTRLEN);
    }
    else {
        agent_manager_update(ip, resources);
    }

    /* Iniciamos/reiniciamos el timer */
    timerfd_start(timerfd, NODE_TIMEOUT_SEC);
}


/* Maneja la recepcion de un mensaje del scheduler */
int handle_scheduler(FdInfo *info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);
    int fd = info->fd;
    char *buf = data->buf_in;
    int len_buf = data->len_buf_in;

    int n; // cantidad de caracteres leidos/escritos (con read/write)

    /* Obtenemos la longitud del mensaje. */
    unsigned short len_msg;
    if (len_buf < NBYTES_PACKET_ERL) { // si se recibio una cantidad parcial de bytes de la longitud
        n = read(fd, buf+len_buf, NBYTES_PACKET_ERL-len_buf);
        if (n < 0)
            return -1;

        data->len_buf_in += n;
        len_buf = data->len_buf_in;
        if (len_buf < NBYTES_PACKET_ERL)
            return 0;
    }
    memcpy(&len_msg, buf, NBYTES_PACKET_ERL);
    len_msg = ntohs(len_msg);    
    
    /* Leemos el mensaje. */
    int read_msg = len_buf-NBYTES_PACKET_ERL; // cantidad de caracteres ya leidos del mensaje
    n = read(fd, buf+len_buf, len_msg-read_msg);
    if (n < 0)
        return -1;

    data->len_buf_in += n;
    len_buf = data->len_buf_in;
    if (len_buf < NBYTES_PACKET_ERL + len_msg)
        return 0;

    buf[NBYTES_PACKET_ERL + len_msg] = '\0';
    
    /* Parseamos el pedido y lo gestionamos */
    char *delim1 = " ", *delim2=":";
    char *saveptr1, *saveptr2;
    char *job_id;
    char *command = strtok_r(buf + NBYTES_PACKET_ERL, delim1, &saveptr1); // command: JOB_REQUEST, JOB_RELEASE o JOB_STATUS

    char request[TAM_BUF];
    unsigned short len;
    char reply[TAM_BUF];
    unsigned short nlen;

    // JOB_REQUEST [ @host:res:amount ... ]
    if (strncmp(command, "JOB_REQUEST", strlen("JOB_REQUEST")) == 0) {

        job_id = strtok_r(NULL, delim1, &saveptr1);

        // Gestionamos cada '@host:res:amount'
        job_req_t reqs[MAX_JOB_RQ];
        int nreqs = 0;
        for (char *token = strtok_r(NULL, delim1, &saveptr1);
            token != NULL && nreqs < MAX_JOB_RQ;
            token = strtok_r(NULL, delim1, &saveptr1), nreqs++) {

            char *host = strtok_r(token, delim2, &saveptr2);
            char *res = strtok_r(NULL, delim2, &saveptr2);
            char *amount = strtok_r(NULL, delim2, &saveptr2);
            
            // Guardamos el pedido para agregarlo a la tabla de jobs
            strncpy(reqs[nreqs].dest_ip, host, INET_ADDRSTRLEN - 1);
            reqs[nreqs].dest_ip[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(reqs[nreqs].res, res, MAX_BYTES_NAME_RESOURCE - 1);
            reqs[nreqs].res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            reqs[nreqs].amount = atoi(amount);
            reqs[nreqs].granted = 0;

            // Verificamos si el host (nodo) esta en la tabla de nodos
            if (agent_manager_get(host) == NULL) { 
                // No se encuentra => no se puede mandar "RESERVE ..."

                // Le avisamos al scheduler
                len = sprintf(reply, "JOB_DENIED %s", job_id);
                nlen = htons(len);
                if (send_msg_tcp(fd, (char*)&nlen, NBYTES_PACKET_ERL, info)
                    == -1)
                    return -1;
                if (send_msg_tcp(fd, reply, len, info) == -1)
                    return -1;
            }
            else {
                // Se encuentra
                FdInfo* fdinfo_host = agent_manager_get_fdinfo(host);
                int sock_host;
                if (fdinfo_host == NULL) { 
                    // Pero no se establecio conexion

                    // Creamos el socket y nos conectamos al host
                    sock_host = socket(AF_INET, SOCK_STREAM ,0);
                    int port_host = atoi(agent_manager_get_port(host));
                    struct sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    inet_pton(AF_INET, host, &addr.sin_addr);
                    addr.sin_port = htons(port_host);
                    connect(sock_host, (struct sockaddr*)&addr, 
                            sizeof(addr));
                    fdinfo_host = epoll_add(sock_host, FD_AGENT, EPOLLIN
                                            | EPOLLHUP | EPOLLERR);
                    agent_manager_set_fdinfo(host, fdinfo_host);
                }

                // Establecida la conexion mandamos el pedido
                sock_host = fdinfo_host->fd;
                len = sprintf(request, "RESERVE %s %s %s\n", 
                                job_id, res, amount);
                send_msg_tcp(sock_host, request, len, info);
            }
        }
        job_add(atoi(job_id), nreqs, reqs);
    }

    // JOB_RELEASE <job_id>
    else if (strncmp(command, "JOB_RELEASE", strlen("JOB_RELEASE")) == 0) {
        job_id = strtok_r(NULL, delim1, &saveptr1);
        const job_table_t* job = job_get(atoi(job_id));

        // Mandamos los "RELEASE" a los host que pedimos recursos.
        for (int i = 0; i < job->nreqs; i++) {
            len = sprintf(request, "RELEASE %s %s %d\n",
                            job_id,
                            (job->reqs[i]).res,
                            (job->reqs[i]).amount);
            FdInfo* fdinfo = 
                agent_manager_get_fdinfo((char *)(job->reqs[i]).dest_ip);
            if (fdinfo != NULL) 
                send_msg_tcp(fdinfo->fd, request, len, fdinfo);
        }

        // Eliminamos el job de la tabla
        job_release(atoi(job_id));
    }

    else 
        return -1;

    data->len_buf_in = 0;    
    return 0;
}