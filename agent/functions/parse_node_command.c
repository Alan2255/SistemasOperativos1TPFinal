#include <string.h>
#include <stdlib.h>
#include "../agent.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Parsea un comando enviado por otro nodo */
int parse_node_command(char *str, char *command_name, int *job_id, char *res, int* amount) {
    char* tokens[4];
    int count_tokens = tokenize_str(str, " ", 4, tokens);

    // Verificamos formato
    if (count_tokens < 2)
        return -1;

    // Copiamos el nombre del comando y job id
    strncpy(command_name, tokens[0], MAX_LEN_COMMAND_AGENT - 1);
    command_name[MAX_LEN_COMMAND_AGENT - 1] = '\0';
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