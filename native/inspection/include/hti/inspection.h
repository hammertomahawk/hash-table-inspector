#ifndef HTI_INSPECTION_H
#define HTI_INSPECTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INSPECT_TIMELINE_MAX_FRAMES ((size_t)64)
#define INSPECT_DOMAIN_HASH_TABLE UINT32_C(1)

typedef struct inspect_timeline inspect_timeline_t;

typedef enum {
    INSPECT_OK = 0,
    INSPECT_INVALID_ARGUMENT,
    INSPECT_NO_MEMORY,
    INSPECT_NOT_FOUND,
    INSPECT_OVERFLOW
} inspect_status_t;

typedef struct {
    uint64_t sequence;
    uint64_t operation_id;
    uint32_t domain;
    uint32_t code;
    int32_t status;
    uint32_t flags;
    uint64_t subject_id;
    uint64_t data[4];
} inspect_event_t;

typedef void (*inspect_snapshot_release_fn)(void *snapshot);

typedef struct {
    void *data;
    inspect_snapshot_release_fn release;
} inspect_owned_snapshot_t;

typedef struct {
    uint64_t frame_id;
    inspect_event_t event;
    const void *snapshot;
} inspect_frame_view_t;

inspect_status_t inspect_timeline_create(inspect_timeline_t **out_timeline);
void inspect_timeline_destroy(inspect_timeline_t *timeline);

inspect_status_t inspect_timeline_append(
    inspect_timeline_t *timeline,
    const inspect_event_t *event,
    inspect_owned_snapshot_t *snapshot,
    uint64_t *out_frame_id);

size_t inspect_timeline_count(const inspect_timeline_t *timeline);

inspect_status_t inspect_timeline_get_at(
    const inspect_timeline_t *timeline,
    size_t index,
    inspect_frame_view_t *out_frame);

inspect_status_t inspect_timeline_get_by_id(
    const inspect_timeline_t *timeline,
    uint64_t frame_id,
    inspect_frame_view_t *out_frame);

const char *inspect_status_name(inspect_status_t status);

#ifdef __cplusplus
}
#endif

#endif
