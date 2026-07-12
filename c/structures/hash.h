#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#define INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.75

// Celda
typedef struct {
    unsigned long hash;
    char *key;
    void *value;
} Entry;

// Tabla hash
typedef struct {
    int *indices;
    Entry *entries;
    int capacity;
    int used;
    pthread_mutex_t mutex;
} Hash;

// --- API de tabla hash ---

// Crea la tabla
Hash* hash_create();

// Destruye la tabla
void hash_destroy(Hash *dict, void (*free_value)(void*));

// Agrega o modifica un elemento
void* hash_set(Hash *dict, const char *key, void *value);

// Busca el elemento mediante su key y lo devuelve si existe
void* hash_get(Hash *table, const char *key, size_t element_size);

// Borra un elemento de la tabla
bool hash_remove(Hash *table, const char *key, void (*free_value)(void*));

#endif