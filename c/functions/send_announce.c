#include <string.h>
#include <stdio.h>
#include "../consts.h"
#include "functions.h"

/* Envia el anuncio */ 
void send_announce() {
    char buf[TAM_BUF];
    char buf_resources[(MAX_BYTES_NAME_RESOURCE + 1 + 4) * MAX_RESOURCES_AGENT];
    
    local_resources_to_str(buf_resources);
    
    snprintf(buf, sizeof(buf), "ANNOUNCE %d %s", puerto_tcp, buf_resources);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PUERTO_UDP);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    ssize_t bytes_sent = sendto(udp_sock, buf, strlen(buf), 0, (struct sockaddr*)&dest, sizeof(dest));
    if (bytes_sent == -1) {
        perror("Error en sendto UDP (Broadcast)");
    }
}