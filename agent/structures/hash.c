#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

// Función Hash
static unsigned long fun_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Crea la tabla
Hash* hash_create(void) {
    Hash *table = malloc(sizeof(Hash));
    if (!table) return NULL;

    table->capacity = INITIAL_CAPACITY;
    table->used = 0;

    // Inicializar la tabla hash de índices con -1
    table->indices = malloc(table->capacity * sizeof(int));
    if (!table->indices) {
        free(table);
        return NULL;
    }

    for (int i = 0; i < table->capacity; i++) {
        table->indices[i] = -1;
    }

    // Inicializar el array compacto de entradas
    table->entries = malloc(table->capacity * sizeof(Entry));
    if (!table->entries) {
        free(table->indices);
        free(table);
        return NULL;
    }

    if (pthread_mutex_init(&table->mutex, NULL) != 0) {
        free(table->entries);
        free(table->indices);
        free(table);
        return NULL;
    }

    return table;
}

// Rehash al doble de la capacidad
static void hash_resize(Hash *table) {
    int old_capacity = table->capacity;
    int new_capacity = old_capacity * 2;

    int *new_indices = malloc(new_capacity * sizeof(int));
    if (!new_indices) return; 

    for (int i = 0; i < new_capacity; i++) {
        new_indices[i] = -1;
    }

    // Reacomodamos los índices apuntando
    for (int i = 0; i < table->used; i++) {
        // Ignoramos elementos borrados si los hubiera
        if (table->entries[i].key == NULL) continue;

        unsigned long h = table->entries[i].hash;
        size_t idx = h & (new_capacity - 1);

        while (new_indices[idx] != -1) {
            idx = (idx + 1) & (new_capacity - 1);
        }
        new_indices[idx] = i;
    }

    free(table->indices);
    table->indices = new_indices;
    table->capacity = new_capacity;
    
    Entry *new_entries = realloc(table->entries, table->capacity * sizeof(Entry));
    if (new_entries) {
        table->entries = new_entries;
    }
}

// Agrega o modifica un elemento
bool hash_set(Hash *table, const char *key, void *value) {
    if (!table || !key) return false;

    pthread_mutex_lock(&table->mutex);

    unsigned long h = fun_hash(key);
    size_t idx = h & (table->capacity - 1);

    // Buscar si la clave ya existe en los índices
    while (table->indices[idx] != -1) {
        int entry_idx = table->indices[idx];
        if (table->entries[entry_idx].key != NULL && strcmp(table->entries[entry_idx].key, key) == 0) {
            // Si ya existe, sobreescribimos el valor
            table->entries[entry_idx].value = value;
            pthread_mutex_unlock(&table->mutex);
            return true;
        }
        idx = (idx + 1) & (table->capacity - 1);
    }

    // Si es una clave nueva, validamos el Factor de Carga (75%)
    if ((float)(table->used + 1) / table->capacity >= LOAD_FACTOR) {
        hash_resize(table);
        // Recalcular índice tras el cambio de tamaño de la tabla
        idx = h & (table->capacity - 1);
        while (table->indices[idx] != -1) {
            idx = (idx + 1) & (table->capacity - 1);
        }
    }

    // Guardar los datos
    int new_entry_idx = table->used;
    table->entries[new_entry_idx].hash = h;
    table->entries[new_entry_idx].key = strdup(key);
    table->entries[new_entry_idx].value = value;

    // Vincular el índice de la tabla hash con el array compacto
    table->indices[idx] = new_entry_idx;
    table->used++;

    pthread_mutex_unlock(&table->mutex);
    return true;
}

void* hash_get(Hash *table, const char *key) {
    if (!table || !key) return NULL;

    pthread_mutex_lock(&table->mutex);

    unsigned long h = fun_hash(key);
    size_t idx = h & (table->capacity - 1);

    while (table->indices[idx] != -1) {
        int entry_idx = table->indices[idx];
        if (table->entries[entry_idx].key != NULL && strcmp(table->entries[entry_idx].key, key) == 0) {
            void *value = table->entries[entry_idx].value;
            pthread_mutex_unlock(&table->mutex);
            return value;
        }
        idx = (idx + 1) & (table->capacity - 1);
    }

    pthread_mutex_unlock(&table->mutex);
    return NULL;
}

// Borra un elemento de la tabla
bool hash_remove(Hash *table, const char *key) {
    if (!table || !key) return false;

    pthread_mutex_lock(&table->mutex);

    unsigned long h = fun_hash(key);
    size_t idx = h & (table->capacity - 1);

    // Buscar el elemento a eliminar
    while (table->indices[idx] != -1) {
        int entry_idx = table->indices[idx];
        if (table->entries[entry_idx].key != NULL && strcmp(table->entries[entry_idx].key, key) == 0) {
            
            // Liberamos la clave que duplicó el hash_set
            free(table->entries[entry_idx].key);
            table->entries[entry_idx].key = NULL;
            table->entries[entry_idx].value = NULL;

            // Quitamos el índice actual
            table->indices[idx] = -1;

            // Rehash de la vecindad
            size_t vacio = idx;
            size_t siguiente = (idx + 1) & (table->capacity - 1);

            while (table->indices[siguiente] != -1) {
                int e_idx = table->indices[siguiente];
                unsigned long hash_sig = table->entries[e_idx].hash;
                size_t posicion_natural = hash_sig & (table->capacity - 1);

                // Evaluamos si el elemento saltó el hueco o quedó desubicado
                // usando aritmética modular circular
                bool debe_moverse = false;
                if (vacio <= siguiente) {
                    if (posicion_natural <= vacio || posicion_natural > siguiente) debe_moverse = true;
                } else {
                    if (posicion_natural <= vacio && posicion_natural > siguiente) debe_moverse = true;
                }

                if (debe_moverse) {
                    table->indices[vacio] = e_idx;
                    table->indices[siguiente] = -1;
                    vacio = siguiente;
                }
                siguiente = (siguiente + 1) & (table->capacity - 1);
            }

            pthread_mutex_unlock(&table->mutex);
            return true;
        }
        idx = (idx + 1) & (table->capacity - 1);
    }

    pthread_mutex_unlock(&table->mutex);
    return false;
}

// Destruir la tabla
void hash_destroy(Hash *table) {
    if (!table) return;

    for (int i = 0; i < table->used; i++) {
        if (table->entries[i].key != NULL) {
            free(table->entries[i].key);
        }
    }
    free(table->entries);
    free(table->indices);
    pthread_mutex_destroy(&table->mutex);
    free(table);
}
