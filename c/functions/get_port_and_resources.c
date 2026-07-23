#include <stdio.h>
#include <stdlib.h>
#include "functions.h"

/* Obtiene el puerto, el tiempo entre request, y los recursos (los guarda en los parametros) desde la linea de comandos. 
En caso de error por formato, imprime indicando el formato y devuelve -1. */
int get_port_and_resources(int argc, char **argv, int *num_resources, char **resource_names, int *capacities) {
    if (argv == NULL || num_resources == NULL || resource_names == NULL || capacities == NULL)
        return -1;

    if (argc < 6) {
        printf("Uso: <time_per_request> <port> <n> <name_1> ... <name_n> <amount_1> ... <amount_n>\n");
        return -1;
    }

    time_per_request = atoi(argv[1]);
    if (time_per_request < 0)  {
        printf("Error: el tiempo entre request no puede ser negativo\n");
        return -1;
    }

    puerto_tcp = atoi(argv[2]);
    if (puerto_tcp < 7000 || puerto_tcp > 15000)  {
        printf("Error: el puerto debe estar entre 7000 y 15000 (incluidos)\n");
        return -1;
    }

    *num_resources = atoi(argv[3]);
    if ( MAX_RESOURCES_AGENT < *num_resources) {
        printf("Error: la maxima cantidad de recursos (n) es %d.\n", MAX_RESOURCES_AGENT);
        return -1;
    }

    if (argc != 4 + (*num_resources * 2)) {
        printf("Error: cantidad incorrecta de argumentos \n");
        printf("Uso: <time_per_request> <port> <n> <name_1> ... <name_n> <amount_1> ... <amount_n>\n");
        return -1;
    }

    for (int i = 0; i < *num_resources; i++) {
        resource_names[i] = argv[4 + i];
        capacities[i] = atoi(argv[4 + *num_resources + i]);
    }

    return 0;
}