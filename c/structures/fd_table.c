#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "fd_table.h"
#include "../functions/functions.h"

Hash *fd_table = NULL;

static void fd_key(int fd, char *out, size_t max_len) {
    snprintf(out, max_len, "%d", fd);
}

// Libera la entrada (pone el fd en -1 y libera la memoria de data)
void release_entry(FdEntry *entry) {
    if (!entry || !entry->data)
        return;

    entry->fd = -1;

    if (entry->type == FD_SCHEDULER || entry->type == FD_AGENT) {
        pthread_mutex_destroy(&((fd_tcp_data*)entry->data)->mutex_in);
        pthread_mutex_destroy(&((fd_tcp_data*)entry->data)->mutex_out);
        pthread_mutex_destroy(&((fd_tcp_data*)entry->data)->mutex_state);
    }

    free(entry->data);
    entry->data = NULL;
}

/* Inicializa la tabla. */
void fd_table_init() {
    if (fd_table == NULL) {
        fd_table = hash_create();
    }
}

/* Destruye la tabla. */
void fd_table_destroy() {
    if (!fd_table) return;

    for (int i = 0; i < fd_table->used; i++) {
        FdEntry *entry = (FdEntry*)fd_table->entries[i].value;
        if (entry == NULL) continue;

        release_entry(entry);
        pthread_mutex_destroy(&entry->mutex);
        free(entry);
    }

    hash_destroy(fd_table);
    fd_table = NULL;
}

/* Registra fd en la tabla. Si es la primera vez que se registra,
crea la entrada con 'reuse' en 0; si ya existia, se usa esa entrada y
se incrementa 'reuse'.
Retorna el identificador de la entrada, o UINT64_MAX en caso de error. */
uint64_t fd_table_add(int fd, fdtype type) {
    if (!fd_table) return UINT64_MAX;

    char key[16];
    fd_key(fd, key, sizeof(key));

    FdEntry *entry = (FdEntry*)hash_get(fd_table, key);
    int is_new = entry == NULL;

    if (is_new) {
        entry = malloc(sizeof(FdEntry));
        if (entry == NULL)
            return UINT64_MAX;

        entry->reuse = 0;
        entry->fd = -1;
        entry->data = NULL;
        pthread_mutex_init(&entry->mutex, NULL);

        if (!hash_set(fd_table, key, entry)) {
            pthread_mutex_destroy(&entry->mutex);
            free(entry);
            return UINT64_MAX;
        }
    }

    void *data;
    if (type == FD_SCHEDULER || type == FD_AGENT) {
        fd_tcp_data *tcp_data = malloc(sizeof(fd_tcp_data));
        if (tcp_data == NULL)
            return UINT64_MAX;

        tcp_data->len_buf_in = 0;
        tcp_data->len_buf_out = 0;
        pthread_mutex_init(&tcp_data->mutex_in, NULL);
        pthread_mutex_init(&tcp_data->mutex_out, NULL);

        tcp_data->ref_count = 0;
        tcp_data->close = 0;
        pthread_mutex_init(&tcp_data->mutex_state, NULL);

        data = tcp_data;
    }
    else if (type == FD_NODE_TIMER) {
        data = malloc(sizeof(fd_node_timer_data));
        if (data == NULL)
            return UINT64_MAX;
    }
    else {
        data = NULL;
    }

    pthread_mutex_lock(&entry->mutex);
    if (!is_new)
        entry->reuse++;
    entry->fd = fd;
    entry->type = type;
    entry->data = data;
    uint64_t id = ((uint64_t)(uint32_t)fd << 32) | (uint32_t)entry->reuse; // NO ES SUFICIENTE Y MAS CLARO CASTEAR fd y resuse A (uint64_t)?
    pthread_mutex_unlock(&entry->mutex);

    return id;
}

/* Opuesta de fd_table_add, reinicia la entrada de la tabla correspondiente 
al fd  (fd = -1, libera 'data'), no elimina la entrada de la tabla ni cierra el fd. */
void fd_table_undo_add(int fd) {
    if (!fd_table) return;

    char key[16];
    fd_key(fd, key, sizeof(key));

    FdEntry *entry = (FdEntry*)hash_get(fd_table, key);
    if (entry == NULL)
        return;

    pthread_mutex_lock(&entry->mutex);
    if (entry->fd == fd) {
        release_entry(entry);
    }
    pthread_mutex_unlock(&entry->mutex);
}

/* Recibe el identificador (fd + reuse count), busca la entrada por el fd,
si la entrada tiene fd en -1 o su contador 'reuse' no coincide con el del 
identificador, retorna NULL. Si la entrada es de tipo FD_SCHEDULER o FD_AGENT
y no fue marcada para cerrar, le suma 1 a su ref_count y retorna la entrada. */
FdEntry* fd_table_get_and_inc(uint64_t id) {
    if (!fd_table) 
        return NULL;

    int fd = (int)(uint32_t)(id >> 32);
    unsigned int reuse = (unsigned int)(id & 0xFFFFFFFFu);

    char key[16];
    fd_key(fd, key, sizeof(key));

    FdEntry *entry = (FdEntry*)hash_get(fd_table, key);
    if (!entry)
        return NULL;

    pthread_mutex_lock(&entry->mutex);

    if (entry->fd == -1 || entry->reuse != reuse) {
        // fd cerrado, o evento viejo de una conexion anterior con el mismo fd
        pthread_mutex_unlock(&entry->mutex);
        return NULL;
    }

    if (entry->type == FD_SCHEDULER || entry->type == FD_AGENT) {
        fd_tcp_data *data = (fd_tcp_data*)entry->data;
        pthread_mutex_lock(&data->mutex_state);
        if (data->close) {
            // Ya fue pedido el cierre: no lo entregamos a otro handler
            pthread_mutex_unlock(&data->mutex_state);
            pthread_mutex_unlock(&entry->mutex);
            return NULL;
        }
        data->ref_count++;
        pthread_mutex_unlock(&data->mutex_state);
    }

    pthread_mutex_unlock(&entry->mutex);
    return entry;
}

/* Opuesta de fd_table_get_and_inc, resta 1 al ref_count. Si llega a 0 y la entrada
estaba marcada para cerrar, lo saca de epoll, cierra el fd y libera la entrada. */
void fd_table_dec_and_release(FdEntry *entry) {
    if (!entry || !(entry->type == FD_SCHEDULER || entry->type == FD_AGENT))
        return;

    fd_tcp_data *data = (fd_tcp_data*)entry->data;

    pthread_mutex_lock(&entry->mutex);

    pthread_mutex_lock(&data->mutex_state);
    data->ref_count--;
    int must_close = (data->ref_count <= 0 && data->close);
    pthread_mutex_unlock(&data->mutex_state);

    if (must_close) {
        int fd = entry->fd;
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
        release_entry(entry);
        close(fd);
    }

    pthread_mutex_unlock(&entry->mutex);
}

/* Pide cerrar el fd. Si nadie lo esta usando (ref_count == 0, o el tipo
no tiene ref-count) lo cierra inmediatamente; si no, queda marcado y lo
cierra la ultima llamada a fd_table_dec_and_release. */
void fd_table_request_close(FdEntry *entry) {
    if (!entry)
        return;

    pthread_mutex_lock(&entry->mutex);

    if (entry->fd == -1) {
        // Ya lo cerro otro thread
        pthread_mutex_unlock(&entry->mutex);
        return;
    }

    int must_close = 0;

    if (entry->type == FD_SCHEDULER || entry->type == FD_AGENT) {
        fd_tcp_data *data = (fd_tcp_data*)entry->data;
        pthread_mutex_lock(&data->mutex_state);
        data->close = 1;
        must_close = data->ref_count <= 0;
        pthread_mutex_unlock(&data->mutex_state);
    }

    if (must_close || !(entry->type == FD_SCHEDULER || entry->type == FD_AGENT)) {
        int fd = entry->fd;
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
        release_entry(entry);
        close(fd);
    }

    pthread_mutex_unlock(&entry->mutex);
}