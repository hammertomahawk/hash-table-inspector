#include <jni.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hti/hash_inspector.h"

typedef enum {
    HTI_JNI_PUT = 1,
    HTI_JNI_LOOKUP,
    HTI_JNI_DELETE
} hti_jni_key_operation_t;

static void hti_jni_throw(
    JNIEnv *environment,
    const char *class_name,
    const char *message)
{
    jclass exception_class = (*environment)->FindClass(
        environment,
        class_name);

    if (exception_class != NULL) {
        (void)(*environment)->ThrowNew(
            environment,
            exception_class,
            message);
        (*environment)->DeleteLocalRef(environment, exception_class);
    }
}

static void hti_jni_throw_status(
    JNIEnv *environment,
    hti_status_t status)
{
    hti_jni_throw(
        environment,
        "java/lang/IllegalStateException",
        hti_status_name(status));
}

static hti_session_t *hti_jni_session_from_handle(
    JNIEnv *environment,
    jlong handle)
{
    if (handle == (jlong)0) {
        hti_jni_throw(
            environment,
            "java/lang/IllegalStateException",
            "native session is closed");
        return NULL;
    }
    return (hti_session_t *)(intptr_t)handle;
}

static jbyteArray hti_jni_copy_json(
    JNIEnv *environment,
    char *json,
    size_t json_size)
{
    jbyteArray bytes;
    jsize byte_count;

    if (json == NULL || json_size > (size_t)INT32_MAX) {
        hti_json_free(json);
        hti_jni_throw(
            environment,
            "java/lang/IllegalStateException",
            "native JSON result is too large");
        return NULL;
    }
    byte_count = (jsize)json_size;
    bytes = (*environment)->NewByteArray(environment, byte_count);
    if (bytes == NULL) {
        hti_json_free(json);
        return NULL;
    }
    if (byte_count != (jsize)0) {
        (*environment)->SetByteArrayRegion(
            environment,
            bytes,
            (jsize)0,
            byte_count,
            (const jbyte *)json);
        if ((*environment)->ExceptionCheck(environment) == JNI_TRUE) {
            hti_json_free(json);
            return NULL;
        }
    }
    hti_json_free(json);
    return bytes;
}

static jbyteArray hti_jni_encode_operation(
    JNIEnv *environment,
    const hti_session_t *session,
    const hti_operation_result_t *result)
{
    char *json = NULL;
    size_t json_size = 0U;
    hti_status_t status = hti_operation_result_json(
        session,
        result,
        &json,
        &json_size);

    if (status != HTI_OK) {
        hti_jni_throw_status(environment, status);
        return NULL;
    }
    return hti_jni_copy_json(environment, json, json_size);
}

static bool hti_jni_copy_key(
    JNIEnv *environment,
    jbyteArray key,
    unsigned char buffer[HTI_DEMO_KEY_MAX_BYTES],
    size_t *out_key_size)
{
    jsize key_size;

    if (key == NULL) {
        hti_jni_throw(
            environment,
            "java/lang/NullPointerException",
            "key");
        return false;
    }
    key_size = (*environment)->GetArrayLength(environment, key);
    if ((*environment)->ExceptionCheck(environment) == JNI_TRUE) {
        return false;
    }
    *out_key_size = (size_t)key_size;
    if (key_size > (jsize)0 &&
        *out_key_size <= HTI_DEMO_KEY_MAX_BYTES) {
        (*environment)->GetByteArrayRegion(
            environment,
            key,
            (jsize)0,
            key_size,
            (jbyte *)buffer);
        if ((*environment)->ExceptionCheck(environment) == JNI_TRUE) {
            return false;
        }
    }
    return true;
}

static jbyteArray hti_jni_run_key_operation(
    JNIEnv *environment,
    jlong handle,
    jbyteArray key,
    jlong value,
    hti_jni_key_operation_t operation)
{
    hti_session_t *session = hti_jni_session_from_handle(
        environment,
        handle);
    unsigned char key_bytes[HTI_DEMO_KEY_MAX_BYTES] = {0};
    size_t key_size = 0U;
    hti_operation_result_t result;

    if (session == NULL ||
        !hti_jni_copy_key(environment, key, key_bytes, &key_size)) {
        return NULL;
    }

    switch (operation) {
    case HTI_JNI_PUT:
        (void)hti_session_put(
            session,
            key_bytes,
            key_size,
            (int64_t)value,
            &result);
        break;
    case HTI_JNI_LOOKUP:
        (void)hti_session_lookup(
            session,
            key_bytes,
            key_size,
            &result);
        break;
    case HTI_JNI_DELETE:
        (void)hti_session_delete(
            session,
            key_bytes,
            key_size,
            &result);
        break;
    }
    return hti_jni_encode_operation(environment, session, &result);
}

static jbyteArray hti_jni_run_session_operation(
    JNIEnv *environment,
    jlong handle,
    hti_jni_key_operation_t operation)
{
    hti_session_t *session = hti_jni_session_from_handle(
        environment,
        handle);
    hti_operation_result_t result;

    if (session == NULL) {
        return NULL;
    }
    switch (operation) {
    case HTI_JNI_PUT:
        (void)hti_session_validate(session, &result);
        break;
    case HTI_JNI_LOOKUP:
        (void)hti_session_corrupt_size(session, &result);
        break;
    case HTI_JNI_DELETE:
        (void)hti_session_restore(session, &result);
        break;
    }
    return hti_jni_encode_operation(environment, session, &result);
}

JNIEXPORT jlong JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_create(
    JNIEnv *environment,
    jobject receiver,
    jlong initial_capacity,
    jlong hash_seed);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_put(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key,
    jlong value);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_lookup(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_delete(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_validate(
    JNIEnv *environment,
    jobject receiver,
    jlong handle);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_corruptSize(
    JNIEnv *environment,
    jobject receiver,
    jlong handle);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_restore(
    JNIEnv *environment,
    jobject receiver,
    jlong handle);

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_timeline(
    JNIEnv *environment,
    jobject receiver,
    jlong handle);

JNIEXPORT void JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_close(
    JNIEnv *environment,
    jobject receiver,
    jlong handle);

JNIEXPORT jlong JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_create(
    JNIEnv *environment,
    jobject receiver,
    jlong initial_capacity,
    jlong hash_seed)
{
    hti_session_t *session = NULL;
    hti_status_t status;

    (void)receiver;
    if (initial_capacity <= (jlong)0) {
        hti_jni_throw(
            environment,
            "java/lang/IllegalArgumentException",
            "initial capacity must be positive");
        return (jlong)0;
    }
    status = hti_session_create(
        (size_t)initial_capacity,
        (uint64_t)hash_seed,
        &session);
    if (status != HTI_OK) {
        hti_jni_throw_status(environment, status);
        return (jlong)0;
    }
    return (jlong)(intptr_t)session;
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_put(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key,
    jlong value)
{
    (void)receiver;
    return hti_jni_run_key_operation(
        environment,
        handle,
        key,
        value,
        HTI_JNI_PUT);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_lookup(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key)
{
    (void)receiver;
    return hti_jni_run_key_operation(
        environment,
        handle,
        key,
        (jlong)0,
        HTI_JNI_LOOKUP);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_delete(
    JNIEnv *environment,
    jobject receiver,
    jlong handle,
    jbyteArray key)
{
    (void)receiver;
    return hti_jni_run_key_operation(
        environment,
        handle,
        key,
        (jlong)0,
        HTI_JNI_DELETE);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_validate(
    JNIEnv *environment,
    jobject receiver,
    jlong handle)
{
    (void)receiver;
    return hti_jni_run_session_operation(
        environment,
        handle,
        HTI_JNI_PUT);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_corruptSize(
    JNIEnv *environment,
    jobject receiver,
    jlong handle)
{
    (void)receiver;
    return hti_jni_run_session_operation(
        environment,
        handle,
        HTI_JNI_LOOKUP);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_restore(
    JNIEnv *environment,
    jobject receiver,
    jlong handle)
{
    (void)receiver;
    return hti_jni_run_session_operation(
        environment,
        handle,
        HTI_JNI_DELETE);
}

JNIEXPORT jbyteArray JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_timeline(
    JNIEnv *environment,
    jobject receiver,
    jlong handle)
{
    hti_session_t *session = hti_jni_session_from_handle(
        environment,
        handle);
    char *json = NULL;
    size_t json_size = 0U;
    hti_status_t status;

    (void)receiver;
    if (session == NULL) {
        return NULL;
    }
    status = hti_timeline_json(session, &json, &json_size);
    if (status != HTI_OK) {
        hti_jni_throw_status(environment, status);
        return NULL;
    }
    return hti_jni_copy_json(environment, json, json_size);
}

JNIEXPORT void JNICALL
Java_com_hammertomahawk_hashtableinspector_NativeBridge_close(
    JNIEnv *environment,
    jobject receiver,
    jlong handle)
{
    hti_session_t *session = hti_jni_session_from_handle(
        environment,
        handle);

    (void)receiver;
    if (session != NULL) {
        hti_session_destroy(session);
    }
}
