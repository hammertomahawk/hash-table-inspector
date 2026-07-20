#include "hash_inspector_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t first_entry_index;
    size_t chain_length;
} hti_snapshot_bucket_record_t;

typedef struct {
    uint64_t entry_id;
    uint64_t hash;
    int64_t value;
    size_t bucket_index;
    size_t chain_index;
    size_t key_offset;
    size_t key_size;
} hti_snapshot_entry_record_t;

struct hti_snapshot {
    uint64_t revision;
    size_t reported_size;
    size_t entry_count;
    size_t capacity;
    size_t resize_threshold;
    size_t violation_count;
    hti_invariant_code_t first_violation;
    size_t buckets_offset;
    size_t entries_offset;
    size_t keys_offset;
    size_t total_size;
    unsigned char storage[];
};

static bool hti_size_add(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || left > SIZE_MAX - right) {
        return false;
    }
    *out_value = left + right;
    return true;
}

static bool hti_size_multiply(size_t left, size_t right, size_t *out_value)
{
    if (out_value == NULL || (right != 0U && left > SIZE_MAX / right)) {
        return false;
    }
    *out_value = left * right;
    return true;
}

static bool hti_align_up(size_t value, size_t alignment, size_t *out_value)
{
    size_t remainder;
    size_t padding;

    if (alignment == 0U || out_value == NULL) {
        return false;
    }
    remainder = value % alignment;
    padding = remainder == 0U ? 0U : alignment - remainder;
    return hti_size_add(value, padding, out_value);
}

static bool hti_is_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool hti_chain_has_cycle(const ht_entry_t *head)
{
    const ht_entry_t *slow = head;
    const ht_entry_t *fast = head;

    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) {
            return true;
        }
    }
    return false;
}

static hti_snapshot_bucket_record_t *hti_bucket_records(hti_snapshot_t *snapshot)
{
    return (hti_snapshot_bucket_record_t *)
        ((unsigned char *)snapshot + snapshot->buckets_offset);
}

static const hti_snapshot_bucket_record_t *hti_const_bucket_records(
    const hti_snapshot_t *snapshot)
{
    return (const hti_snapshot_bucket_record_t *)
        ((const unsigned char *)snapshot + snapshot->buckets_offset);
}

static hti_snapshot_entry_record_t *hti_entry_records(hti_snapshot_t *snapshot)
{
    return (hti_snapshot_entry_record_t *)
        ((unsigned char *)snapshot + snapshot->entries_offset);
}

static const hti_snapshot_entry_record_t *hti_const_entry_records(
    const hti_snapshot_t *snapshot)
{
    return (const hti_snapshot_entry_record_t *)
        ((const unsigned char *)snapshot + snapshot->entries_offset);
}

static unsigned char *hti_key_pool(hti_snapshot_t *snapshot)
{
    return (unsigned char *)snapshot + snapshot->keys_offset;
}

static const unsigned char *hti_const_key_pool(const hti_snapshot_t *snapshot)
{
    return (const unsigned char *)snapshot + snapshot->keys_offset;
}

static void hti_record_violation(
    hti_snapshot_t *snapshot,
    hti_invariant_code_t invariant)
{
    if (snapshot->violation_count == 0U) {
        snapshot->first_violation = invariant;
    }
    ++snapshot->violation_count;
}

static void hti_validate_snapshot(const ht_t *table, hti_snapshot_t *snapshot)
{
    const hti_snapshot_entry_record_t *entries = hti_const_entry_records(snapshot);
    const unsigned char *keys = hti_const_key_pool(snapshot);
    size_t index;

    if (snapshot->reported_size != snapshot->entry_count) {
        hti_record_violation(snapshot, HTI_INVARIANT_SIZE_MISMATCH);
    }
    if (snapshot->resize_threshold !=
        snapshot->capacity - (snapshot->capacity / 4U)) {
        hti_record_violation(snapshot, HTI_INVARIANT_RESIZE_THRESHOLD);
    }
    if (snapshot->reported_size > snapshot->resize_threshold) {
        hti_record_violation(snapshot, HTI_INVARIANT_LOAD_EXCEEDED);
    }

    for (index = 0U; index < snapshot->entry_count; ++index) {
        const hti_snapshot_entry_record_t *entry = &entries[index];
        const unsigned char *key = keys + entry->key_offset;
        uint64_t expected_hash = ht_internal_hash_key(
            table->hash_seed,
            key,
            entry->key_size);
        size_t expected_bucket =
            (size_t)(expected_hash & (uint64_t)(snapshot->capacity - 1U));
        size_t other_index;

        if (entry->hash != expected_hash) {
            hti_record_violation(snapshot, HTI_INVARIANT_HASH_MISMATCH);
        }
        if (entry->bucket_index != expected_bucket) {
            hti_record_violation(snapshot, HTI_INVARIANT_WRONG_BUCKET);
        }
        if (entry->entry_id == UINT64_C(0)) {
            hti_record_violation(snapshot, HTI_INVARIANT_ZERO_ENTRY_ID);
        }
        if (entry->entry_id >= table->next_entry_id) {
            hti_record_violation(snapshot, HTI_INVARIANT_NEXT_ENTRY_ID);
        }

        for (other_index = 0U; other_index < index; ++other_index) {
            const hti_snapshot_entry_record_t *other = &entries[other_index];
            if (entry->entry_id == other->entry_id) {
                hti_record_violation(snapshot, HTI_INVARIANT_DUPLICATE_ENTRY_ID);
            }
            if (entry->hash == other->hash &&
                entry->key_size == other->key_size &&
                (entry->key_size == 0U ||
                 memcmp(
                     key,
                     keys + other->key_offset,
                     entry->key_size) == 0)) {
                hti_record_violation(snapshot, HTI_INVARIANT_DUPLICATE_KEY);
            }
        }
    }
}

hti_status_t hti_snapshot_capture_internal(
    const ht_t *table,
    hti_snapshot_t **out_snapshot)
{
    size_t entry_count = 0U;
    size_t key_bytes = 0U;
    size_t bucket_bytes;
    size_t entry_bytes;
    size_t buckets_offset;
    size_t entries_offset;
    size_t keys_offset;
    size_t total_size;
    size_t bucket_index;
    size_t entry_index = 0U;
    size_t key_offset = 0U;
    hti_snapshot_t *snapshot;
    hti_snapshot_bucket_record_t *bucket_records;
    hti_snapshot_entry_record_t *entry_records;
    unsigned char *keys;

    if (table == NULL || out_snapshot == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *out_snapshot = NULL;
    if (table->buckets == NULL || table->capacity < 4U ||
        !hti_is_power_of_two(table->capacity)) {
        return HTI_CORRUPT_STRUCTURE;
    }

    for (bucket_index = 0U; bucket_index < table->capacity; ++bucket_index) {
        const ht_entry_t *entry;
        if (hti_chain_has_cycle(table->buckets[bucket_index])) {
            return HTI_CORRUPT_STRUCTURE;
        }
        entry = table->buckets[bucket_index];
        while (entry != NULL) {
            if (!hti_size_add(entry_count, 1U, &entry_count) ||
                !hti_size_add(key_bytes, entry->key_size, &key_bytes)) {
                return HTI_OVERFLOW;
            }
            entry = entry->next;
        }
    }

    if (!hti_size_multiply(
            table->capacity,
            sizeof(hti_snapshot_bucket_record_t),
            &bucket_bytes) ||
        !hti_size_multiply(
            entry_count,
            sizeof(hti_snapshot_entry_record_t),
            &entry_bytes) ||
        !hti_align_up(
            sizeof(*snapshot),
            _Alignof(hti_snapshot_bucket_record_t),
            &buckets_offset) ||
        !hti_size_add(buckets_offset, bucket_bytes, &entries_offset) ||
        !hti_align_up(
            entries_offset,
            _Alignof(hti_snapshot_entry_record_t),
            &entries_offset) ||
        !hti_size_add(entries_offset, entry_bytes, &keys_offset) ||
        !hti_size_add(keys_offset, key_bytes, &total_size)) {
        return HTI_OVERFLOW;
    }

    snapshot = calloc(1U, total_size);
    if (snapshot == NULL) {
        return HTI_NO_MEMORY;
    }
    snapshot->revision = table->revision;
    snapshot->reported_size = table->size;
    snapshot->entry_count = entry_count;
    snapshot->capacity = table->capacity;
    snapshot->resize_threshold = table->resize_threshold;
    snapshot->first_violation = HTI_INVARIANT_NONE;
    snapshot->buckets_offset = buckets_offset;
    snapshot->entries_offset = entries_offset;
    snapshot->keys_offset = keys_offset;
    snapshot->total_size = total_size;

    bucket_records = hti_bucket_records(snapshot);
    entry_records = hti_entry_records(snapshot);
    keys = hti_key_pool(snapshot);

    for (bucket_index = 0U; bucket_index < table->capacity; ++bucket_index) {
        const ht_entry_t *entry = table->buckets[bucket_index];
        size_t chain_index = 0U;
        bucket_records[bucket_index].first_entry_index =
            entry == NULL ? SIZE_MAX : entry_index;
        while (entry != NULL) {
            hti_snapshot_entry_record_t *record = &entry_records[entry_index];
            record->entry_id = entry->entry_id;
            record->hash = entry->hash;
            record->value = entry->value;
            record->bucket_index = bucket_index;
            record->chain_index = chain_index;
            record->key_offset = key_offset;
            record->key_size = entry->key_size;
            if (entry->key_size != 0U) {
                memcpy(keys + key_offset, entry->key, entry->key_size);
            }
            key_offset += entry->key_size;
            ++entry_index;
            ++chain_index;
            entry = entry->next;
        }
        bucket_records[bucket_index].chain_length = chain_index;
    }

    hti_validate_snapshot(table, snapshot);
    *out_snapshot = snapshot;
    return HTI_OK;
}

void hti_snapshot_release_internal(void *snapshot)
{
    free(snapshot);
}

hti_status_t hti_snapshot_info(
    const hti_snapshot_t *snapshot,
    hti_snapshot_info_t *out_info)
{
    if (snapshot == NULL || out_info == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *out_info = (hti_snapshot_info_t){
        .revision = snapshot->revision,
        .reported_size = snapshot->reported_size,
        .entry_count = snapshot->entry_count,
        .capacity = snapshot->capacity,
        .resize_threshold = snapshot->resize_threshold,
        .valid = snapshot->violation_count == 0U,
        .violation_count = snapshot->violation_count,
        .first_violation = snapshot->first_violation
    };
    return HTI_OK;
}

hti_status_t hti_snapshot_bucket(
    const hti_snapshot_t *snapshot,
    size_t index,
    hti_bucket_view_t *out_bucket)
{
    const hti_snapshot_bucket_record_t *records;

    if (snapshot == NULL || out_bucket == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    if (index >= snapshot->capacity) {
        return HTI_NOT_FOUND;
    }
    records = hti_const_bucket_records(snapshot);
    *out_bucket = (hti_bucket_view_t){
        .index = index,
        .first_entry_index = records[index].first_entry_index,
        .chain_length = records[index].chain_length
    };
    return HTI_OK;
}

hti_status_t hti_snapshot_entry(
    const hti_snapshot_t *snapshot,
    size_t index,
    hti_entry_view_t *out_entry)
{
    const hti_snapshot_entry_record_t *records;
    const hti_snapshot_entry_record_t *record;

    if (snapshot == NULL || out_entry == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    if (index >= snapshot->entry_count) {
        return HTI_NOT_FOUND;
    }
    records = hti_const_entry_records(snapshot);
    record = &records[index];
    *out_entry = (hti_entry_view_t){
        .entry_id = record->entry_id,
        .hash = record->hash,
        .value = record->value,
        .bucket_index = record->bucket_index,
        .chain_index = record->chain_index,
        .key = hti_const_key_pool(snapshot) + record->key_offset,
        .key_size = record->key_size
    };
    return HTI_OK;
}
