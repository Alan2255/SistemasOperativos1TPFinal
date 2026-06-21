/*
RESERVE <job_id> <resource_name> <amount>
RELEASE <job_id> <resource_name> <amount>
GRANTED <job_id>
DENIED <job_id>
*/
#define MAX_STRLEN_COMMAND_NAME_NODE 7// maxima longitud del nombre de un comando entre nodos (como RELEASE O GRANTED)


int parse_node_command(char *str, char *command_name, int *job_id, char *res, int* amount) {
    char* tokens[4];
    int count_tokens = tokenize_str(str, " ", 4, tokens);

    // Verificamos formato
    if (count_tokens < 2)
        return -1;

    // Copiamos el nombre del comando y job id
    strncpy(command_name, tokens[0], MAX_STRLEN_COMMAND_NAME_NODE - 1);
    command_name[MAX_STRLEN_COMMAND_NAME_NODE - 1] = '\0';
    *job_id = atoi(tokens[1]);

    if (strcmp(command_name, "GRANTED") == 0 
        || strcmp(command_name, "DENIED") == 0) {
        // Verificamos formato
        if (count_tokens != 2)
            return -1;
    }
    else if (strcmp(command_name, "RESERVE") == 0 
        || strcmp(command_name, "RELEASE") == 0) {
        
        // Verificamos formato
        if (count_tokens != 4)
            return -1;

        // Copiamos el recurso y monto
        strncpy(res, tokens[2], MAX_BYTES_NAME_RESOURCE - 1);
        res[MAX_BYTES_NAME_RESOURCE - 1] = '\0';
        *amount = atoi(tokens[3]);
    }
    else 
        return -1;

    return 0;
}

void handle_node_disconnect() {

}

void handle_node_connect() {}

void handle_node_msg(int fd, FD_TCP_Data *data) {
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
        char res[MAX_BYTES_NAME_RESOURCE];
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