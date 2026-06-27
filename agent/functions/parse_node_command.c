#include <string.h>
#include <stdlib.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Parsea un comando enviado por otro nodo */
int parse_node_command(char *str, char *command_name, int *job_id, char *res, int* amount) {
    char* tokens[4];
    int count_tokens = tokenize_str(str, " ", 4, tokens);

    // Verificamos formato
    if (count_tokens < 2)
        return -1;

    // Nombre del comando
    snprintf(command_name, MAX_LEN_COMMAND_AGENT, "%s", tokens[0]);
    printf("COMMAND_NAME PARSER BEFORE1 [%s]\n",command_name); 

    // Job id
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
        
        printf("COMMAND_NAME PARSER BEFORE2 [%s]\n",command_name); 
        snprintf(res, MAX_BYTES_NAME_RESOURCE, "%s", tokens[2]);
        printf("COMMAND_NAME PARSER AFTER [%s]\n",command_name); 
        *amount = atoi(tokens[3]);
    }
    else 
        return -1;

    return 0;
}