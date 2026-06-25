#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "functions.h"
#include "../consts.h"
#include "../structures/fdinfo.h"

/* Manda un mensaje un socket tcp, si se mando todo retorna 0, si no,
guarda lo que queda en su buffer de 'info', y retorna 1. En caso de error retorna -1. */
int send_msg_tcp(int fd, char *msg, int len_msg, FdInfo* info) {
    if (info == NULL)
        return -1;

    fd = info->fd;
    fd_tcp_data* data = (fd_tcp_data*)(info->data);

    if (data->len_buf_out != 0) { 
        int available = TAM_BUF - data->len_buf_out;
        memcpy(data->buf_out + data->len_buf_out, msg,
                len_msg < available ? len_msg : available);
    }
    else {
        int n = write(fd, msg, len_msg);
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {// No se mando nada
            memcpy(data->buf_out, msg, len_msg);
            data->len_buf_out = 0;
        }
        else if (n == -1) // Ocurrio otro error
            return -1;
        else {
            data->len_buf_out = len_msg - n;
            memcpy(data->buf_out, msg+n, len_msg - n);
        }
    }
    return 1;
}