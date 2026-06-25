#include "../consts.h"
#include "../structures/fdinfo.h"
#include <pthread.h>
#include <string.h>


/* Anade el string al buffer 'buf_out' de 'info', usando el mutex de 'info'. */
int add_to_buffer(FdInfo* info, char* src, int len) {
    fd_tcp_data* data = (fd_tcp_data*)info->data;
    int available = TAM_BUF - data->len_buf_out;

    pthread_mutex_lock(&data->mutex);

    memcpy(data->buf_out + data->len_buf_out, src, len < available ? len : available);
    data->len_buf_out += len < available ? len : available;

    pthread_mutex_unlock(&data->mutex);
    
    return 1;
}