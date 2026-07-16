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
#include "../structures/fd_table.h"
#include "../structures/table_agent.h"
#include "../structures/table_job.h"
#include "../structures/table_reservation.h"
#include "../consts.h"
#include "functions.h"
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

/* Separa el string en tokens y guarda una referencia a cada uno en 'tokens'. */
static int tokenize_str(char *str, char *delim, int max_tokens, char **tokens) {
    int i = 0;

    if (str == NULL || delim == NULL || tokens == NULL || max_tokens <= 0)
        return 0;

    for (char *token = strtok(str, delim);
         token != NULL && i < max_tokens;
         i++)
    {
        tokens[i] = token;
        token = strtok(NULL, delim);
    }

    return i;
}

/* Parsea el anuncio "ANNOUNCE <puerto> <recursos>"
y copia los datos en los parametros pasados. */
static int parse_announce(char *msg, char *port, Resource *resources, int *res_count) {
    char* tokens[2 + MAX_RESOURCES_AGENT];
    int count_tokens = tokenize_str(msg, " ", 2 + MAX_RESOURCES_AGENT, tokens);

    // Parseamos el comando
    if (count_tokens <= 2 || strcmp(tokens[0], "ANNOUNCE") != 0) {
        return -1;
    }

    snprintf(port, PORTSTRLEN, "%s", tokens[1]);

    // Copiamos los recursos
    *res_count = count_tokens - 2;
    for(int i = 0; i < *res_count; i++) {
        char* subtokens[2]; // <res>:<amount>
        tokenize_str(tokens[2+i], ":", 2, subtokens);

        // Copiamos el nombre del recurso usando snprintf
        snprintf(resources[i].name, MAX_BYTES_NAME_RESOURCE, "%s", subtokens[0]);

        resources[i].total_capacity = atoi(subtokens[1]);
        resources[i].available = atoi(subtokens[1]);
    }
    return 0;
}

/* Anade el string al buffer 'buf_out' de 'info'. */
static int add_to_buffer(FdEntry* info, char* msg, int len) {
    fd_tcp_data* data = info->data;

    if (len < TAM_BUF - data->len_buf_out) {
        printf("buf_out sin espacio.\n");
        return -1;
    }
    else {
        memcpy(data->buf_out + data->len_buf_out, msg, len);
        data->len_buf_out += len;
    }

    return 0;
}

static int send_msg(uint64_t id, FdEntry* info, char* msg, int len) {
    fd_tcp_data* data = info->data;

    pthread_mutex_lock(&data->mutex_out);

    // Si ya hay cosas encoladas, mantenemos el orden FIFO metiendo lo nuevo atras
    if (data->len_buf_out > 0) {
        add_to_buffer(info, msg, len);
    }
    else {
        int n = send(info->fd, msg, len, MSG_NOSIGNAL);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // El socket esta temporalmente lleno, encolamos TODO el mensaje
                add_to_buffer(info, msg, len);
                epoll_mod(info->fd, EPOLLOUT | EPOLLIN | EPOLLET, id);
            }
            else {
                // Errores reales (EPIPE, ECONNRESET, etc.)
                pthread_mutex_unlock(&data->mutex_out);
                return -1; // Retornamos error real, el caller decide como cerrar
            }
        }
        else if (n < len) {
            // Envio parcial, encolamos lo que falta
            add_to_buffer(info, msg + n, len - n);
            epoll_mod(info->fd, EPOLLOUT | EPOLLIN | EPOLLET, id);
        }
    }
    pthread_mutex_unlock(&data->mutex_out);

    return 0;
}

/* Maneja el evento EPOLLOUT de un socket tcp. */
int handle_tcp_epollout(uint64_t id, FdEntry* info) {
    printf("handle_tcp_epollout.\n");
    if (info == NULL)
        return -1;

    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_out);

    int n = send(info->fd, data->buf_out, data->len_buf_out, MSG_NOSIGNAL);
    if (n == -1) {
        pthread_mutex_unlock(&data->mutex_out);
        if (info->type == FD_AGENT)
            close_agent_conn(id, info);
        else
            close_scheduler_conn(info);
        return -1;
    }

    // Actualizamos el buffer
    memmove(data->buf_out, data->buf_out + n, data->len_buf_out - n);
    data->len_buf_out -= n;

    if (data->len_buf_out == 0) { // Si se mando todo sacamos EPOLLOUT de los eventos
        epoll_mod(info->fd, EPOLLIN | EPOLLET, id);
    }

    pthread_mutex_unlock(&data->mutex_out);

    return 0;
}


/* Maneja el evento en el timer para considerar a un
nodo como caido. */
void handle_node_timer(FdEntry* info) {
    fd_node_timer_data* data = info->data;

    // Eliminamos el nodo de la tabla
    agent_table_delete(data->ip, data->port);

    // Eliminamos el timer de epoll, lo cerramos y liberamos su id
    fd_table_request_close(info);
}


/* Maneja el evento en el timer para lanzar el anuncio. */
void handle_announce_timer(FdEntry* info) {
    // Vaciamos el fd
    uint64_t nexpirations;
    read(info->fd, &nexpirations, sizeof(nexpirations));

    // Mandamos el anuncio
    send_announce();

    // Iniciamos el timer
    timer_start(info->fd, ANNOUNCE_SEC);
}


/* Maneja el intento de conexion del scheduler. */
void handle_listen_scheduler(FdEntry* info) {
    scheduler_fd = accept4(info->fd, NULL, NULL, SOCK_NONBLOCK);
    if (scheduler_fd == -1)
        return;

    uint64_t new_id = fd_table_add(scheduler_fd, FD_SCHEDULER);
    if (new_id == UINT64_MAX) {
        close(scheduler_fd);
        return;
    }

    if (epoll_add(scheduler_fd, EPOLLIN | EPOLLET, new_id) == -1) {
        fd_table_undo_add(scheduler_fd);
        close(scheduler_fd);
        return;
    }
    scheduler_id = new_id;

    printf("[handle_listen_scheduler] se conecto el scheduler con el id %" PRIx64 "\n", scheduler_id);

}


/* Maneja el intento de conexion de un agente. */
void handle_agent_connect(FdEntry* info) {
    // Aceptamos todos los clientes que llegaron
    while (1) {
        int agent_fd = accept4(info->fd, NULL, NULL, SOCK_NONBLOCK);

        if (agent_fd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;

        if (agent_fd == -1)
            return;

        uint64_t agent_id = fd_table_add(agent_fd, FD_AGENT);
        if (agent_id == UINT64_MAX) {
            close(agent_fd);
            return;
        }

        if (epoll_add(agent_fd, EPOLLIN | EPOLLET, agent_id) == -1) {
            fd_table_undo_add(agent_fd);
            close(agent_fd);
            return;
        }

        printf("[handle_agent_connect] aceptamos un agente con el id 0x%" PRIx64 "\n", agent_id);
        
    }
}


/* Maneja la recepcion de un anuncion en el socket udp. */
void handle_announce(FdEntry* info) {

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
        buf[len_buf] = '\0';

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
        int timerfd = agent_table_get_timerfd(ip, port);

        // printf("%d.\n", timerfd);

        if (timerfd == -1) {
            perror("agent_table_get_timerfd");
        }
        else if (timerfd == 0) { // Si el nodo no se encuentra en la tabla
            // Creamos el timer
            timerfd = timerfd_create(CLOCK_MONOTONIC, 0);

            // Agregamos el nodo a la tabla
            agent_table_add(ip, port, res_count, resources, timerfd);

            // Agregamos el timer a la instancia epoll
            uint64_t timer_id = fd_table_add(timerfd, FD_NODE_TIMER);
            if (timer_id == UINT64_MAX) {
                close(timerfd);
                return;
            }
            if (epoll_add(timerfd, EPOLLIN | EPOLLET, timer_id) == -1) {
                fd_table_undo_add(timerfd);
                close(timerfd);
                return;
            }

            FdEntry *timer_entry = fd_table_get_and_inc(timer_id);
            strcpy(((fd_node_timer_data *)(timer_entry->data))->ip, ip);
            strcpy(((fd_node_timer_data *)(timer_entry->data))->port, port);

            printf("[handle_announce] nuevo agente %s:%s res_count=%d ",
                ((fd_node_timer_data *)(timer_entry->data))->ip,
                ((fd_node_timer_data *)(timer_entry->data))->port,
                res_count);
            printf("%s:%d", resources[0].name, resources[0].available);
            for (int i = 1; i < res_count; i++) {
                printf(", %s:%d", resources[i].name, resources[i].available);
            }
            printf(".\n");

            fd_table_dec_and_release(timer_entry);
        }
        else {
            agent_table_update(ip, port, resources, res_count);
        }

        /* Iniciamos/reiniciamos el timer */
        timer_start(timerfd, NODE_TIMEOUT_SEC);
    }
}

static int send_job_denied(char* job_id_str, int has_reference, FdEntry *scheduler_entry) {
    char reply[TAM_BUF];
    unsigned short len = sprintf(reply + NBYTES_PACKET_ERL, "JOB_DENIED %s", job_id_str);
    unsigned short nlen = htons(len);
    memcpy(reply, (char*)&nlen, NBYTES_PACKET_ERL);

    if (!has_reference) {
        scheduler_entry = fd_table_get_and_inc(scheduler_id);
        if (!scheduler_entry) {
            return -1;
        }
    }

    if (send_msg(scheduler_id, scheduler_entry, reply, NBYTES_PACKET_ERL + len) == -1) {
        close_scheduler_conn(scheduler_entry);
    }

    if (!has_reference) {
        fd_table_dec_and_release(scheduler_entry);
    }

    return 0;
}

/* Procesa "RESERVE <job_id> <res> <amount>" recibido de otro agente. */
static void reserve(uint64_t id, FdEntry *info, char *job_id_str, char *res, char *amount_str) {
    if (job_id_str == NULL || res == NULL || amount_str == NULL)
        return;

    int job_id = atoi(job_id_str);
    int amount = atoi(amount_str);

    printf("[handle_agent] procesando 'RESERVE %d %s %d'\n", job_id, res, amount);

    char reply[TAM_BUF];
    int len;
    switch (local_resources_reserve(job_id, info->fd, res, amount)) {
        case -1: // No se pudo conceder
            len = sprintf(reply, "DENIED %d\n", job_id);
            if (send_msg(id, info, reply, len) == -1)
                close_agent_conn(id, info);
            break;
        case 1: // Se encolo
            reservation_table_add(job_id, info->fd, res, amount, 0);
            break;
        case 0: // Se concedio
            reservation_table_add(job_id, info->fd, res, amount, 1);
            len = sprintf(reply, "GRANTED %d\n", job_id);
            if (send_msg(id, info, reply, len) == -1)
                close_agent_conn(id, info);

            char buff[TAM_BUF];
            local_resources_to_str(buff);
            printf("[handle_agent] local_resources=%s\n", buff);

            break;
    }
}

/* Procesa "GRANTED <job_id>" recibido de otro agente. */
static void granted(char *job_id_str) {
    if (job_id_str == NULL)
        return;

    int job_id = atoi(job_id_str);
    printf("[handle_agent] procesando 'GRANTED %d'\n", job_id);

    job_table_inc_ngranted(job_id);

    if (job_table_check_granted(job_id)) {
        printf("[handle_agent] mandando ");

        char reply[TAM_BUF];
        unsigned short len = sprintf(reply + NBYTES_PACKET_ERL, "JOB_GRANTED %d", job_id);
        unsigned short nlen = htons(len);
        memcpy(reply, (char*)&nlen, NBYTES_PACKET_ERL);

        FdEntry *scheduler_entry = fd_table_get_and_inc(scheduler_id);
        if (scheduler_entry != NULL) {
            if (send_msg(scheduler_id, scheduler_entry, reply, NBYTES_PACKET_ERL + len) == -1)
                close_scheduler_conn(scheduler_entry);
            fd_table_dec_and_release(scheduler_entry);
        }

        printf("len = 0x%x, %s.\n", *((unsigned char*)reply + 1), reply + 2);
    }
}

/* Procesa "RELEASE <job_id> <res> <amount>" recibido de otro agente. */
static void release(FdEntry *info, char *job_id_str, char *res, char *amount_str) {
    if (job_id_str == NULL || res == NULL || amount_str == NULL)
        return;

    int job_id = atoi(job_id_str);
    int amount = atoi(amount_str);

    printf("[handle_agent] procesando 'RELEASE %d %s %d'\n", job_id, res, amount);

    local_resources_release(job_id, info->fd, res, amount);

    char buff[TAM_BUF];
    local_resources_to_str(buff);
    printf("[handle_agent] local_resources=%s\n", buff);

    reservation_table_release(job_id, info->fd, res);
}

/* Procesa "DENIED <job_id>" recibido de otro agente. */
static void denied(char *job_id_str) {
    if (!job_id_str)
        return;

    send_job_denied(job_id_str, 0, NULL);

    job_table_release(atoi(job_id_str));
}

/* Maneja la recepcion de un mensaje de un agente, acumula
bytes hasta tener un comando completo y lo procesa. */
void handle_agent(uint64_t id, FdEntry* info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_in);
    // Leemos el socket hasta EAGAIN y procesamos los mensajes leidos
    while (1) {
        int n = read(info->fd, 
                     data->buf_in + data->len_buf_in,
                     TAM_BUF - data->len_buf_in - 1); // -1 para asegurar espacio del \0

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n == -1 || n == 0) {
            printf("handle_agent: agent closed connection or an error occurred.\n");
            pthread_mutex_unlock(&data->mutex_in);
            close_agent_conn(id, info);
            return;
        }

        data->len_buf_in += n;
        data->buf_in[data->len_buf_in] = '\0';
        // printf("Buffer crudo recibido: [%s]\n", data->buf_in);

        char* read_ptr = data->buf_in;

        while (1) {
            // Verificamos si tenemos un comando completo
            char* end_of_command = strchr(read_ptr, '\n');
            if (end_of_command == NULL) {
                break;
            }

            // Obtenemos el comando completo, reemplazando el '\n' por '\0' 
            *end_of_command = '\0';
            char *command = read_ptr;

            char *space = " ";
            char *saveptr1;
            char *command_name = strtok_r(command, space, &saveptr1);
            char *job_id_str = strtok_r(NULL, space, &saveptr1);
            char *res = strtok_r(NULL, space, &saveptr1);
            char *amount_str = strtok_r(NULL, space, &saveptr1);

            if (command_name != NULL && strncmp(command_name, "RESERVE", strlen("RESERVE")) == 0) {
                reserve(id, info, job_id_str, res, amount_str);
            }
            else if (command_name != NULL && strncmp(command_name, "GRANTED", strlen("GRANTED")) == 0) {
                granted(job_id_str);
            }
            else if (command_name != NULL && strncmp(command_name, "RELEASE", strlen("RELEASE")) == 0) {
                release(info, job_id_str, res, amount_str);
            }
            else if (command_name != NULL && strncmp(command_name, "DENIED", strlen("DENIED")) == 0) {
                denied(job_id_str);
            }
            else {
                printf("[handle_agent] invalid request.\n");
            }

            // Avanzamos el puntero de lectura al siguiente caracter después del '\n'
            read_ptr = end_of_command + 1;
        }

        // Reiniciamos el buffer
        int bytes_procesados = read_ptr - data->buf_in;

        if (bytes_procesados >= data->len_buf_in) {
            // Si procesamos todo hasta el final (terminó en \n), RESETEAMOS A 0
            data->len_buf_in = 0;
            data->buf_in[0] = '\0';
            // printf("Buffer completamente procesado y reseteado a 0.\n");
        } 
        else if (bytes_procesados > 0) {
            // Si quedó un mensaje a la mitad (no alcanzó a tener \n), nos traemos solo ese pedazo al inicio
            memmove(data->buf_in, read_ptr, data->len_buf_in - bytes_procesados);
            data->len_buf_in -= bytes_procesados;
            data->buf_in[data->len_buf_in] = '\0';
            printf("Mensaje incompleto remanente. Nuevo len_buf_in = %d, buf_in=%s.\n", data->len_buf_in, data->buf_in);
        }
    }

    pthread_mutex_unlock(&data->mutex_in);
}


/* Procesa "JOB_REQUEST <job_id> <ip:port:res:amount> ..." 
(si el agente de ip:port esta en la tabla le manda "RESERVE 
<res> <amount>, en caso contrario le avisa al scheduler y 
descarta todo el job. */
static void job_request(FdEntry *info, char *job_id, char *reqs_str) {
    if (!info || !job_id || !reqs_str)
        return;
        
    printf("[handle_scheduler] procesando JOB_REQUEST %s\n", job_id);
    
    job_req_t reqs[MAX_JOB_RQ];
    int nreqs = 0;
    
    char request[TAM_BUF];
    unsigned short len;
    
    FdEntry* agent_info;
    
    // Gestionamos cada 'ip:port:res:amount'
    char *space = " ", *colon = ":";
    char *saveptr1, *saveptr2;
    char *token = strtok_r(reqs_str, space, &saveptr1);
    for (; token != NULL && nreqs < MAX_JOB_RQ;
         token = strtok_r(NULL, space, &saveptr1), nreqs++) {

        // Obtenemos cada campo
        char *ip = strtok_r(token, colon, &saveptr2);
        char *port = strtok_r(NULL, colon, &saveptr2);
        char *res = strtok_r(NULL, colon, &saveptr2);
        char *amount = strtok_r(NULL, colon, &saveptr2);

        printf("[handle_scheduler] procesando %s:%s:%s:%s\n", ip, port, res, amount);

        uint64_t agent_id;
        if (agent_table_get_id(ip, port, &agent_id) <= 0) { 
            // El agente no esta en la tabla de nodos
            printf("[handle_scheduler] No se encuentra el agente %s:%s.\n", ip, port);

            send_job_denied(job_id, 1, info);
            nreqs = 0;
            break;
        }
        else {
            // El agente esta en la tabla de nodos
            
            if (agent_id == UINT64_MAX) {
                // No se establecio conexion

                // Creamos el socket
                int agent_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
                if (agent_fd == -1) {
                    printf("[handle_scheduler] error (socket) estableciendo conexion con el agente %s:%s. \n", ip, port);

                    send_job_denied(job_id, 1, info);
                    nreqs = 0;
                    break;
                }

                // Connect
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                inet_pton(AF_INET, ip, &addr.sin_addr);
                addr.sin_port = htons(atoi(port));
                printf("[handle_scheduler] conectando el socket %d a %s:%s\n", agent_fd, ip, port);
                if (connect(agent_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1 && errno != EINPROGRESS) {
                    printf("[handle_scheduler] error (connect) estableciendo conexion con el agente %s:%s.\n", ip, port);
                    
                    close(agent_fd);
                    send_job_denied(job_id, 1, info);
                    nreqs = 0;
                    break;
                }

                // Agregamos el fd a la tabla y obtenemos el id (fd+reuse)
                agent_id = fd_table_add(agent_fd, FD_AGENT);
                if (agent_id == UINT64_MAX) {
                    printf("[handle_scheduler] error (fd_table_add) estableciendo conexion con el agente %s:%s.\n", ip, port);
                    
                    close(agent_fd);
                    send_job_denied(job_id, 1, info);
                    nreqs = 0;
                    break;
                }

                // Agregamos el fd a epoll
                if (epoll_add(agent_fd, EPOLLIN | EPOLLET, agent_id) == -1) {
                    printf("[handle_scheduler] error (epoll_add) estableciendo conexion con el agente %s:%s.\n", ip, port);

                    fd_table_undo_add(agent_fd);
                    close(agent_fd);
                    send_job_denied(job_id, 1, info);
                    nreqs = 0;
                    break;
                }

                // Guardamos el id en la tabla de nodos
                agent_table_set_id(ip, port, agent_id);
            }

            // Mandamos "RESERVE <job_id> <res> <amount>"
            agent_info = fd_table_get_and_inc(agent_id);
            if (agent_info != NULL) {
                printf("[handle_scheduler] mandando reserve a %d\n", agent_info->fd);
                len = sprintf(request, "RESERVE %s %s %s\n", job_id, res, amount);
                if (send_msg(agent_id, agent_info, request, len) == -1)  {
                    printf("[handle_scheduler] error (send_msg)\n");

                    close_agent_conn(agent_id, agent_info);
                }
                fd_table_dec_and_release(agent_info);
            }

            // Agregamos la request (ip:port:res:amount) al arreglo para
            // luego agregarlo a la job_table 
            strncpy(reqs[nreqs].dest_ip, ip, INET_ADDRSTRLEN - 1);
            reqs[nreqs].dest_ip[INET_ADDRSTRLEN - 1] = '\0';
            strncpy(reqs[nreqs].dest_port, port, PORTSTRLEN - 1);
            reqs[nreqs].dest_port[PORTSTRLEN - 1] = '\0';
            strncpy(reqs[nreqs].res, res, MAX_BYTES_NAME_RESOURCE - 1);
            reqs[nreqs].res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
            reqs[nreqs].amount = atoi(amount);
        }
    }
    if (nreqs != 0) {
        printf("\n");
        job_table_add(atoi(job_id), nreqs, reqs);
    }
}

/* Manda "RELEASE ..." a cada agente de 'job'. */
void release_job(job_table_t *job) {
    if (job == NULL)
        return;

    char request[TAM_BUF];
    for (int i = 0; i < job->nreqs; i++) {
        job_req_t req = job->reqs[i];
        int len = sprintf(request, "RELEASE %d %s %d\n", job->job_id, req.res, req.amount);
        uint64_t agent_id;
        if (agent_table_get_id(req.dest_ip, req.dest_port, &agent_id) == 0) {
            continue;
        }
        FdEntry *agent_info = fd_table_get_and_inc(agent_id);
        if (agent_info != NULL) {
            if (send_msg(agent_id, agent_info, request, len) == -1)
                close_agent_conn(agent_id, agent_info);
            fd_table_dec_and_release(agent_info);
        }
    }
}

/* Procesa "JOB_RELEASE <job_id>". */
static void job_release(char* job_id) {
    printf("[handle_scheduler] JOB_RELEASE %s.\n", job_id);
    job_table_t *job = job_table_extract(atoi(job_id));
    release_job(job);
    free(job);
}

/* Procesa "GET_NODES" (manda al scheduler la lista de agentes
con sus recursos). */
static void get_nodes(uint64_t id, FdEntry *info) {
    printf("[handle_scheduler] GET_NODES.\n");

    char reply[TAM_BUF];
    unsigned short len, nlen;

    // Armamos el mensaje
    char *buf = agent_table_get_nodes();
    len = sprintf(reply + NBYTES_PACKET_ERL, "%s", buf);
    nlen = htons(len);
    memcpy(reply, (char*)&nlen, NBYTES_PACKET_ERL);

    if (send_msg(id, info, reply, NBYTES_PACKET_ERL + len) == -1)
        close_scheduler_conn(info);

    printf("[handle_scheduler] tabla mandada (%s).\n", buf);
    free(buf);
}

/* Maneja la recepcion de un mensaje del scheduler, acumula
bytes hasta tener un comando completo y lo procesa. */
int handle_scheduler(uint64_t id, FdEntry *info) {
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    pthread_mutex_lock(&data->mutex_in);
    // Leemos el socket hasta EAGAIN y procesamos los mensajes leidos
    while (1) {
        int n = read(info->fd,
                     data->buf_in + data->len_buf_in,
                     TAM_BUF - data->len_buf_in);

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        if (n == -1 || n == 0) {
            printf("[handle_scheduler] scheduler closed connection or an error occurred.\n");
            pthread_mutex_unlock(&data->mutex_in);
            close_scheduler_conn(info);
            return -1;
        }

        data->len_buf_in += n;

        char *read_ptr = data->buf_in;

        while (1) {
            // Calculamos la cantidad de bytes que quedan en el buffer
            int remaining = data->len_buf_in - (read_ptr - data->buf_in);

            // Verificamos si tenemos un comando completo
            if (remaining < NBYTES_PACKET_ERL)
                break;
            uint16_t len_msg;
            memcpy(&len_msg, read_ptr, NBYTES_PACKET_ERL);
            len_msg = ntohs(len_msg);
            if (remaining < NBYTES_PACKET_ERL + len_msg)
                break;

            // Obtenemos el comando completo, copiandolo en un buffer temporal
            char command[TAM_BUF];
            memcpy(command, read_ptr + NBYTES_PACKET_ERL, len_msg);
            command[len_msg] = '\0';

            char *space = " ";
            char *saveptr1;
            char *command_name = strtok_r(command, space, &saveptr1);

            if (command_name != NULL && strncmp(command_name, "JOB_REQUEST", strlen("JOB_REQUEST")) == 0) {
                char *job_id = strtok_r(NULL, space, &saveptr1);
                char *reqs_str = strtok_r(NULL, "", &saveptr1); // resto de la linea
                job_request(info, job_id, reqs_str);
            }
            else if (command_name != NULL && strncmp(command_name, "JOB_RELEASE", strlen("JOB_RELEASE")) == 0) {
                char *job_id = strtok_r(NULL, space, &saveptr1);
                job_release(job_id);
            }
            else if (command_name != NULL && strncmp(command_name, "GET_NODES", strlen("GET_NODES")) == 0) {
                get_nodes(id, info);
            }
            else {
                printf("[handle_scheduer] invalid request.\n");
            }

            // Avanzamos el puntero de lectura
            read_ptr += NBYTES_PACKET_ERL + len_msg;
        }

        // Reiniciamos el buffer
        int bytes_procesados = read_ptr - data->buf_in;

        if (bytes_procesados >= data->len_buf_in) {
            data->len_buf_in = 0;
            data->buf_in[0] = '\0';
        } 
        else if (bytes_procesados > 0) {
            memmove(data->buf_in, read_ptr, data->len_buf_in - bytes_procesados);
            data->len_buf_in -= bytes_procesados;
            data->buf_in[data->len_buf_in] = '\0';
        }
    }
    pthread_mutex_unlock(&data->mutex_in);

    return 0;
}
