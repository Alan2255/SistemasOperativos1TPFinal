#include <string.h>
#include <stdio.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Envia el anuncio */ 
void send_announce() {
    char buf[TAM_BUF];
    char buf_resources[(MAX_BYTES_NAME_RESOURCE + 1 + 4) * MAX_RESOURCES_NODE];
    
    local_resources_to_str(buf_resources);
    
    /* 1. CORRECCIÓN: snprintf evita desbordar buf si buf_resources es muy grande */
    int bytes_written = snprintf(buf, sizeof(buf), "ANNOUNCE %d %s", puerto_tcp + 1, buf_resources);
    if (bytes_written >= (int)sizeof(buf)) {
        fprintf(stderr, "Advertencia: El anuncio fue truncado por falta de espacio en TAM_BUF\n");
    }

    struct sockaddr_in dest;
    /* 2. CORRECCIÓN CRÍTICA: Limpiamos la estructura para eliminar basura de la memoria */
    memset(&dest, 0, sizeof(dest));
    
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PUERTO_UDP);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255

    /* 3. Validación de envío */
    ssize_t bytes_sent = sendto(sockudp, buf, strlen(buf), 0, (struct sockaddr*)&dest, sizeof(dest));
    if (bytes_sent == -1) {
        perror("Error en sendto UDP (Broadcast)");
    }
}