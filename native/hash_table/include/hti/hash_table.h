#ifndef HTI_HASH_TABLE_H
#define HTI_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ht ht_t;

typedef enum {
    HT_OK = 0,
    HT_NOT_FOUND,
    HT_INVALID_ARGUMENT,
    HT_NO_MEMORY,
    HT_OVERFLOW
} ht_status_t;

typedef enum {
    HT_PUT_INSERTED = 1,
    HT_PUT_UPDATED = 2
} ht_put_result_t;

typedef void *(*ht_allocate_fn)(void *context, size_t size);
typedef void (*ht_deallocate_fn)(void *context, void *memory);

typedef struct {
    void *context;
    ht_allocate_fn allocate;
    ht_deallocate_fn deallocate;
} ht_allocator_t;

typedef struct {
    uint32_t struct_size;
    size_t initial_capacity;
    uint64_t hash_seed;
    ht_allocator_t allocator;
} ht_config_t;

ht_status_t ht_create(const ht_config_t *config, ht_t **out_table);
void ht_destroy(ht_t *table);

ht_status_t ht_put(
    ht_t *table,
    const void *key,
    size_t key_size,
    int64_t value,
    ht_put_result_t *out_result);

ht_status_t ht_get(
    const ht_t *table,
    const void *key,
    size_t key_size,
    int64_t *out_value);

ht_status_t ht_remove(
    ht_t *table,
    const void *key,
    size_t key_size,
    int64_t *out_previous_value);

ht_status_t ht_reserve(ht_t *table, size_t requested_capacity);

size_t ht_size(const ht_t *table);
size_t ht_capacity(const ht_t *table);
size_t ht_resize_threshold(const ht_t *table);
const char *ht_status_name(ht_status_t status);

#ifdef __cplusplus
}
#endif

#endif
