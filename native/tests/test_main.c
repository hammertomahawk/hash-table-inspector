#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hti/hash_inspector.h"
#include "hti/hash_table.h"
#include "hti/inspection.h"
#include "hash_table_internal.h"

#define TEST_HASH_SEED UINT64_C(0x0123456789abcdef)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            (void)fprintf(                                                   \
                stderr,                                                      \
                "CHECK failed at %s:%d: %s\n",                              \
                __FILE__,                                                    \
                __LINE__,                                                    \
                #condition);                                                 \
            return false;                                                    \
        }                                                                    \
    } while (false)

typedef bool (*test_function_t)(void);

typedef struct {
    const char *name;
    test_function_t function;
} test_case_t;

static hti_status_t put_text(
    hti_session_t *session,
    const char *key,
    int64_t value,
    hti_operation_result_t *result)
{
    return hti_session_put(session, key, strlen(key), value, result);
}

static hti_status_t lookup_text(
    hti_session_t *session,
    const char *key,
    hti_operation_result_t *result)
{
    return hti_session_lookup(session, key, strlen(key), result);
}

static hti_status_t delete_text(
    hti_session_t *session,
    const char *key,
    hti_operation_result_t *result)
{
    return hti_session_delete(session, key, strlen(key), result);
}

static bool latest_frame(
    const hti_session_t *session,
    hti_frame_view_t *out_frame)
{
    size_t count = hti_session_timeline_count(session);
    return count != 0U &&
        hti_session_frame_at(session, count - 1U, out_frame) == HTI_OK;
}

static bool snapshot_entry_for_key(
    const hti_snapshot_t *snapshot,
    const char *key,
    hti_entry_view_t *out_entry)
{
    hti_snapshot_info_t info;
    size_t index;
    size_t key_size = strlen(key);

    if (hti_snapshot_info(snapshot, &info) != HTI_OK) {
        return false;
    }
    for (index = 0U; index < info.entry_count; ++index) {
        hti_entry_view_t entry;
        if (hti_snapshot_entry(snapshot, index, &entry) != HTI_OK) {
            return false;
        }
        if (entry.key_size == key_size &&
            memcmp(entry.key, key, key_size) == 0) {
            *out_entry = entry;
            return true;
        }
    }
    return false;
}

static bool test_core_owned_binary_keys(void)
{
    const unsigned char key[] = {0U, (unsigned char)'x', 0xffU};
    ht_config_t config = {
        .struct_size = sizeof(config),
        .initial_capacity = 4U,
        .hash_seed = TEST_HASH_SEED
    };
    ht_t *table = NULL;
    ht_put_result_t put_result;
    int64_t value = 0;
    int64_t removed = 0;

    CHECK(ht_create(&config, &table) == HT_OK);
    CHECK(ht_capacity(table) == 4U);
    CHECK(ht_resize_threshold(table) == 3U);
    CHECK(ht_put(table, key, sizeof(key), INT64_C(11), &put_result) == HT_OK);
    CHECK(put_result == HT_PUT_INSERTED);
    CHECK(ht_put(table, key, sizeof(key), INT64_C(12), &put_result) == HT_OK);
    CHECK(put_result == HT_PUT_UPDATED);
    CHECK(ht_size(table) == 1U);
    CHECK(ht_get(table, key, sizeof(key), &value) == HT_OK);
    CHECK(value == INT64_C(12));
    CHECK(ht_remove(table, key, sizeof(key), &removed) == HT_OK);
    CHECK(removed == INT64_C(12));
    CHECK(ht_get(table, key, sizeof(key), &value) == HT_NOT_FOUND);
    CHECK(ht_size(table) == 0U);
    ht_destroy(table);
    return true;
}

static bool test_insert_update_lookup_delete(void)
{
    hti_session_t *session = NULL;
    hti_operation_result_t inserted;
    hti_operation_result_t result;
    hti_frame_view_t frame;
    hti_entry_view_t entry;
    hti_snapshot_info_t info;

    CHECK(hti_session_create(4U, TEST_HASH_SEED, &session) == HTI_OK);
    CHECK(hti_session_timeline_count(session) == 1U);

    CHECK(put_text(session, "alpha", INT64_C(10), &inserted) == HTI_OK);
    CHECK(inserted.table_status == HT_OK);
    CHECK(inserted.event_code == HTI_EVENT_PUT_INSERTED);
    CHECK(inserted.frame_count == 1U);
    CHECK(inserted.has_value && inserted.value == INT64_C(10));

    CHECK(put_text(session, "alpha", INT64_C(20), &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_PUT_UPDATED);
    CHECK(result.frame_count == 1U);
    CHECK(hti_session_frame_by_id(
        session,
        inserted.first_frame_id,
        &frame) == HTI_OK);
    CHECK(snapshot_entry_for_key(frame.snapshot, "alpha", &entry));
    CHECK(entry.value == INT64_C(10));

    CHECK(lookup_text(session, "alpha", &result) == HTI_OK);
    CHECK(result.table_status == HT_OK);
    CHECK(result.event_code == HTI_EVENT_LOOKUP_HIT);
    CHECK(result.has_value && result.value == INT64_C(20));

    CHECK(lookup_text(session, "missing", &result) == HTI_OK);
    CHECK(result.table_status == HT_NOT_FOUND);
    CHECK(result.event_code == HTI_EVENT_LOOKUP_MISS);
    CHECK(!result.has_value);

    CHECK(delete_text(session, "alpha", &result) == HTI_OK);
    CHECK(result.table_status == HT_OK);
    CHECK(result.event_code == HTI_EVENT_DELETE_HIT);
    CHECK(result.has_value && result.value == INT64_C(20));
    CHECK(latest_frame(session, &frame));
    CHECK(hti_snapshot_info(frame.snapshot, &info) == HTI_OK);
    CHECK(info.reported_size == 0U);
    CHECK(info.entry_count == 0U);
    CHECK(info.valid);

    CHECK(delete_text(session, "alpha", &result) == HTI_OK);
    CHECK(result.table_status == HT_NOT_FOUND);
    CHECK(result.event_code == HTI_EVENT_DELETE_MISS);

    hti_session_destroy(session);
    return true;
}

static bool find_collision_pair(const char **out_first, const char **out_second)
{
    static const char *const candidates[] = {
        "a", "b", "c", "d", "e", "f", "g", "h",
        "i", "j", "k", "l", "m", "n", "o", "p"
    };
    size_t first;
    size_t second;

    for (first = 0U; first < sizeof(candidates) / sizeof(candidates[0]); ++first) {
        uint64_t first_hash = ht_internal_hash_key(
            TEST_HASH_SEED,
            candidates[first],
            strlen(candidates[first]));
        for (second = first + 1U;
             second < sizeof(candidates) / sizeof(candidates[0]);
             ++second) {
            uint64_t second_hash = ht_internal_hash_key(
                TEST_HASH_SEED,
                candidates[second],
                strlen(candidates[second]));
            if ((first_hash & UINT64_C(3)) ==
                (second_hash & UINT64_C(3))) {
                *out_first = candidates[first];
                *out_second = candidates[second];
                return true;
            }
        }
    }
    return false;
}

static bool test_deterministic_collision(void)
{
    const char *first = NULL;
    const char *second = NULL;
    hti_session_t *session = NULL;
    hti_operation_result_t result;
    hti_frame_view_t frame;
    hti_snapshot_info_t info;
    hti_bucket_view_t bucket;
    hti_entry_view_t entry;

    CHECK(find_collision_pair(&first, &second));
    CHECK(hti_session_create(4U, TEST_HASH_SEED, &session) == HTI_OK);
    CHECK(put_text(session, first, INT64_C(1), &result) == HTI_OK);
    CHECK(put_text(session, second, INT64_C(2), &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_PUT_INSERTED);
    CHECK(latest_frame(session, &frame));
    CHECK(frame.data[1] == UINT64_C(1));
    CHECK(hti_snapshot_info(frame.snapshot, &info) == HTI_OK);
    CHECK(info.capacity == 4U);
    CHECK(info.entry_count == 2U);
    CHECK(hti_snapshot_entry(frame.snapshot, 0U, &entry) == HTI_OK);
    CHECK(hti_snapshot_bucket(
        frame.snapshot,
        entry.bucket_index,
        &bucket) == HTI_OK);
    CHECK(bucket.chain_length == 2U);
    CHECK(info.valid);

    (void)printf(
        "    deterministic demo collision: %s / %s\n",
        first,
        second);
    hti_session_destroy(session);
    return true;
}

static bool test_four_to_eight_rehash_and_json(void)
{
    static const char *const keys[] = {"one", "two", "three", "four"};
    hti_session_t *session = NULL;
    hti_operation_result_t result;
    hti_frame_view_t before;
    hti_frame_view_t after;
    hti_frame_view_t inserted;
    hti_snapshot_info_t info;
    hti_entry_view_t before_entry;
    hti_entry_view_t after_entry;
    char *json = NULL;
    size_t json_size = 0U;
    size_t index;

    CHECK(hti_session_create(4U, TEST_HASH_SEED, &session) == HTI_OK);
    for (index = 0U; index < 3U; ++index) {
        CHECK(put_text(session, keys[index], (int64_t)index, &result) == HTI_OK);
        CHECK(result.frame_count == 1U);
    }

    CHECK(put_text(session, keys[3], INT64_C(3), &result) == HTI_OK);
    CHECK(result.frame_count == 3U);
    CHECK(result.event_code == HTI_EVENT_PUT_INSERTED);
    CHECK(hti_session_frame_by_id(
        session,
        result.first_frame_id,
        &before) == HTI_OK);
    CHECK(hti_session_frame_by_id(
        session,
        result.first_frame_id + UINT64_C(1),
        &after) == HTI_OK);
    CHECK(hti_session_frame_by_id(
        session,
        result.first_frame_id + UINT64_C(2),
        &inserted) == HTI_OK);

    CHECK(before.event_code == HTI_EVENT_REHASH_BEGIN);
    CHECK(before.data[0] == UINT64_C(4));
    CHECK(before.data[1] == UINT64_C(8));
    CHECK(hti_snapshot_info(before.snapshot, &info) == HTI_OK);
    CHECK(info.capacity == 4U && info.entry_count == 3U && info.valid);

    CHECK(after.event_code == HTI_EVENT_REHASH_END);
    CHECK(after.data[2] == UINT64_C(3));
    CHECK(hti_snapshot_info(after.snapshot, &info) == HTI_OK);
    CHECK(info.capacity == 8U && info.entry_count == 3U && info.valid);

    CHECK(inserted.event_code == HTI_EVENT_PUT_INSERTED);
    CHECK(hti_snapshot_info(inserted.snapshot, &info) == HTI_OK);
    CHECK(info.capacity == 8U && info.entry_count == 4U && info.valid);

    for (index = 0U; index < 3U; ++index) {
        CHECK(snapshot_entry_for_key(before.snapshot, keys[index], &before_entry));
        CHECK(snapshot_entry_for_key(after.snapshot, keys[index], &after_entry));
        CHECK(before_entry.entry_id == after_entry.entry_id);
    }

    CHECK(hti_operation_result_json(
        session,
        &result,
        &json,
        &json_size) == HTI_OK);
    CHECK(json != NULL);
    CHECK(json_size == strlen(json));
    CHECK(strstr(json, "\"version\":1") != NULL);
    CHECK(strstr(json, "\"event\":\"rehash_begin\"") != NULL);
    CHECK(strstr(json, "\"event\":\"rehash_end\"") != NULL);
    CHECK(strstr(json, "\"capacity\":8") != NULL);
    CHECK(strstr(json, "\"key\":\"four\"") != NULL);
    hti_json_free(json);

    CHECK(hti_timeline_json(session, &json, &json_size) == HTI_OK);
    CHECK(json != NULL);
    CHECK(json_size == strlen(json));
    CHECK(strstr(json, "\"version\":1") != NULL);
    CHECK(strstr(json, "\"event\":\"session_created\"") != NULL);
    CHECK(strstr(json, "\"event\":\"rehash_begin\"") != NULL);
    CHECK(strstr(json, "\"event\":\"rehash_end\"") != NULL);
    CHECK(strstr(json, "\"key\":\"four\"") != NULL);
    hti_json_free(json);

    CHECK(!hti_demo_key_is_valid("bad key", strlen("bad key")));
    CHECK(hti_session_put(
        session,
        "bad key",
        strlen("bad key"),
        INT64_C(1),
        &result) == HTI_INVALID_ARGUMENT);

    hti_session_destroy(session);
    return true;
}

static bool test_size_corruption_detection_and_restore(void)
{
    hti_session_t *session = NULL;
    hti_operation_result_t result;
    hti_frame_view_t frame;
    hti_snapshot_info_t info;

    CHECK(hti_session_create(4U, TEST_HASH_SEED, &session) == HTI_OK);
    CHECK(put_text(session, "alpha", INT64_C(42), &result) == HTI_OK);

    CHECK(hti_session_validate(session, &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_VALIDATION_PASSED);
    CHECK(latest_frame(session, &frame));
    CHECK(hti_snapshot_info(frame.snapshot, &info) == HTI_OK);
    CHECK(info.valid);

    CHECK(hti_session_corrupt_size(session, &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_SIZE_CORRUPTION_APPLIED);
    CHECK(hti_session_is_quarantined(session));
    CHECK(latest_frame(session, &frame));
    CHECK(hti_snapshot_info(frame.snapshot, &info) == HTI_OK);
    CHECK(!info.valid);
    CHECK(info.reported_size == 2U);
    CHECK(info.entry_count == 1U);
    CHECK(info.first_violation == HTI_INVARIANT_SIZE_MISMATCH);

    CHECK(hti_session_validate(session, &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_VALIDATION_FAILED);
    CHECK(lookup_text(session, "alpha", &result) == HTI_QUARANTINED);
    CHECK(result.status == HTI_QUARANTINED);

    CHECK(hti_session_restore(session, &result) == HTI_OK);
    CHECK(result.event_code == HTI_EVENT_CLEAN_STATE_RESTORED);
    CHECK(!hti_session_is_quarantined(session));
    CHECK(latest_frame(session, &frame));
    CHECK(hti_snapshot_info(frame.snapshot, &info) == HTI_OK);
    CHECK(info.valid);
    CHECK(info.reported_size == 1U && info.entry_count == 1U);

    CHECK(lookup_text(session, "alpha", &result) == HTI_OK);
    CHECK(result.table_status == HT_OK);
    CHECK(result.has_value && result.value == INT64_C(42));

    hti_session_destroy(session);
    return true;
}

static bool test_timeline_is_bounded_to_64_full_frames(void)
{
    hti_session_t *session = NULL;
    hti_operation_result_t result;
    hti_frame_view_t first;
    hti_frame_view_t last;
    hti_snapshot_info_t info;
    size_t index;

    CHECK(hti_session_create(4U, TEST_HASH_SEED, &session) == HTI_OK);
    for (index = 0U; index < 70U; ++index) {
        CHECK(lookup_text(session, "absent", &result) == HTI_OK);
        CHECK(result.table_status == HT_NOT_FOUND);
        CHECK(result.frame_count == 1U);
    }
    CHECK(hti_session_timeline_count(session) == INSPECT_TIMELINE_MAX_FRAMES);
    CHECK(hti_session_frame_at(session, 0U, &first) == HTI_OK);
    CHECK(hti_session_frame_at(
        session,
        INSPECT_TIMELINE_MAX_FRAMES - 1U,
        &last) == HTI_OK);
    CHECK(first.frame_id == UINT64_C(8));
    CHECK(last.frame_id == UINT64_C(71));
    CHECK(first.snapshot != NULL && last.snapshot != NULL);
    CHECK(hti_snapshot_info(last.snapshot, &info) == HTI_OK);
    CHECK(info.valid && info.entry_count == 0U && info.capacity == 4U);

    hti_session_destroy(session);
    return true;
}

int main(void)
{
    static const test_case_t tests[] = {
        {"core owns arbitrary byte keys", test_core_owned_binary_keys},
        {"insert/update, lookup hit/miss, and delete", test_insert_update_lookup_delete},
        {"deterministic collision snapshot", test_deterministic_collision},
        {"stable 4-to-8 rehash frames and JSON", test_four_to_eight_rehash_and_json},
        {"size corruption detection and clean restore", test_size_corruption_detection_and_restore},
        {"timeline retains at most 64 full frames", test_timeline_is_bounded_to_64_full_frames}
    };
    size_t index;
    size_t passed = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].function()) {
            (void)fprintf(stderr, "not ok - %s\n", tests[index].name);
            return EXIT_FAILURE;
        }
        ++passed;
        (void)printf("ok - %s\n", tests[index].name);
    }
    (void)printf("%zu native tests passed\n", passed);
    return EXIT_SUCCESS;
}
