#include <string.h>
#include <stdlib.h>

#include "tokenize_str.h"
#include "../structures/table_agent.h"
#include "../agent.h"

/* Parsea el anuncio "ANNOUNCE <puerto> <recursos>" (con hasta 3 recursos)
y copia los datos en los parametros pasados. */
int parse_announce(char *msg, char *port, Resource *resources, int *res_count) {
    char* tokens[5];
    int count_tokens = tokenize_str(msg, " ", 5, tokens);

    /* Parseamos el comando */
    if (count_tokens <= 2 || strcmp(tokens[0], "ANNOUNCE") != 0)
        return -1;
    
    /* Copiamos el puerto */
    strncpy(port, tokens[1], PORTSTRLEN);
    
    /* Copiamos los recursos*/
    *res_count = count_tokens-2;
    for(int i = 0; i < *res_count; i++) {
        char* subtokens[2]; // <res>:<amount>
        tokenize_str(tokens[2+i], ":", 2, subtokens);
        strncpy(resources[i].name, subtokens[0], MAX_BYTES_NAME_RESOURCE-1);
        resources[i].available = atoi(subtokens[1]);
    }
    return 0;
}