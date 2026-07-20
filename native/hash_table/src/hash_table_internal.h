#ifndef HTI_HASH_TABLE_INTERNAL_H
#define HTI_HASH_TABLE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hti/hash_table.h"

#ifndef HT_ENABLE_INSPECTION
#define HT_ENABLE_INSPECTION 0
#endif

#ifndef HT_ENABLE_CORRUPTION
#define HT_ENABLE_CORRUPTION 0
#endif

typedef struct ht_entry {
    uint64_t entry_id;
    uint64_t hash;
    size_t key_size;
    int64_t value;
    struct ht_entry *next;
    unsigned char key[];
} ht_entry_t;

typedef enum {
    HT_TRACE_PUT_INSERTED = 1,
    HT_TRACE_PUT_UPDATED,
    HT_TRACE_LOOKUP_HIT,
    HT_TRACE_LOOKUP_MISS,
    HT_TRACE_DELETE_HIT,
    HT_TRACE_DELETE_MISS,
    HT_TRACE_REHASH_BEGIN,
    HT_TRACE_REHASH_END,
    HT_TRACE_REHASH_FAILED
} ht_trace_kind_t;

typedef struct {
    ht_trace_kind_t kind;
    ht_status_t status;
    uint64_t entry_id;
    uint64_t hash;
    size_t bucket_index;
    size_t probe_count;
    size_t moved_count;
    size_t size_before;
    size_t size_after;
    size_t capacity_before;
    size_t capacity_after;
} ht_trace_event_t;

typedef void (*ht_trace_sink_fn)(
    void *context,
    const ht_t *table,
    const ht_trace_event_t *event);

struct ht {
    ht_entry_t **buckets;
    size_t size;
    size_t capacity;
    size_t resize_threshold;
    uint64_t revision;
    uint64_t next_entry_id;
    uint64_t hash_seed;
    ht_allocator_t allocator;
#if HT_ENABLE_INSPECTION
    ht_trace_sink_fn trace_sink;
    void *trace_context;
#endif
};

uint64_t ht_internal_hash_key(
    uint64_t seed,
    const void *key,
    size_t key_size);

#if HT_ENABLE_INSPECTION
void ht_internal_set_trace(
    ht_t *table,
    ht_trace_sink_fn sink,
    void *context);

ht_status_t ht_internal_clone(const ht_t *source, ht_t **out_clone);
#endif

#if HT_ENABLE_CORRUPTION
ht_status_t ht_internal_corrupt_size_plus_one(ht_t *table);
#endif

#endif
