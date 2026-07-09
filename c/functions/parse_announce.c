#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../consts.h"
#include "../structures/table_reservation.h"
#include "functions.h"

/* Parsea el anuncio "ANNOUNCE <puerto> <recursos>"
y copia los datos en los parametros pasados. */
int parse_announce(char *msg, char *port, Resource *resources, int *res_count) {
    char* tokens[2 + MAX_RESOURCES_AGENT];
    int count_tokens = tokenize_str(msg, " ", 2 + MAX_RESOURCES_AGENT, tokens);

    /* Parseamos el comando */
    if (count_tokens <= 2 || strcmp(tokens[0], "ANNOUNCE") != 0)
        return -1;
    
    snprintf(port, PORTSTRLEN, "%s", tokens[1]);
    
    /* Copiamos los recursos*/
    *res_count = count_tokens - 2;
    for(int i = 0; i < *res_count; i++) {
        char* subtokens[2]; // <res>:<amount>
        tokenize_str(tokens[2+i], ":", 2, subtokens);
        
        /* Copiamos el nombre del recurso usando snprintf */
        snprintf(resources[i].name, MAX_BYTES_NAME_RESOURCE, "%s", subtokens[0]);
        
        resources[i].total_capacity = atoi(subtokens[1]);
        resources[i].available = atoi(subtokens[1]);
    }
    return 0;
}