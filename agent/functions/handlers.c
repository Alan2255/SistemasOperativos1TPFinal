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

    /* Iniciamos el timer */
    timerfd_start(info->fd, ANNOUNCE_SEC);
}

/* Maneja el intento de conexion del scheduler */
void handle_listen_scheduler(FdInfo* info) {
    scheduler_fd = accept4(info->fd, NULL, NULL, SOCK_NONBLOCK);
    if (scheduler_fd == -1)
        return;

    scheduler_info = epoll_add(scheduler_fd, FD_SCHEDULER, EPOLLIN | EPOLLET , NULL);
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
    
    // Si ya hay cosas encoladas, mantenemos el orden FIFO metiendo lo nuevo atrás
    if (data->len_buf_out > 0) {
        add_to_buffer(info, msg, len);
    }
    else {
        int n = send(info->fd, msg, len, MSG_NOSIGNAL);
        
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // El socket está temporalmente lleno. Encolamos TODO el mensaje
                add_to_buffer(info, msg, len);
                epoll_add(info->fd, info->type, 
                          EPOLLOUT | EPOLLIN | EPOLLET, info);
            }
            else {
                // Errores reales (EPIPE, ECONNRESET, etc.)
                pthread_mutex_unlock(&data->mutex_out);
                return -1; // Retornamos error real
            }
        }
        else if (n < len) {
            // Envío parcial: encolamos lo que faltó mandar
            add_to_buffer(info, msg + n, len - n);
            epoll_add(info->fd, info->type, 
                        EPOLLOUT | EPOLLIN | EPOLLET, info);
        }
    }
    pthread_mutex_unlock(&data->mutex_out);

    return 0; // Éxito (o el mensaje ya quedó encolado de forma segura)
}

/* Maneja la recepcion de un mensaje de un agente */
void handle_agent_msg(FdInfo* info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_in);
    // Leemos el socket hasta EAGAIN y procesamos los mensajes leidos
    while (1) {
        int n = read(info->fd, data->buf_in + data->len_buf_in,
                        TAM_BUF - data->len_buf_in - 1); // -1 para asegurar espacio del \0   
        
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { 
                break;
            }
            else { 
                perror("handle_agent_msg");
                pthread_mutex_unlock(&data->mutex_in);
                return;
            }
        }
        else if (n == 0) { 
            reservation_manager_release_by_socket(info->fd);
            close(info->fd);
            fd_info_destr(info);
            printf("handle_agent_msg: Agent closed connection.\n");
            pthread_mutex_unlock(&data->mutex_in);
            return;
        }

        data->len_buf_in += n;
        data->buf_in[data->len_buf_in] = '\0';
        // printf("Buffer crudo recibido: [%s]\n", data->buf_in);

        // --- SOLUCIÓN DEPROCESAMIENTO PORLÍNEAS SEGURO ---
        char* read_ptr = data->buf_in;
        
        while (1) {
            // Buscamos el siguiente '\n' a partir de donde quedamos
            char* end_of_command = strchr(read_ptr, '\n');
            if (end_of_command == NULL) {
                // No hay más mensajes completos en esta ráfaga
                break;
            }
            
            // Delimitamos el string reemplazando el '\n' por '\0'
            *end_of_command = '\0';

            /* Parseamos el comando usando la posición actual de lectura */
            char command_name[MAX_LEN_COMMAND_AGENT];
            int job_id;
            char res[MAX_BYTES_NAME_RESOURCE];
            int amount;

            if (parse_node_command(read_ptr, command_name, &job_id, res, &amount) != -1) {
                
                printf("[handle_agent_msg] %s\n", command_name);
                char reply[TAM_BUF];
                int len;
                if (strcmp(command_name, "RESERVE") == 0) {
                    // printf("Procesando RESERVE para Job %d\n", job_id);
                    printf("[handle_agent_msg] procesando 'RESERVE %d'\n", job_id);

                    switch (local_resources_reserve(job_id, info->fd, res, amount)) {
                        case -1:
                            len = sprintf(reply, "DENIED %d\n", job_id);
                            send_msg(info, reply, len);
                            break;
                        case 1:
                            reservation_manager_add(job_id, info->fd, res, amount, 0);
                            break;
                        case 0:
                            reservation_manager_add(job_id, info->fd, res, amount, 1);
                            len = sprintf(reply, "GRANTED %d\n", job_id);
                            send_msg(info, reply, len);
                            break;
                    }
                }
                else if (strcmp(command_name, "GRANTED") == 0) {
                    printf("[handle_agent_msg] procesando 'GRANTED %d'\n", job_id);

                    char ip[INET_ADDRSTRLEN];
                    char port[PORTSTRLEN];
                    agent_manager_get_addr_by_fd(info->fd, ip, port);
                    job_set_granted(job_id, ip, port, 1);

                    printf("[handle_agent_msg] buscamos job_id=%d en la job_table y obtenemos: ", job_id);
                    const job_table_t * entry = job_get(job_id);
                    if (entry != NULL) {
                        printf("'%s:%s:%s:%d granted=%d'", entry->reqs[0].dest_ip, entry->reqs[0].dest_port, entry->reqs[0].res, entry->reqs[0].amount, entry->reqs[0].granted);
                        for (int i = 1; i < entry->nreqs; i++) {
                        printf(", '%s:%s:%s:%d granted=%d'", entry->reqs[0].dest_ip, entry->reqs[0].dest_port, entry->reqs[0].res, entry->reqs[0].amount, entry->reqs[0].granted);
                        }
                    }
                    printf(".\n");

                    int job_is_granted = job_check_granted(job_id);
                    if (job_is_granted) { 
                        printf("[handle_agent_msg] mandando JOB_GRANTED %d al scheduler.\n", job_id);

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
                    len = sprintf(reply, "JOB_DENIED %d", job_id);
                    unsigned short nlen = htons(len);
                    send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                    send_msg(scheduler_info, reply, len);
                    job_release(job_id);
                }
            } else {
                printf("handle_agent_msg (invalid request: %s).\n", read_ptr);
            }

            // Avanzamos el puntero de lectura al siguiente caracter después del '\n'
            read_ptr = end_of_command + 1;
        }

        /* --- AQUÍ REINICIAMOS EL BUFFER --- */
        int bytes_procesados = read_ptr - data->buf_in;
        
        if (bytes_procesados >= data->len_buf_in) {
            // Si procesamos todo hasta el final (terminó en \n), RESETEAMOS A 0
            data->len_buf_in = 0;
            data->buf_in[0] = '\0';
            // printf("Buffer completamente procesado y reseteado a 0.\n");
        } else if (bytes_procesados > 0) {
            // Si quedó un mensaje a la mitad (no alcanzó a tener \n), nos traemos solo ese pedazo al inicio
            memmove(data->buf_in, read_ptr, data->len_buf_in - bytes_procesados);
            data->len_buf_in -= bytes_procesados;
            data->buf_in[data->len_buf_in] = '\0';
            printf("Mensaje incompleto remanente. Nuevo len_buf_in = %d\n", data->len_buf_in);
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
    
    // printf("Evento de agente.\n");

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
    
        // printf("ip: %s, ", ip);
        // printf("puerto: %s, agent_table->timerfd=",port);

        /* Agregamos o actualizamos el nodo en la tabla */
        int timerfd = agent_manager_get_timerfd(ip, port);

        // printf("%d.\n", timerfd);

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
            // printf("ernno EAGAIN.\n");
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
                // printf("salimos del while mas interno.\n");
                break;
            }
            uint16_t len_msg;
            memcpy(&len_msg, data->buf_in, NBYTES_PACKET_ERL);
            len_msg = ntohs(len_msg);
            // printf("len_msg=%u, buf+2=%s.\n", len_msg, data->buf_in+NBYTES_PACKET_ERL);

            /* Verificamos si llego el mensaje completo */
            if (data->len_buf_in < NBYTES_PACKET_ERL + len_msg)
                break;
    
            // Clonamos el mensaje en un buffer temporal
            char working_buf[TAM_BUF];
            memcpy(working_buf, data->buf_in + NBYTES_PACKET_ERL, len_msg);
            working_buf[len_msg] = '\0'; // Nos aseguramos el fin de cadena string en C

            /* Parseamos el pedido y lo gestionamos sobre la copia aislada */
            char *space = " ", *colon=":";
            char *saveptr1, *saveptr2;
            char *job_id;
            
            char *command = strtok_r(working_buf, space, &saveptr1);

            printf("[handle_scheduler] %s\n",command);
        
            // Buffers para contestar
            char request[TAM_BUF];
            unsigned short len;
            char reply[TAM_BUF];
            unsigned short nlen;
            
            int agent_sock;
            FdInfo* agent_fdinfo;

            if (command != NULL && strncmp(command, "JOB_REQUEST", strlen("JOB_REQUEST")) == 0) {
                // JOB_REQUEST [ ip:port:res:amount ... ]
                job_id = strtok_r(NULL, space, &saveptr1);

                // printf("[handle_scheduler] job_table before JOB_REQUEST %s:\n", job_id);
                // char *job_table_str = job_table_to_string();
                // printf("%s\n", job_table_str);
                // if (job_table_str != NULL) free(job_table_str);



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
                    if (agent_manager_get(ip, port) == NULL) { 
                        printf("No se encuentra el agente %s:%s.\n", ip, port);
                        
                        // Le avisamos al scheduler
                        len = sprintf(reply, "JOB_DENIED %s", job_id);
                        nlen = htons(len);

                        send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                        send_msg(scheduler_info, reply, len);

                        nreqs = 0;
                        break;
                    }
                    else { 
                        agent_fdinfo = agent_manager_get_fdinfo(ip, port);

                        if (agent_fdinfo == NULL) { 
                            agent_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK ,0);

                            struct sockaddr_in addr;
                            addr.sin_family = AF_INET;
                            inet_pton(AF_INET, ip, &addr.sin_addr);
                            addr.sin_port = htons(atoi(port));
                            connect(agent_sock, (struct sockaddr*)&addr, sizeof(addr));

                            agent_fdinfo = epoll_add(agent_sock, FD_AGENT, 
                                                    EPOLLIN | EPOLLET, NULL);
                            
                            agent_manager_set_fdinfo(ip, port, agent_fdinfo);
                        }
        
                        agent_sock = agent_fdinfo->fd;
                        len = sprintf(request, "RESERVE %s %s %s\n", job_id, res, amount);
                        send_msg(agent_fdinfo, request, len);
                        // printf("%s.\n", request);

                        strncpy(reqs[nreqs].dest_ip, ip, INET_ADDRSTRLEN - 1);
                        reqs[nreqs].dest_ip[INET_ADDRSTRLEN - 1] = '\0';
                        strncpy(reqs[nreqs].dest_port, port, PORTSTRLEN - 1);
                        reqs[nreqs].dest_port[PORTSTRLEN - 1] = '\0';
                        strncpy(reqs[nreqs].res, res, MAX_BYTES_NAME_RESOURCE - 1);
                        reqs[nreqs].res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
                        reqs[nreqs].amount = atoi(amount);
                        reqs[nreqs].granted = 0;
                    }
                }
                if (nreqs != 0) {
                    job_add(atoi(job_id), nreqs, reqs);
                }

                // printf("[handle_scheduler] job_table after JOB_REQUEST %s:\n", job_id);
                // job_table_str = job_table_to_string();
                // printf("%s\n", job_table_str);
                // if (job_table_str != NULL) free(job_table_str);

            }
            else if (command != NULL && strncmp(command, "JOB_RELEASE", strlen("JOB_RELEASE")) == 0) {
                job_id = strtok_r(NULL, space, &saveptr1);
                printf("[handle_scheduler] Procesando JOB_RELEASE %s.\n", job_id);

                const job_table_t* job = job_get(atoi(job_id));

                // printf("[handle_scheduler] job_table before JOB_RELEASE %s\n", job_id);
                // char *job_table_str = job_table_to_string();
                // printf("%s\n", job_table_str);
                // if (job_table_str != NULL) free(job_table_str);


                // Mandamos "RELEASE ..." a cada agente que le mandamos "RESERVE ..."
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

                // printf("[handle_scheduler] job_table after JOB_RELEASE %s\n", job_id);
                // job_table_str = job_table_to_string();
                // printf("%s\n", job_table_str);
                // if (job_table_str != NULL) free(job_table_str);

            }
            else if (command != NULL && strncmp(command, "GET_NODES", strlen("GET_NODES")) == 0) {
                // printf("get_nodes.");
                                
                char *buf = agent_manager_get_nodes();
                len = sprintf(reply, "%s", buf);

                // printf("len=%d, reply=%s.\n", len, reply);

                nlen = htons(len);
                send_msg(scheduler_info, (char*)&nlen, NBYTES_PACKET_ERL);
                send_msg(scheduler_info, reply, len);

                printf("[handle_scheduler] tabla mandada.\n");
            }
            else {
                return -1;
            }

            /* Actualizamos el buffer */
            int bytes_procesados = NBYTES_PACKET_ERL + len_msg;
            memmove(data->buf_in, 
                    data->buf_in + bytes_procesados, 
                    data->len_buf_in - bytes_procesados);
            data->len_buf_in -= bytes_procesados;
            (data->buf_in)[data->len_buf_in] = '\0';
            
            // printf("len residual=%d, buf actual=%s.\n", data->len_buf_in, data->buf_in);
        }
    }
    return 0;
}