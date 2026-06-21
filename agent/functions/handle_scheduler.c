/*
    JOB_REQUEST <job_id> [ @host:res:amount ... ]
    JOB_RELEASE <job_id>
    JOB_STATUS <job_id>
*/

void handle_scheduler(conn_data *event_data) {
    int fd = event_data->fd;
    char *buf = event_data->buf;
    int len_buf = event_data->len_buf;
    int read; // cantidad de caracteres leidos (con la funcion read)

    /* Obtenemos la longitud del mensaje. */
    uint16_t len_msg;
    if (len_buf < NBYTES_PACKET_ERL) { // si se recibio una cantidad parcial de bytes de la longitud
        read = read(fd, buf+len_buf, NBYTES_PACKET_ERL-len_buf);
        if (read < 0)
            quit("read");

        event_data->len_buf += read;
        if (read < NBYTES_PACKET_ERL-len_buf)
            return;
    }
    memcpy(&len_msg, buf, NBYTES_PACKET_ERL);
    len_msg = ntohs(len_msg);    
    
    /* Leemos el mensaje. */
    int read_msg = len_buf-NBYTES_PACKET_ERL; // cantidad de caracteres ya leidos del mensaje
    read = read(fd, buf+len_buf, len_msg-read_msg);
    if (read < 0)
        quit("read");

    event_data->len_buf += read;
    if (read < len_msg-read_msg)
        return
    
    /* Parseamos el pedido */
    char *delim1 = " ", *delim2=":";
    char *saveptr1; *saveptr2
    char *job_id;
    char *command = strtok_r(buf, delim, &saveptr1); // command: JOB_REQUEST, JOB_RELEASE o JOB_STATUS
    if (strncmp(command, "JOB_REQUEST", strlen("JOB_REQUEST")) == 0) {

        job_id = strtok_r(NULL, delim, &saveptr1);

        // Parseamos '[ @host:res:amount ... ]' y enviamos "RESERVE ..."
        int reserve_count = 0;
        for (char *token = strtok_r(NULL, delim1, &saveptr1);
            token != NULL;
            reserve_count++, token = strtok_r(NULL, delim1, &saveptr1) ) {

            char *host = strtok_r(token, delim2, &saveptr2);
            char *res = strtok_r(NULL, delim2, &saveptr2);
            char *amount = strtok_r(NULL, delim2, &saveptr2);
            // int sock = table_nodes(host, PUERTO_TCP);
            char request[TAM_BUF];
            sprintf(request, "RESERVE %s %s %s\n", job_id, res, amount);
            write(sock, request, strlen(request)); // si se conecta desde aca, luego se tiene que agregar a epoll 
        }
        

        }
    }
    else if (strncmp(command, "JOB_RELEASE", strlen("JOB_RELEASE")) == 0) {
        job_id = strtok(NULL, delim); // <job_id>
    } 
    else if (strncmp(command, "JOB_STATUS", strlen("JOB_STATUS")) == 0) {
        job_id = strtok(NULL, delim); // <job_id>
    }
    else 
        quit("handle_scheduler_request: invalid request");
}



