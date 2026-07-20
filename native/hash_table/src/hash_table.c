#include "hti/hash_table.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hash_table_internal.h"

#define HT_MIN_CAPACITY ((size_t)4)
#define HT_DEFAULT_CAPACITY ((size_t)8)
#define HT_DEFAULT_HASH_SEED UINT64_C(0x9e3779b97f4a7c15)

static void *ht_default_allocate(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void ht_default_deallocate(void *context, void *memory)
{
    (void)context;
    free(memory);
}

static bool ht_size_add(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || left > SIZE_MAX - right) {
        return false;
    }
    *out_value = left + right;
    return true;
}

static bool ht_size_multiply(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || (right != 0U && left > SIZE_MAX / right)) {
        return false;
    }
    *out_value = left * right;
    return true;
}

static bool ht_normalize_capacity(size_t requested, size_t *out_capacity)
{
    size_t capacity = HT_MIN_CAPACITY;

    if (out_capacity == NULL) {
        return false;
    }
    if (requested < HT_MIN_CAPACITY) {
        requested = HT_MIN_CAPACITY;
    }
    while (capacity < requested) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    *out_capacity = capacity;
    return true;
}

static size_t ht_threshold_for_capacity(size_t capacity)
{
    return capacity - (capacity / 4U);
}

static bool ht_key_arguments_valid(const void *key, size_t key_size)
{
    return key_size == 0U || key != NULL;
}

static bool ht_keys_equal(
    const ht_entry_t *entry,
    const void *key,
    size_t key_size)
{
    if (entry->key_size != key_size) {
        return false;
    }
    return key_size == 0U || memcmp(entry->key, key, key_size) == 0;
}

uint64_t ht_internal_hash_key(
    uint64_t seed,
    const void *key,
    size_t key_size)
{
    const unsigned char *bytes = key;
    uint64_t hash = UINT64_C(14695981039346656037) ^ seed;
    size_t index;

    for (index = 0U; index < key_size; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }

    hash ^= hash >> 33U;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33U;
    hash *= UINT64_C(0xc4ceb9fe1a85ec53);
    hash ^= hash >> 33U;
    return hash;
}

#if HT_ENABLE_INSPECTION
static void ht_emit_trace(const ht_t *table, const ht_trace_event_t *event)
{
    if (table->trace_sink != NULL) {
        table->trace_sink(table->trace_context, table, event);
    }
}
#else
static void ht_emit_trace(const ht_t *table, const ht_trace_event_t *event)
{
    (void)table;
    (void)event;
}
#endif

static ht_status_t ht_allocate_buckets(
    ht_t *table,
    size_t capacity,
    ht_entry_t ***out_buckets)
{
    ht_entry_t **buckets;
    size_t bytes;

    if (!ht_size_multiply(capacity, sizeof(*buckets), &bytes)) {
        return HT_OVERFLOW;
    }
    buckets = table->allocator.allocate(table->allocator.context, bytes);
    if (buckets == NULL) {
        return HT_NO_MEMORY;
    }
    memset(buckets, 0, bytes);
    *out_buckets = buckets;
    return HT_OK;
}

static ht_status_t ht_allocate_entry(
    ht_t *table,
    const void *key,
    size_t key_size,
    uint64_t hash,
    int64_t value,
    ht_entry_t **out_entry)
{
    ht_entry_t *entry;
    size_t bytes;

    if (!ht_size_add(offsetof(ht_entry_t, key), key_size, &bytes)) {
        return HT_OVERFLOW;
    }
    entry = table->allocator.allocate(table->allocator.context, bytes);
    if (entry == NULL) {
        return HT_NO_MEMORY;
    }
    entry->entry_id = table->next_entry_id;
    entry->hash = hash;
    entry->key_size = key_size;
    entry->value = value;
    entry->next = NULL;
    if (key_size != 0U) {
        memcpy(entry->key, key, key_size);
    }
    *out_entry = entry;
    return HT_OK;
}

static ht_status_t ht_rehash(ht_t *table, size_t new_capacity)
{
    ht_entry_t **new_buckets = NULL;
    ht_entry_t **old_buckets;
    ht_trace_event_t event;
    ht_status_t status;
    size_t old_capacity = table->capacity;
    size_t bucket_index;
    size_t moved_count = 0U;

    status = ht_allocate_buckets(table, new_capacity, &new_buckets);
    if (status != HT_OK) {
        event = (ht_trace_event_t){
            .kind = HT_TRACE_REHASH_FAILED,
            .status = status,
            .size_before = table->size,
            .size_after = table->size,
            .capacity_before = old_capacity,
            .capacity_after = new_capacity
        };
        ht_emit_trace(table, &event);
        return status;
    }

    event = (ht_trace_event_t){
        .kind = HT_TRACE_REHASH_BEGIN,
        .status = HT_OK,
        .size_before = table->size,
        .size_after = table->size,
        .capacity_before = old_capacity,
        .capacity_after = new_capacity
    };
    ht_emit_trace(table, &event);

    old_buckets = table->buckets;
    for (bucket_index = 0U; bucket_index < old_capacity; ++bucket_index) {
        ht_entry_t *entry = old_buckets[bucket_index];
        while (entry != NULL) {
            ht_entry_t *next = entry->next;
            size_t new_index = (size_t)(entry->hash & (uint64_t)(new_capacity - 1U));
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;
            ++moved_count;
            entry = next;
        }
    }

    table->buckets = new_buckets;
    table->capacity = new_capacity;
    table->resize_threshold = ht_threshold_for_capacity(new_capacity);
    ++table->revision;
    table->allocator.deallocate(table->allocator.context, old_buckets);

    event = (ht_trace_event_t){
        .kind = HT_TRACE_REHASH_END,
        .status = HT_OK,
        .moved_count = moved_count,
        .size_before = table->size,
        .size_after = table->size,
        .capacity_before = old_capacity,
        .capacity_after = new_capacity
    };
    ht_emit_trace(table, &event);
    return HT_OK;
}

ht_status_t ht_create(const ht_config_t *config, ht_t **out_table)
{
    ht_allocator_t allocator = {
        .context = NULL,
        .allocate = ht_default_allocate,
        .deallocate = ht_default_deallocate
    };
    size_t requested_capacity = HT_DEFAULT_CAPACITY;
    uint64_t hash_seed = HT_DEFAULT_HASH_SEED;
    size_t capacity;
    ht_t *table;
    ht_status_t status;

    if (out_table == NULL) {
        return HT_INVALID_ARGUMENT;
    }
    *out_table = NULL;

    if (config != NULL) {
        if (config->struct_size != sizeof(*config)) {
            return HT_INVALID_ARGUMENT;
        }
        if ((config->allocator.allocate == NULL) !=
            (config->allocator.deallocate == NULL)) {
            return HT_INVALID_ARGUMENT;
        }
        if (config->allocator.allocate != NULL) {
            allocator = config->allocator;
        }
        if (config->initial_capacity != 0U) {
            requested_capacity = config->initial_capacity;
        }
        hash_seed = config->hash_seed;
    }

    if (!ht_normalize_capacity(requested_capacity, &capacity)) {
        return HT_OVERFLOW;
    }
    table = allocator.allocate(allocator.context, sizeof(*table));
    if (table == NULL) {
        return HT_NO_MEMORY;
    }
    memset(table, 0, sizeof(*table));
    table->allocator = allocator;
    table->capacity = capacity;
    table->resize_threshold = ht_threshold_for_capacity(capacity);
    table->next_entry_id = UINT64_C(1);
    table->hash_seed = hash_seed;

    status = ht_allocate_buckets(table, capacity, &table->buckets);
    if (status != HT_OK) {
        allocator.deallocate(allocator.context, table);
        return status;
    }

    *out_table = table;
    return HT_OK;
}

void ht_destroy(ht_t *table)
{
    size_t bucket_index;

    if (table == NULL) {
        return;
    }
    for (bucket_index = 0U; bucket_index < table->capacity; ++bucket_index) {
        ht_entry_t *entry = table->buckets[bucket_index];
        while (entry != NULL) {
            ht_entry_t *next = entry->next;
            table->allocator.deallocate(table->allocator.context, entry);
            entry = next;
        }
    }
    table->allocator.deallocate(table->allocator.context, table->buckets);
    table->allocator.deallocate(table->allocator.context, table);
}

ht_status_t ht_put(
    ht_t *table,
    const void *key,
    size_t key_size,
    int64_t value,
    ht_put_result_t *out_result)
{
    uint64_t hash;
    size_t bucket_index;
    size_t probes = 0U;
    ht_entry_t *entry;
    ht_status_t status;
    ht_trace_event_t event;
    size_t size_before;

    if (table == NULL || out_result == NULL ||
        !ht_key_arguments_valid(key, key_size)) {
        return HT_INVALID_ARGUMENT;
    }

    hash = ht_internal_hash_key(table->hash_seed, key, key_size);
    bucket_index = (size_t)(hash & (uint64_t)(table->capacity - 1U));
    entry = table->buckets[bucket_index];
    while (entry != NULL) {
        ++probes;
        if (entry->hash == hash && ht_keys_equal(entry, key, key_size)) {
            entry->value = value;
            ++table->revision;
            *out_result = HT_PUT_UPDATED;
            event = (ht_trace_event_t){
                .kind = HT_TRACE_PUT_UPDATED,
                .status = HT_OK,
                .entry_id = entry->entry_id,
                .hash = hash,
                .bucket_index = bucket_index,
                .probe_count = probes,
                .size_before = table->size,
                .size_after = table->size,
                .capacity_before = table->capacity,
                .capacity_after = table->capacity
            };
            ht_emit_trace(table, &event);
            return HT_OK;
        }
        entry = entry->next;
    }

    if (table->size == SIZE_MAX || table->next_entry_id == UINT64_MAX) {
        return HT_OVERFLOW;
    }
    status = ht_allocate_entry(table, key, key_size, hash, value, &entry);
    if (status != HT_OK) {
        return status;
    }

    size_before = table->size;
    if (table->size + 1U > table->resize_threshold) {
        size_t new_capacity;
        if (table->capacity > SIZE_MAX / 2U) {
            table->allocator.deallocate(table->allocator.context, entry);
            return HT_OVERFLOW;
        }
        new_capacity = table->capacity * 2U;
        status = ht_rehash(table, new_capacity);
        if (status != HT_OK) {
            table->allocator.deallocate(table->allocator.context, entry);
            return status;
        }
    }

    bucket_index = (size_t)(hash & (uint64_t)(table->capacity - 1U));
    probes = 0U;
    {
        ht_entry_t *current = table->buckets[bucket_index];
        while (current != NULL) {
            ++probes;
            current = current->next;
        }
    }
    entry->next = table->buckets[bucket_index];
    table->buckets[bucket_index] = entry;
    ++table->size;
    ++table->next_entry_id;
    ++table->revision;
    *out_result = HT_PUT_INSERTED;

    event = (ht_trace_event_t){
        .kind = HT_TRACE_PUT_INSERTED,
        .status = HT_OK,
        .entry_id = entry->entry_id,
        .hash = hash,
        .bucket_index = bucket_index,
        .probe_count = probes,
        .size_before = size_before,
        .size_after = table->size,
        .capacity_before = table->capacity,
        .capacity_after = table->capacity
    };
    ht_emit_trace(table, &event);
    return HT_OK;
}

ht_status_t ht_get(
    const ht_t *table,
    const void *key,
    size_t key_size,
    int64_t *out_value)
{
    uint64_t hash;
    size_t bucket_index;
    size_t probes = 0U;
    const ht_entry_t *entry;
    ht_trace_event_t event;

    if (table == NULL || out_value == NULL ||
        !ht_key_arguments_valid(key, key_size)) {
        return HT_INVALID_ARGUMENT;
    }

    hash = ht_internal_hash_key(table->hash_seed, key, key_size);
    bucket_index = (size_t)(hash & (uint64_t)(table->capacity - 1U));
    entry = table->buckets[bucket_index];
    while (entry != NULL) {
        ++probes;
        if (entry->hash == hash && ht_keys_equal(entry, key, key_size)) {
            *out_value = entry->value;
            event = (ht_trace_event_t){
                .kind = HT_TRACE_LOOKUP_HIT,
                .status = HT_OK,
                .entry_id = entry->entry_id,
                .hash = hash,
                .bucket_index = bucket_index,
                .probe_count = probes,
                .size_before = table->size,
                .size_after = table->size,
                .capacity_before = table->capacity,
                .capacity_after = table->capacity
            };
            ht_emit_trace(table, &event);
            return HT_OK;
        }
        entry = entry->next;
    }

    event = (ht_trace_event_t){
        .kind = HT_TRACE_LOOKUP_MISS,
        .status = HT_NOT_FOUND,
        .hash = hash,
        .bucket_index = bucket_index,
        .probe_count = probes,
        .size_before = table->size,
        .size_after = table->size,
        .capacity_before = table->capacity,
        .capacity_after = table->capacity
    };
    ht_emit_trace(table, &event);
    return HT_NOT_FOUND;
}

ht_status_t ht_remove(
    ht_t *table,
    const void *key,
    size_t key_size,
    int64_t *out_previous_value)
{
    uint64_t hash;
    size_t bucket_index;
    size_t probes = 0U;
    size_t size_before;
    ht_entry_t *entry;
    ht_entry_t *previous = NULL;
    ht_trace_event_t event;

    if (table == NULL || !ht_key_arguments_valid(key, key_size)) {
        return HT_INVALID_ARGUMENT;
    }

    hash = ht_internal_hash_key(table->hash_seed, key, key_size);
    bucket_index = (size_t)(hash & (uint64_t)(table->capacity - 1U));
    entry = table->buckets[bucket_index];
    while (entry != NULL) {
        ++probes;
        if (entry->hash == hash && ht_keys_equal(entry, key, key_size)) {
            uint64_t entry_id = entry->entry_id;
            int64_t previous_value = entry->value;
            size_before = table->size;
            if (previous == NULL) {
                table->buckets[bucket_index] = entry->next;
            } else {
                previous->next = entry->next;
            }
            table->allocator.deallocate(table->allocator.context, entry);
            --table->size;
            ++table->revision;
            if (out_previous_value != NULL) {
                *out_previous_value = previous_value;
            }
            event = (ht_trace_event_t){
                .kind = HT_TRACE_DELETE_HIT,
                .status = HT_OK,
                .entry_id = entry_id,
                .hash = hash,
                .bucket_index = bucket_index,
                .probe_count = probes,
                .size_before = size_before,
                .size_after = table->size,
                .capacity_before = table->capacity,
                .capacity_after = table->capacity
            };
            ht_emit_trace(table, &event);
            return HT_OK;
        }
        previous = entry;
        entry = entry->next;
    }

    event = (ht_trace_event_t){
        .kind = HT_TRACE_DELETE_MISS,
        .status = HT_NOT_FOUND,
        .hash = hash,
        .bucket_index = bucket_index,
        .probe_count = probes,
        .size_before = table->size,
        .size_after = table->size,
        .capacity_before = table->capacity,
        .capacity_after = table->capacity
    };
    ht_emit_trace(table, &event);
    return HT_NOT_FOUND;
}

ht_status_t ht_reserve(ht_t *table, size_t requested_capacity)
{
    size_t normalized;

    if (table == NULL) {
        return HT_INVALID_ARGUMENT;
    }
    if (!ht_normalize_capacity(requested_capacity, &normalized)) {
        return HT_OVERFLOW;
    }
    if (normalized <= table->capacity) {
        return HT_OK;
    }
    return ht_rehash(table, normalized);
}

size_t ht_size(const ht_t *table)
{
    return table == NULL ? 0U : table->size;
}

size_t ht_capacity(const ht_t *table)
{
    return table == NULL ? 0U : table->capacity;
}

size_t ht_resize_threshold(const ht_t *table)
{
    return table == NULL ? 0U : table->resize_threshold;
}

const char *ht_status_name(ht_status_t status)
{
    switch (status) {
    case HT_OK:
        return "ok";
    case HT_NOT_FOUND:
        return "not_found";
    case HT_INVALID_ARGUMENT:
        return "invalid_argument";
    case HT_NO_MEMORY:
        return "no_memory";
    case HT_OVERFLOW:
        return "overflow";
    }
    return "unknown";
}

#if HT_ENABLE_INSPECTION
void ht_internal_set_trace(
    ht_t *table,
    ht_trace_sink_fn sink,
    void *context)
{
    if (table != NULL) {
        table->trace_sink = sink;
        table->trace_context = context;
    }
}

ht_status_t ht_internal_clone(const ht_t *source, ht_t **out_clone)
{
    ht_config_t config;
    ht_t *clone;
    size_t bucket_index;
    ht_status_t status;

    if (source == NULL || out_clone == NULL) {
        return HT_INVALID_ARGUMENT;
    }
    *out_clone = NULL;
    config = (ht_config_t){
        .struct_size = sizeof(config),
        .initial_capacity = source->capacity,
        .hash_seed = source->hash_seed,
        .allocator = source->allocator
    };
    status = ht_create(&config, &clone);
    if (status != HT_OK) {
        return status;
    }

    for (bucket_index = 0U; bucket_index < source->capacity; ++bucket_index) {
        const ht_entry_t *source_entry = source->buckets[bucket_index];
        ht_entry_t **destination_link = &clone->buckets[bucket_index];
        while (source_entry != NULL) {
            ht_entry_t *copy;
            status = ht_allocate_entry(
                clone,
                source_entry->key,
                source_entry->key_size,
                source_entry->hash,
                source_entry->value,
                &copy);
            if (status != HT_OK) {
                ht_destroy(clone);
                return status;
            }
            copy->entry_id = source_entry->entry_id;
            *destination_link = copy;
            destination_link = &copy->next;
            source_entry = source_entry->next;
        }
    }

    clone->size = source->size;
    clone->resize_threshold = source->resize_threshold;
    clone->revision = source->revision;
    clone->next_entry_id = source->next_entry_id;
    *out_clone = clone;
    return HT_OK;
}
#endif

#if HT_ENABLE_CORRUPTION
ht_status_t ht_internal_corrupt_size_plus_one(ht_t *table)
{
    if (table == NULL) {
        return HT_INVALID_ARGUMENT;
    }
    if (table->size == SIZE_MAX) {
        return HT_OVERFLOW;
    }
    ++table->size;
    ++table->revision;
    return HT_OK;
}
#endif
