#include <string.h>
#include <stdio.h>
#include "../consts.h"
#include "../structures/fdinfo.h"
#include "functions.h"

/* Envia el anuncio */ 
void send_announce() {
    char buf[TAM_BUF];
    char buf_resources[(MAX_BYTES_NAME_RESOURCE+1+4)*MAX_RESOURCES_NODE];
    local_resources_to_str(buf_resources);
    sprintf(buf, "ANNOUNCE %d %s", puerto_tcp, buf_resources);

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PUERTO_UDP);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(sockudp, buf, strlen(buf), 0, (struct sockaddr*)&dest, sizeof(dest));
}