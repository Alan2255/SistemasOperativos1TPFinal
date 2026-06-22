#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include "functions.h"
#include "../consts.h"
#include "../structures/fdinfo.h"

/* Manda un mensaje un socket tcp, en caso de error retorna -1
    si se mando todo retorna 0, si no, guarda lo que queda en 'info',
    setea 'EPOLLOUT' en 'epollfd' y retorna 1*/
int send_msg_tcp(int fd, char *msg, int len_msg, FdInfo* info) {
    int n = write(fd, msg, len_msg);
    if (n < 0)
        return -1;

    // Si se mando todo el mensaje:
    if (n == len_msg){ 
        return 0;
    }

    // Si no, guardamos lo que no se mando
    fd_tcp_data* data = (fd_tcp_data*)(info->data);
    data->len_buf_out = len_msg - n;
    memcpy(data->buf_out, msg+n, data->len_buf_out);
    
    // Seteamos EPOLLOUT asi cuando listo mandar lo demas
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLIN | EPOLLHUP | EPOLLERR;
    ev.data.ptr = info;
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) == -1)
        return -1;

    return 1;
}