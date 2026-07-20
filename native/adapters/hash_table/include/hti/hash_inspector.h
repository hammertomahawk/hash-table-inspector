#ifndef HTI_HASH_INSPECTOR_H
#define HTI_HASH_INSPECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hti/hash_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTI_JSON_CONTRACT_VERSION UINT32_C(1)
#define HTI_DEMO_KEY_MAX_BYTES ((size_t)32)

typedef struct hti_session hti_session_t;
typedef struct hti_snapshot hti_snapshot_t;

typedef enum {
    HTI_OK = 0,
    HTI_INVALID_ARGUMENT,
    HTI_NO_MEMORY,
    HTI_NOT_FOUND,
    HTI_OVERFLOW,
    HTI_QUARANTINED,
    HTI_NOT_CORRUPTED,
    HTI_CAPTURE_FAILED,
    HTI_CORRUPT_STRUCTURE
} hti_status_t;

typedef enum {
    HTI_EVENT_SESSION_CREATED = 1,
    HTI_EVENT_PUT_INSERTED,
    HTI_EVENT_PUT_UPDATED,
    HTI_EVENT_LOOKUP_HIT,
    HTI_EVENT_LOOKUP_MISS,
    HTI_EVENT_DELETE_HIT,
    HTI_EVENT_DELETE_MISS,
    HTI_EVENT_REHASH_BEGIN,
    HTI_EVENT_REHASH_END,
    HTI_EVENT_REHASH_FAILED,
    HTI_EVENT_VALIDATION_PASSED,
    HTI_EVENT_VALIDATION_FAILED,
    HTI_EVENT_SIZE_CORRUPTION_APPLIED,
    HTI_EVENT_CLEAN_STATE_RESTORED,
    HTI_EVENT_OPERATION_FAILED
} hti_event_code_t;

typedef enum {
    HTI_INVARIANT_NONE = 0,
    HTI_INVARIANT_SIZE_MISMATCH,
    HTI_INVARIANT_RESIZE_THRESHOLD,
    HTI_INVARIANT_LOAD_EXCEEDED,
    HTI_INVARIANT_HASH_MISMATCH,
    HTI_INVARIANT_WRONG_BUCKET,
    HTI_INVARIANT_ZERO_ENTRY_ID,
    HTI_INVARIANT_DUPLICATE_ENTRY_ID,
    HTI_INVARIANT_DUPLICATE_KEY,
    HTI_INVARIANT_NEXT_ENTRY_ID
} hti_invariant_code_t;

typedef struct {
    uint64_t revision;
    size_t reported_size;
    size_t entry_count;
    size_t capacity;
    size_t resize_threshold;
    bool valid;
    size_t violation_count;
    hti_invariant_code_t first_violation;
} hti_snapshot_info_t;

typedef struct {
    size_t index;
    size_t first_entry_index;
    size_t chain_length;
} hti_bucket_view_t;

typedef struct {
    uint64_t entry_id;
    uint64_t hash;
    int64_t value;
    size_t bucket_index;
    size_t chain_index;
    const unsigned char *key;
    size_t key_size;
} hti_entry_view_t;

typedef struct {
    uint64_t frame_id;
    uint64_t sequence;
    uint64_t operation_id;
    hti_event_code_t event_code;
    int32_t status;
    uint64_t subject_id;
    uint64_t data[4];
    const hti_snapshot_t *snapshot;
} hti_frame_view_t;

typedef struct {
    hti_status_t status;
    ht_status_t table_status;
    uint64_t operation_id;
    hti_event_code_t event_code;
    uint64_t first_frame_id;
    size_t frame_count;
    bool has_value;
    int64_t value;
} hti_operation_result_t;

hti_status_t hti_session_create(
    size_t initial_capacity,
    uint64_t hash_seed,
    hti_session_t **out_session);

void hti_session_destroy(hti_session_t *session);

hti_status_t hti_session_put(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    int64_t value,
    hti_operation_result_t *out_result);

hti_status_t hti_session_lookup(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    hti_operation_result_t *out_result);

hti_status_t hti_session_delete(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    hti_operation_result_t *out_result);

hti_status_t hti_session_validate(
    hti_session_t *session,
    hti_operation_result_t *out_result);

hti_status_t hti_session_corrupt_size(
    hti_session_t *session,
    hti_operation_result_t *out_result);

hti_status_t hti_session_restore(
    hti_session_t *session,
    hti_operation_result_t *out_result);

bool hti_session_is_quarantined(const hti_session_t *session);
size_t hti_session_timeline_count(const hti_session_t *session);

hti_status_t hti_session_frame_at(
    const hti_session_t *session,
    size_t index,
    hti_frame_view_t *out_frame);

hti_status_t hti_session_frame_by_id(
    const hti_session_t *session,
    uint64_t frame_id,
    hti_frame_view_t *out_frame);

hti_status_t hti_snapshot_info(
    const hti_snapshot_t *snapshot,
    hti_snapshot_info_t *out_info);

hti_status_t hti_snapshot_bucket(
    const hti_snapshot_t *snapshot,
    size_t index,
    hti_bucket_view_t *out_bucket);

hti_status_t hti_snapshot_entry(
    const hti_snapshot_t *snapshot,
    size_t index,
    hti_entry_view_t *out_entry);

hti_status_t hti_operation_result_json(
    const hti_session_t *session,
    const hti_operation_result_t *result,
    char **out_json,
    size_t *out_size);

hti_status_t hti_timeline_json(
    const hti_session_t *session,
    char **out_json,
    size_t *out_size);

void hti_json_free(char *json);
bool hti_demo_key_is_valid(const void *key, size_t key_size);

const char *hti_status_name(hti_status_t status);
const char *hti_event_name(hti_event_code_t event_code);
const char *hti_invariant_name(hti_invariant_code_t invariant);

#ifdef __cplusplus
}
#endif

#endif
