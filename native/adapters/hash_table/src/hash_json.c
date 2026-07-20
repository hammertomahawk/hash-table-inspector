#include "hti/hash_inspector.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} hti_json_builder_t;

#if defined(__clang__) || defined(__GNUC__)
static bool hti_json_append_format(
    hti_json_builder_t *builder,
    const char *format,
    ...) __attribute__((format(printf, 2, 3)));
#endif

static bool hti_json_reserve(hti_json_builder_t *builder, size_t additional)
{
    size_t required;
    size_t capacity;
    char *resized;

    if (additional > SIZE_MAX - builder->length - 1U) {
        return false;
    }
    required = builder->length + additional + 1U;
    if (required <= builder->capacity) {
        return true;
    }
    capacity = builder->capacity == 0U ? 256U : builder->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    resized = realloc(builder->data, capacity);
    if (resized == NULL) {
        return false;
    }
    builder->data = resized;
    builder->capacity = capacity;
    return true;
}

static bool hti_json_append_raw(
    hti_json_builder_t *builder,
    const char *text,
    size_t length)
{
    if (!hti_json_reserve(builder, length)) {
        return false;
    }
    memcpy(builder->data + builder->length, text, length);
    builder->length += length;
    builder->data[builder->length] = '\0';
    return true;
}

static bool hti_json_append_format(
    hti_json_builder_t *builder,
    const char *format,
    ...)
{
    va_list arguments;
    va_list copy;
    int needed;
    int written;
    size_t required;

    va_start(arguments, format);
    va_copy(copy, arguments);
    needed = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(arguments);
        return false;
    }
    required = (size_t)needed;
    if (!hti_json_reserve(builder, required)) {
        va_end(arguments);
        return false;
    }
    written = vsnprintf(
        builder->data + builder->length,
        builder->capacity - builder->length,
        format,
        arguments);
    va_end(arguments);
    if (written != needed) {
        return false;
    }
    builder->length += required;
    return true;
}

static bool hti_json_append_snapshot(
    hti_json_builder_t *builder,
    const hti_snapshot_t *snapshot)
{
    hti_snapshot_info_t info;
    size_t bucket_index;

    if (hti_snapshot_info(snapshot, &info) != HTI_OK ||
        !hti_json_append_format(
            builder,
            "{\"revision\":%" PRIu64
            ",\"reportedSize\":%zu,\"entryCount\":%zu"
            ",\"capacity\":%zu,\"resizeAt\":%zu"
            ",\"valid\":%s,\"violationCount\":%zu"
            ",\"violation\":\"%s\",\"buckets\":[",
            info.revision,
            info.reported_size,
            info.entry_count,
            info.capacity,
            info.resize_threshold,
            info.valid ? "true" : "false",
            info.violation_count,
            hti_invariant_name(info.first_violation))) {
        return false;
    }

    for (bucket_index = 0U; bucket_index < info.capacity; ++bucket_index) {
        hti_bucket_view_t bucket;
        size_t chain_index;
        if (hti_snapshot_bucket(snapshot, bucket_index, &bucket) != HTI_OK ||
            (bucket_index != 0U &&
             !hti_json_append_raw(builder, ",", 1U)) ||
            !hti_json_append_format(
                builder,
                "{\"index\":%zu,\"entries\":[",
                bucket_index)) {
            return false;
        }
        for (chain_index = 0U; chain_index < bucket.chain_length; ++chain_index) {
            hti_entry_view_t entry;
            size_t entry_index = bucket.first_entry_index + chain_index;
            if (hti_snapshot_entry(snapshot, entry_index, &entry) != HTI_OK ||
                (chain_index != 0U &&
                 !hti_json_append_raw(builder, ",", 1U)) ||
                !hti_json_append_format(
                    builder,
                    "{\"id\":%" PRIu64
                    ",\"hash\":\"%016" PRIx64
                    "\",\"key\":\"",
                    entry.entry_id,
                    entry.hash) ||
                !hti_json_append_raw(
                    builder,
                    (const char *)entry.key,
                    entry.key_size) ||
                !hti_json_append_format(
                    builder,
                    "\",\"value\":%" PRId64
                    ",\"chainIndex\":%zu}",
                    entry.value,
                    entry.chain_index)) {
                return false;
            }
        }
        if (!hti_json_append_raw(builder, "]}", 2U)) {
            return false;
        }
    }
    return hti_json_append_raw(builder, "]}", 2U);
}

static bool hti_json_append_frame(
    hti_json_builder_t *builder,
    const hti_frame_view_t *frame)
{
    if (!hti_json_append_format(
            builder,
            "{\"frameId\":%" PRIu64
            ",\"sequence\":%" PRIu64
            ",\"operationId\":%" PRIu64
            ",\"event\":\"%s\",\"status\":%" PRId32
            ",\"subjectId\":%" PRIu64
            ",\"data\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            "],\"snapshot\":",
            frame->frame_id,
            frame->sequence,
            frame->operation_id,
            hti_event_name(frame->event_code),
            frame->status,
            frame->subject_id,
            frame->data[0],
            frame->data[1],
            frame->data[2],
            frame->data[3]) ||
        !hti_json_append_snapshot(builder, frame->snapshot)) {
        return false;
    }
    return hti_json_append_raw(builder, "}", 1U);
}

hti_status_t hti_operation_result_json(
    const hti_session_t *session,
    const hti_operation_result_t *result,
    char **out_json,
    size_t *out_size)
{
    hti_json_builder_t builder = {0};
    size_t frame_index;

    if (session == NULL || result == NULL || out_json == NULL ||
        out_size == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *out_json = NULL;
    *out_size = 0U;

    if (!hti_json_append_format(
            &builder,
            "{\"version\":%" PRIu32
            ",\"operationId\":%" PRIu64
            ",\"status\":\"%s\",\"tableStatus\":\"%s\",\"value\":",
            HTI_JSON_CONTRACT_VERSION,
            result->operation_id,
            hti_status_name(result->status),
            ht_status_name(result->table_status))) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }
    if (result->has_value) {
        if (!hti_json_append_format(&builder, "%" PRId64, result->value)) {
            free(builder.data);
            return HTI_NO_MEMORY;
        }
    } else if (!hti_json_append_raw(&builder, "null", 4U)) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }
    if (!hti_json_append_raw(&builder, ",\"frames\":[", 11U)) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }

    for (frame_index = 0U; frame_index < result->frame_count; ++frame_index) {
        hti_frame_view_t frame;
        hti_status_t status = hti_session_frame_by_id(
            session,
            result->first_frame_id + (uint64_t)frame_index,
            &frame);
        if (status != HTI_OK) {
            free(builder.data);
            return status;
        }
        if ((frame_index != 0U &&
             !hti_json_append_raw(&builder, ",", 1U)) ||
            !hti_json_append_frame(&builder, &frame)) {
            free(builder.data);
            return HTI_NO_MEMORY;
        }
    }
    if (!hti_json_append_raw(&builder, "]}", 2U)) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }

    *out_json = builder.data;
    *out_size = builder.length;
    return HTI_OK;
}

hti_status_t hti_timeline_json(
    const hti_session_t *session,
    char **out_json,
    size_t *out_size)
{
    hti_json_builder_t builder = {0};
    size_t frame_count;
    size_t frame_index;

    if (session == NULL || out_json == NULL || out_size == NULL) {
        return HTI_INVALID_ARGUMENT;
    }
    *out_json = NULL;
    *out_size = 0U;
    frame_count = hti_session_timeline_count(session);

    if (!hti_json_append_format(
            &builder,
            "{\"version\":%" PRIu32 ",\"frames\":[",
            HTI_JSON_CONTRACT_VERSION)) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }
    for (frame_index = 0U; frame_index < frame_count; ++frame_index) {
        hti_frame_view_t frame;
        hti_status_t status = hti_session_frame_at(
            session,
            frame_index,
            &frame);
        if (status != HTI_OK) {
            free(builder.data);
            return status;
        }
        if ((frame_index != 0U &&
             !hti_json_append_raw(&builder, ",", 1U)) ||
            !hti_json_append_frame(&builder, &frame)) {
            free(builder.data);
            return HTI_NO_MEMORY;
        }
    }
    if (!hti_json_append_raw(&builder, "]}", 2U)) {
        free(builder.data);
        return HTI_NO_MEMORY;
    }

    *out_json = builder.data;
    *out_size = builder.length;
    return HTI_OK;
}

void hti_json_free(char *json)
{
    free(json);
}
