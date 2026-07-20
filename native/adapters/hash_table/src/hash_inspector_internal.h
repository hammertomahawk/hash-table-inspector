#ifndef HTI_HASH_INSPECTOR_INTERNAL_H
#define HTI_HASH_INSPECTOR_INTERNAL_H

#include "hti/hash_inspector.h"
#include "hash_table_internal.h"

hti_status_t hti_snapshot_capture_internal(
    const ht_t *table,
    hti_snapshot_t **out_snapshot);

void hti_snapshot_release_internal(void *snapshot);

#endif
