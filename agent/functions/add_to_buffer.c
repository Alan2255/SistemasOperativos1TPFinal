#include "../consts.h"
#include "../structures/fdinfo.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>


/* Anade el string al buffer 'buf_out' de 'info'. */
int add_to_buffer(FdInfo* info, char* msg, int len) {
    fd_tcp_data* data = info->data;

    if (len < TAM_BUF - data->len_buf_out) {
        printf("buf_out sin espacio.\n");
        return -1;
    }
    else {
        memcpy(data->buf_out + data->len_buf_out, msg, len);
        data->len_buf_out += len;            
    }

    return 0;
}