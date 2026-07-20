#include "hti/hash_inspector.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hti/inspection.h"
#include "hash_inspector_internal.h"

struct hti_session {
    ht_t *table;
    ht_t *clean_backup;
    inspect_timeline_t *timeline;
    uint64_t next_operation_id;
    bool quarantined;
    hti_operation_result_t *active_result;
    hti_status_t active_capture_status;
};

static hti_status_t hti_from_inspect_status(inspect_status_t status)
{
    switch (status) {
    case INSPECT_OK:
        return HTI_OK;
    case INSPECT_INVALID_ARGUMENT:
        return HTI_INVALID_ARGUMENT;
    case INSPECT_NO_MEMORY:
        return HTI_NO_MEMORY;
    case INSPECT_NOT_FOUND:
        return HTI_NOT_FOUND;
    case INSPECT_OVERFLOW:
        return HTI_OVERFLOW;
    }
    return HTI_CAPTURE_FAILED;
}

static hti_status_t hti_append_owned_snapshot(
    hti_session_t *session,
    hti_operation_result_t *result,
    hti_event_code_t event_code,
    int32_t status,
    uint64_t subject_id,
    const uint64_t data[4],
    hti_snapshot_t *snapshot)
{
    inspect_event_t event = {
        .operation_id = result == NULL ? UINT64_C(0) : result->operation_id,
        .domain = INSPECT_DOMAIN_HASH_TABLE,
        .code = (uint32_t)event_code,
        .status = status,
        .subject_id = subject_id
    };
    inspect_owned_snapshot_t owned = {
        .data = snapshot,
        .release = hti_snapshot_release_internal
    };
    inspect_status_t inspect_status;
    uint64_t frame_id;
    size_t index;

    if (data != NULL) {
        for (index = 0U; index < 4U; ++index) {
            event.data[index] = data[index];
        }
    }
    inspect_status = inspect_timeline_append(
        session->timeline,
        &event,
        &owned,
        &frame_id);
    if (inspect_status != INSPECT_OK) {
        hti_snapshot_release_internal(snapshot);
        return hti_from_inspect_status(inspect_status);
    }
    if (result != NULL) {
        if (result->frame_count == 0U) {
            result->first_frame_id = frame_id;
        }
        ++result->frame_count;
        result->event_code = event_code;
    }
    return HTI_OK;
}

static hti_status_t hti_capture_event(
    hti_session_t *session,
    hti_operation_result_t *result,
    hti_event_code_t event_code,
    int32_t status,
    uint64_t subject_id,
    const uint64_t data[4])
{
    hti_snapshot_t *snapshot;
    hti_status_t capture_status = hti_snapshot_capture_internal(
        session->table,
        &snapshot);
    if (capture_status != HTI_OK) {
        return capture_status;
    }
    return hti_append_owned_snapshot(
        session,
        result,
        event_code,
        status,
        subject_id,
        data,
        snapshot);
}

static hti_event_code_t hti_event_from_trace(ht_trace_kind_t kind)
{
    switch (kind) {
    case HT_TRACE_PUT_INSERTED:
        return HTI_EVENT_PUT_INSERTED;
    case HT_TRACE_PUT_UPDATED:
        return HTI_EVENT_PUT_UPDATED;
    case HT_TRACE_LOOKUP_HIT:
        return HTI_EVENT_LOOKUP_HIT;
    case HT_TRACE_LOOKUP_MISS:
        return HTI_EVENT_LOOKUP_MISS;
    case HT_TRACE_DELETE_HIT:
        return HTI_EVENT_DELETE_HIT;
    case HT_TRACE_DELETE_MISS:
        return HTI_EVENT_DELETE_MISS;
    case HT_TRACE_REHASH_BEGIN:
        return HTI_EVENT_REHASH_BEGIN;
    case HT_TRACE_REHASH_END:
        return HTI_EVENT_REHASH_END;
    case HT_TRACE_REHASH_FAILED:
        return HTI_EVENT_REHASH_FAILED;
    }
    return HTI_EVENT_OPERATION_FAILED;
}

static void hti_trace_sink(
    void *context,
    const ht_t *table,
    const ht_trace_event_t *trace)
{
    hti_session_t *session = context;
    uint64_t data[4];
    hti_status_t status;

    (void)table;
    if (session == NULL || trace == NULL || session->active_result == NULL ||
        session->active_capture_status != HTI_OK) {
        return;
    }

    if (trace->kind == HT_TRACE_REHASH_BEGIN ||
        trace->kind == HT_TRACE_REHASH_END ||
        trace->kind == HT_TRACE_REHASH_FAILED) {
        data[0] = (uint64_t)trace->capacity_before;
        data[1] = (uint64_t)trace->capacity_after;
        data[2] = (uint64_t)trace->moved_count;
        data[3] = (uint64_t)trace->size_after;
    } else {
        data[0] = (uint64_t)trace->bucket_index;
        data[1] = (uint64_t)trace->probe_count;
        data[2] = (uint64_t)trace->size_after;
        data[3] = (uint64_t)trace->capacity_after;
    }

    status = hti_capture_event(
        session,
        session->active_result,
        hti_event_from_trace(trace->kind),
        (int32_t)trace->status,
        trace->entry_id,
        data);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
}

static hti_status_t hti_begin_operation(
    hti_session_t *session,
    hti_operation_result_t *result)
{
    if (session == NULL || result == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *result = (hti_operation_result_t){
        .status = HTI_OK,
        .table_status = HT_OK
    };
    if (session->next_operation_id == UINT64_MAX) {
        result->status = HTI_OVERFLOW;
        return HTI_OVERFLOW;
    }
    result->operation_id = session->next_operation_id;
    ++session->next_operation_id;
    session->active_result = result;
    session->active_capture_status = HTI_OK;
    return HTI_OK;
}

static hti_status_t hti_finish_operation(hti_session_t *session)
{
    hti_status_t status = session->active_capture_status;
    hti_operation_result_t *result = session->active_result;

    session->active_result = NULL;
    session->active_capture_status = HTI_OK;
    if (status != HTI_OK) {
        result->status = HTI_CAPTURE_FAILED;
        return HTI_CAPTURE_FAILED;
    }
    result->status = HTI_OK;
    return HTI_OK;
}

static hti_status_t hti_reject_operation(
    hti_operation_result_t *result,
    hti_status_t status)
{
    if (result != NULL) {
        *result = (hti_operation_result_t){
            .status = status,
            .table_status = HT_INVALID_ARGUMENT
        };
    }
    return status;
}

static hti_status_t hti_capture_failure_if_needed(
    hti_session_t *session,
    hti_operation_result_t *result)
{
    uint64_t data[4] = {0};

    if (result->frame_count != 0U || session->active_capture_status != HTI_OK) {
        return session->active_capture_status;
    }
    data[0] = (uint64_t)result->table_status;
    return hti_capture_event(
        session,
        result,
        HTI_EVENT_OPERATION_FAILED,
        (int32_t)result->table_status,
        UINT64_C(0),
        data);
}

bool hti_demo_key_is_valid(const void *key, size_t key_size)
{
    const unsigned char *bytes = key;
    size_t index;

    if (key == NULL || key_size == 0U || key_size > HTI_DEMO_KEY_MAX_BYTES) {
        return false;
    }
    for (index = 0U; index < key_size; ++index) {
        unsigned char byte = bytes[index];
        bool allowed =
            (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
            (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
            (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
            byte == (unsigned char)'.' || byte == (unsigned char)'_' ||
            byte == (unsigned char)'-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

hti_status_t hti_session_create(
    size_t initial_capacity,
    uint64_t hash_seed,
    hti_session_t **out_session)
{
    hti_session_t *session;
    ht_config_t config;
    inspect_status_t inspect_status;
    ht_status_t table_status;
    hti_status_t status;

    if (out_session == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *out_session = NULL;
    session = calloc(1U, sizeof(*session));
    if (session == NULL) {
        return HTI_NO_MEMORY;
    }
    inspect_status = inspect_timeline_create(&session->timeline);
    if (inspect_status != INSPECT_OK) {
        free(session);
        return hti_from_inspect_status(inspect_status);
    }
    config = (ht_config_t){
        .struct_size = sizeof(config),
        .initial_capacity = initial_capacity,
        .hash_seed = hash_seed
    };
    table_status = ht_create(&config, &session->table);
    if (table_status != HT_OK) {
        inspect_timeline_destroy(session->timeline);
        free(session);
        return table_status == HT_NO_MEMORY ? HTI_NO_MEMORY : HTI_INVALID_ARGUMENT;
    }
    session->next_operation_id = UINT64_C(1);
    ht_internal_set_trace(session->table, hti_trace_sink, session);
    status = hti_capture_event(
        session,
        NULL,
        HTI_EVENT_SESSION_CREATED,
        (int32_t)HT_OK,
        UINT64_C(0),
        NULL);
    if (status != HTI_OK) {
        ht_destroy(session->table);
        inspect_timeline_destroy(session->timeline);
        free(session);
        return status;
    }
    *out_session = session;
    return HTI_OK;
}

void hti_session_destroy(hti_session_t *session)
{
    if (session == NULL) {
        return;
    }
    ht_destroy(session->table);
    ht_destroy(session->clean_backup);
    inspect_timeline_destroy(session->timeline);
    free(session);
}

hti_status_t hti_session_put(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    int64_t value,
    hti_operation_result_t *out_result)
{
    ht_put_result_t put_result;
    hti_status_t status;

    if (session == NULL || out_result == NULL ||
        !hti_demo_key_is_valid(key, key_size)) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    if (session->quarantined) {
        return hti_reject_operation(out_result, HTI_QUARANTINED);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    out_result->table_status = ht_put(
        session->table,
        key,
        key_size,
        value,
        &put_result);
    if (out_result->table_status == HT_OK) {
        out_result->has_value = true;
        out_result->value = value;
    }
    status = hti_capture_failure_if_needed(session, out_result);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

hti_status_t hti_session_lookup(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    hti_operation_result_t *out_result)
{
    int64_t value = 0;
    hti_status_t status;

    if (session == NULL || out_result == NULL ||
        !hti_demo_key_is_valid(key, key_size)) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    if (session->quarantined) {
        return hti_reject_operation(out_result, HTI_QUARANTINED);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    out_result->table_status = ht_get(session->table, key, key_size, &value);
    if (out_result->table_status == HT_OK) {
        out_result->has_value = true;
        out_result->value = value;
    }
    status = hti_capture_failure_if_needed(session, out_result);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

hti_status_t hti_session_delete(
    hti_session_t *session,
    const void *key,
    size_t key_size,
    hti_operation_result_t *out_result)
{
    int64_t value = 0;
    hti_status_t status;

    if (session == NULL || out_result == NULL ||
        !hti_demo_key_is_valid(key, key_size)) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    if (session->quarantined) {
        return hti_reject_operation(out_result, HTI_QUARANTINED);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    out_result->table_status = ht_remove(session->table, key, key_size, &value);
    if (out_result->table_status == HT_OK) {
        out_result->has_value = true;
        out_result->value = value;
    }
    status = hti_capture_failure_if_needed(session, out_result);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

hti_status_t hti_session_validate(
    hti_session_t *session,
    hti_operation_result_t *out_result)
{
    hti_snapshot_t *snapshot;
    hti_snapshot_info_t info;
    hti_status_t status;
    uint64_t data[4] = {0};
    hti_event_code_t event_code;

    if (session == NULL || out_result == NULL) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    status = hti_snapshot_capture_internal(session->table, &snapshot);
    if (status != HTI_OK) {
        session->active_capture_status = status;
        return hti_finish_operation(session);
    }
    (void)hti_snapshot_info(snapshot, &info);
    event_code = info.valid ?
        HTI_EVENT_VALIDATION_PASSED : HTI_EVENT_VALIDATION_FAILED;
    data[0] = (uint64_t)info.violation_count;
    data[1] = (uint64_t)info.first_violation;
    data[2] = (uint64_t)info.reported_size;
    data[3] = (uint64_t)info.entry_count;
    status = hti_append_owned_snapshot(
        session,
        out_result,
        event_code,
        info.valid ? INT32_C(0) : INT32_C(1),
        UINT64_C(0),
        data,
        snapshot);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

hti_status_t hti_session_corrupt_size(
    hti_session_t *session,
    hti_operation_result_t *out_result)
{
    ht_t *clone;
    ht_status_t table_status;
    hti_status_t status;
    uint64_t data[4] = {UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0)};

    if (session == NULL || out_result == NULL) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    if (session->quarantined) {
        return hti_reject_operation(out_result, HTI_QUARANTINED);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    table_status = ht_internal_clone(session->table, &clone);
    if (table_status != HT_OK) {
        out_result->table_status = table_status;
        status = hti_capture_failure_if_needed(session, out_result);
        if (status != HTI_OK) {
            session->active_capture_status = status;
        }
        return hti_finish_operation(session);
    }
    session->clean_backup = session->table;
    session->table = clone;
    ht_internal_set_trace(session->table, hti_trace_sink, session);
    table_status = ht_internal_corrupt_size_plus_one(session->table);
    out_result->table_status = table_status;
    if (table_status != HT_OK) {
        ht_destroy(session->table);
        session->table = session->clean_backup;
        session->clean_backup = NULL;
        return hti_finish_operation(session);
    }
    session->quarantined = true;
    status = hti_capture_event(
        session,
        out_result,
        HTI_EVENT_SIZE_CORRUPTION_APPLIED,
        INT32_C(0),
        UINT64_C(0),
        data);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

hti_status_t hti_session_restore(
    hti_session_t *session,
    hti_operation_result_t *out_result)
{
    ht_t *corrupted;
    hti_status_t status;

    if (session == NULL || out_result == NULL) {
        return hti_reject_operation(out_result, HTI_INVALID_ARGUMENT);
    }
    if (!session->quarantined || session->clean_backup == NULL) {
        return hti_reject_operation(out_result, HTI_NOT_CORRUPTED);
    }
    status = hti_begin_operation(session, out_result);
    if (status != HTI_OK) {
        return status;
    }
    corrupted = session->table;
    session->table = session->clean_backup;
    session->clean_backup = NULL;
    session->quarantined = false;
    ht_destroy(corrupted);
    status = hti_capture_event(
        session,
        out_result,
        HTI_EVENT_CLEAN_STATE_RESTORED,
        INT32_C(0),
        UINT64_C(0),
        NULL);
    if (status != HTI_OK) {
        session->active_capture_status = status;
    }
    return hti_finish_operation(session);
}

bool hti_session_is_quarantined(const hti_session_t *session)
{
    return session != NULL && session->quarantined;
}

size_t hti_session_timeline_count(const hti_session_t *session)
{
    return session == NULL ? 0U : inspect_timeline_count(session->timeline);
}

static hti_status_t hti_convert_frame(
    inspect_status_t status,
    const inspect_frame_view_t *frame,
    hti_frame_view_t *out_frame)
{
    size_t index;

    if (status != INSPECT_OK) {
        return hti_from_inspect_status(status);
    }
    *out_frame = (hti_frame_view_t){
        .frame_id = frame->frame_id,
        .sequence = frame->event.sequence,
        .operation_id = frame->event.operation_id,
        .event_code = (hti_event_code_t)frame->event.code,
        .status = frame->event.status,
        .subject_id = frame->event.subject_id,
        .snapshot = frame->snapshot
    };
    for (index = 0U; index < 4U; ++index) {
        out_frame->data[index] = frame->event.data[index];
    }
    return HTI_OK;
}

hti_status_t hti_session_frame_at(
    const hti_session_t *session,
    size_t index,
    hti_frame_view_t *out_frame)
{
    inspect_frame_view_t frame;
    inspect_status_t status;

    if (session == NULL || out_frame == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    status = inspect_timeline_get_at(session->timeline, index, &frame);
    return hti_convert_frame(status, &frame, out_frame);
}

hti_status_t hti_session_frame_by_id(
    const hti_session_t *session,
    uint64_t frame_id,
    hti_frame_view_t *out_frame)
{
    inspect_frame_view_t frame;
    inspect_status_t status;

    if (session == NULL || out_frame == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    status = inspect_timeline_get_by_id(session->timeline, frame_id, &frame);
    return hti_convert_frame(status, &frame, out_frame);
}

const char *hti_status_name(hti_status_t status)
{
    switch (status) {
    case HTI_OK:
        return "ok";
    case HTI_INVALID_ARGUMENT:
        return "invalid_argument";
    case HTI_NO_MEMORY:
        return "no_memory";
    case HTI_NOT_FOUND:
        return "not_found";
    case HTI_OVERFLOW:
        return "overflow";
    case HTI_QUARANTINED:
        return "quarantined";
    case HTI_NOT_CORRUPTED:
        return "not_corrupted";
    case HTI_CAPTURE_FAILED:
        return "capture_failed";
    case HTI_CORRUPT_STRUCTURE:
        return "corrupt_structure";
    }
    return "unknown";
}

const char *hti_event_name(hti_event_code_t event_code)
{
    switch (event_code) {
    case HTI_EVENT_SESSION_CREATED:
        return "session_created";
    case HTI_EVENT_PUT_INSERTED:
        return "put_inserted";
    case HTI_EVENT_PUT_UPDATED:
        return "put_updated";
    case HTI_EVENT_LOOKUP_HIT:
        return "lookup_hit";
    case HTI_EVENT_LOOKUP_MISS:
        return "lookup_miss";
    case HTI_EVENT_DELETE_HIT:
        return "delete_hit";
    case HTI_EVENT_DELETE_MISS:
        return "delete_miss";
    case HTI_EVENT_REHASH_BEGIN:
        return "rehash_begin";
    case HTI_EVENT_REHASH_END:
        return "rehash_end";
    case HTI_EVENT_REHASH_FAILED:
        return "rehash_failed";
    case HTI_EVENT_VALIDATION_PASSED:
        return "validation_passed";
    case HTI_EVENT_VALIDATION_FAILED:
        return "validation_failed";
    case HTI_EVENT_SIZE_CORRUPTION_APPLIED:
        return "size_corruption_applied";
    case HTI_EVENT_CLEAN_STATE_RESTORED:
        return "clean_state_restored";
    case HTI_EVENT_OPERATION_FAILED:
        return "operation_failed";
    }
    return "unknown";
}

const char *hti_invariant_name(hti_invariant_code_t invariant)
{
    switch (invariant) {
    case HTI_INVARIANT_NONE:
        return "none";
    case HTI_INVARIANT_SIZE_MISMATCH:
        return "size_mismatch";
    case HTI_INVARIANT_RESIZE_THRESHOLD:
        return "resize_threshold";
    case HTI_INVARIANT_LOAD_EXCEEDED:
        return "load_exceeded";
    case HTI_INVARIANT_HASH_MISMATCH:
        return "hash_mismatch";
    case HTI_INVARIANT_WRONG_BUCKET:
        return "wrong_bucket";
    case HTI_INVARIANT_ZERO_ENTRY_ID:
        return "zero_entry_id";
    case HTI_INVARIANT_DUPLICATE_ENTRY_ID:
        return "duplicate_entry_id";
    case HTI_INVARIANT_DUPLICATE_KEY:
        return "duplicate_key";
    case HTI_INVARIANT_NEXT_ENTRY_ID:
        return "next_entry_id";
    }
    return "unknown";
}
