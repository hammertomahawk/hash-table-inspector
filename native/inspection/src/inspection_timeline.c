#include "hti/inspection.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint64_t frame_id;
    inspect_event_t event;
    void *snapshot;
    inspect_snapshot_release_fn release;
} inspect_frame_t;

struct inspect_timeline {
    inspect_frame_t frames[INSPECT_TIMELINE_MAX_FRAMES];
    size_t head;
    size_t count;
    uint64_t next_frame_id;
};

static void inspect_release_frame(inspect_frame_t *frame)
{
    if (frame->snapshot != NULL && frame->release != NULL) {
        frame->release(frame->snapshot);
    }
    *frame = (inspect_frame_t){0};
}

inspect_status_t inspect_timeline_create(inspect_timeline_t **out_timeline)
{
    inspect_timeline_t *timeline;

    if (out_timeline == NULL) {
        return INSPECT_INVALID_ARGUMENT;
    }
    *out_timeline = NULL;
    timeline = calloc(1U, sizeof(*timeline));
    if (timeline == NULL) {
        return INSPECT_NO_MEMORY;
    }
    timeline->next_frame_id = UINT64_C(1);
    *out_timeline = timeline;
    return INSPECT_OK;
}

void inspect_timeline_destroy(inspect_timeline_t *timeline)
{
    size_t index;

    if (timeline == NULL) {
        return;
    }
    for (index = 0U; index < timeline->count; ++index) {
        size_t slot = (timeline->head + index) % INSPECT_TIMELINE_MAX_FRAMES;
        inspect_release_frame(&timeline->frames[slot]);
    }
    free(timeline);
}

inspect_status_t inspect_timeline_append(
    inspect_timeline_t *timeline,
    const inspect_event_t *event,
    inspect_owned_snapshot_t *snapshot,
    uint64_t *out_frame_id)
{
    size_t slot;
    inspect_frame_t *frame;

    if (timeline == NULL || event == NULL || snapshot == NULL ||
        snapshot->data == NULL || snapshot->release == NULL ||
        out_frame_id == NULL) {
        return INSPECT_INVALID_ARGUMENT;
    }
    if (timeline->next_frame_id == UINT64_MAX) {
        return INSPECT_OVERFLOW;
    }

    if (timeline->count == INSPECT_TIMELINE_MAX_FRAMES) {
        inspect_release_frame(&timeline->frames[timeline->head]);
        timeline->head = (timeline->head + 1U) % INSPECT_TIMELINE_MAX_FRAMES;
        --timeline->count;
    }

    slot = (timeline->head + timeline->count) % INSPECT_TIMELINE_MAX_FRAMES;
    frame = &timeline->frames[slot];
    frame->frame_id = timeline->next_frame_id;
    frame->event = *event;
    frame->event.sequence = timeline->next_frame_id;
    frame->snapshot = snapshot->data;
    frame->release = snapshot->release;
    *out_frame_id = frame->frame_id;
    ++timeline->next_frame_id;
    ++timeline->count;
    snapshot->data = NULL;
    snapshot->release = NULL;
    return INSPECT_OK;
}

size_t inspect_timeline_count(const inspect_timeline_t *timeline)
{
    return timeline == NULL ? 0U : timeline->count;
}

inspect_status_t inspect_timeline_get_at(
    const inspect_timeline_t *timeline,
    size_t index,
    inspect_frame_view_t *out_frame)
{
    size_t slot;
    const inspect_frame_t *frame;

    if (timeline == NULL || out_frame == NULL) {
        return INSPECT_INVALID_ARGUMENT;
    }
    if (index >= timeline->count) {
        return INSPECT_NOT_FOUND;
    }
    slot = (timeline->head + index) % INSPECT_TIMELINE_MAX_FRAMES;
    frame = &timeline->frames[slot];
    *out_frame = (inspect_frame_view_t){
        .frame_id = frame->frame_id,
        .event = frame->event,
        .snapshot = frame->snapshot
    };
    return INSPECT_OK;
}

inspect_status_t inspect_timeline_get_by_id(
    const inspect_timeline_t *timeline,
    uint64_t frame_id,
    inspect_frame_view_t *out_frame)
{
    size_t index;

    if (timeline == NULL || out_frame == NULL) {
        return INSPECT_INVALID_ARGUMENT;
    }
    for (index = 0U; index < timeline->count; ++index) {
        inspect_frame_view_t frame;
        inspect_status_t status = inspect_timeline_get_at(timeline, index, &frame);
        if (status != INSPECT_OK) {
            return status;
        }
        if (frame.frame_id == frame_id) {
            *out_frame = frame;
            return INSPECT_OK;
        }
    }
    return INSPECT_NOT_FOUND;
}

const char *inspect_status_name(inspect_status_t status)
{
    switch (status) {
    case INSPECT_OK:
        return "ok";
    case INSPECT_INVALID_ARGUMENT:
        return "invalid_argument";
    case INSPECT_NO_MEMORY:
        return "no_memory";
    case INSPECT_NOT_FOUND:
        return "not_found";
    case INSPECT_OVERFLOW:
        return "overflow";
    }
    return "unknown";
}
