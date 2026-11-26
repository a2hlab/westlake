/*
 * camerametadata_natives.c (libwl_missing_natives.so)
 *
 * JNI bindings for android.hardware.camera2.impl.CameraMetadataNative: the container every camera2
 * characteristic, request and result travels in (CameraCharacteristics, CaptureRequest, CaptureResult).
 * Westlake's runtime registered none of its natives, so the class's first use failed ("No implementation
 * found for ... CameraMetadataNative.nativeAllocate") and no camera could be described to an app.
 *
 * The natives keep AOSP's semantics (frameworks/base/core/jni/android_hardware_camera2_CameraMetadata.cpp over
 * frameworks/av's CameraMetadata), written directly over AOSP's C library (third_party/aosp-camera-metadata):
 * - The Java object holds a pointer to a holder whose buffer may be NULL (an empty CameraMetadata has none).
 * - nativeReadValues returns the entry's raw bytes, or null when the tag is absent; nativeWriteValues replaces
 *   the entry (null src deletes it), growing the buffer as CameraMetadata::update does.
 * - Key names map to tags by section ("android.lens.info" + "minimumFocusDistance"), as
 *   CameraMetadata::getTagFromName; an unknown key or tag throws IllegalArgumentException.
 * - There are no vendor tags: the descriptor setup succeeds, nativeGetAllVendorKeys answers null, as AOSP does
 *   without a vendor tag descriptor.
 * - Parcels carry the metadata buffer as one byte array. Westlake's camera service runs in the app's process,
 *   so a CameraMetadataNative crosses no process boundary; the two parcel natives only have to agree.
 */

#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <system/camera_metadata.h>
#include <camera_metadata_hidden.h>

typedef struct {
    camera_metadata_t *buffer;   /* NULL: empty */
} wl_metadata;

static wl_metadata *holder(jlong ptr) { return (wl_metadata *) (intptr_t) ptr; }

static void throw_named(JNIEnv *env, const char *cls, const char *message)
{
    jclass c = (*env)->FindClass(env, cls);
    if (c != NULL) (*env)->ThrowNew(env, c, message);
}

static int check(JNIEnv *env, jlong ptr)
{
    if (ptr == 0) {
        throw_named(env, "java/lang/IllegalStateException", "Metadata object is null");
        return 0;
    }
    return 1;
}

static jlong CameraMetadata_allocate(JNIEnv *env, jclass clazz)
{
    (void) clazz;
    wl_metadata *m = calloc(1, sizeof(*m));
    if (m == NULL) throw_named(env, "java/lang/OutOfMemoryError", "camera metadata");
    return (jlong) (intptr_t) m;
}

static jlong CameraMetadata_allocateCopy(JNIEnv *env, jclass clazz, jlong other)
{
    (void) clazz;
    if (!check(env, other)) return 0;
    wl_metadata *m = calloc(1, sizeof(*m));
    if (m == NULL) {
        throw_named(env, "java/lang/OutOfMemoryError", "camera metadata");
        return 0;
    }
    if (holder(other)->buffer != NULL) {
        m->buffer = clone_camera_metadata(holder(other)->buffer);
        if (m->buffer == NULL) {
            free(m);
            throw_named(env, "java/lang/OutOfMemoryError", "camera metadata copy");
            return 0;
        }
    }
    return (jlong) (intptr_t) m;
}

static void CameraMetadata_close(JNIEnv *env, jclass clazz, jlong ptr)
{
    (void) env; (void) clazz;
    wl_metadata *m = holder(ptr);
    if (m == NULL) return;
    if (m->buffer != NULL) free_camera_metadata(m->buffer);
    free(m);
}

static jboolean CameraMetadata_isEmpty(JNIEnv *env, jclass clazz, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return JNI_TRUE;
    camera_metadata_t *b = holder(ptr)->buffer;
    return b == NULL || get_camera_metadata_entry_count(b) == 0 ? JNI_TRUE : JNI_FALSE;
}

static jint CameraMetadata_getEntryCount(JNIEnv *env, jclass clazz, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return 0;
    camera_metadata_t *b = holder(ptr)->buffer;
    return b == NULL ? 0 : (jint) get_camera_metadata_entry_count(b);
}

static jlong CameraMetadata_getBufferSize(JNIEnv *env, jclass clazz, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return 0;
    camera_metadata_t *b = holder(ptr)->buffer;
    return b == NULL ? 0 : (jlong) get_camera_metadata_size(b);
}

static void CameraMetadata_swap(JNIEnv *env, jclass clazz, jlong ptr, jlong other)
{
    (void) clazz;
    if (!check(env, ptr) || !check(env, other)) return;
    camera_metadata_t *t = holder(ptr)->buffer;
    holder(ptr)->buffer = holder(other)->buffer;
    holder(other)->buffer = t;
}

static void CameraMetadata_setVendorId(JNIEnv *env, jclass clazz, jlong ptr, jlong vendorId)
{
    (void) clazz;
    if (!check(env, ptr)) return;
    if (holder(ptr)->buffer != NULL) set_camera_metadata_vendor_id(holder(ptr)->buffer, (metadata_vendor_id_t) vendorId);
}

/* Room for one more entry with data_size bytes of data, as CameraMetadata::resizeIfNeeded. */
static int make_room(wl_metadata *m, size_t data_size)
{
    size_t entries = m->buffer ? get_camera_metadata_entry_count(m->buffer) : 0;
    size_t entry_cap = m->buffer ? get_camera_metadata_entry_capacity(m->buffer) : 0;
    size_t data = m->buffer ? get_camera_metadata_data_count(m->buffer) : 0;
    size_t data_cap = m->buffer ? get_camera_metadata_data_capacity(m->buffer) : 0;
    if (m->buffer != NULL && entries + 1 <= entry_cap && data + data_size <= data_cap) return 1;
    size_t new_entries = (entries + 1) * 2, new_data = (data + data_size) * 2;
    camera_metadata_t *grown = allocate_camera_metadata(new_entries, new_data);
    if (grown == NULL) return 0;
    if (m->buffer != NULL) {
        if (append_camera_metadata(grown, m->buffer) != 0) {
            free_camera_metadata(grown);
            return 0;
        }
        free_camera_metadata(m->buffer);
    }
    m->buffer = grown;
    return 1;
}

static int tag_type(JNIEnv *env, uint32_t tag, const camera_metadata_t *b)
{
    int type = get_local_camera_metadata_tag_type(tag, b);
    if (type < 0) {
        char message[96];
        snprintf(message, sizeof(message), "Tag (0x%x) did not have a type", tag);
        throw_named(env, "java/lang/IllegalArgumentException", message);
    }
    return type;
}

static jbyteArray CameraMetadata_readValues(JNIEnv *env, jclass clazz, jint tag, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return NULL;
    camera_metadata_t *b = holder(ptr)->buffer;
    if (tag_type(env, (uint32_t) tag, b) < 0) return NULL;
    if (b == NULL) return NULL;
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(b, (uint32_t) tag, &entry) != 0 || entry.count == 0) return NULL;
    size_t size = calculate_camera_metadata_entry_data_size(entry.type, entry.count);
    if (size == 0) size = camera_metadata_type_size[entry.type] * entry.count;   /* inline data */
    jbyteArray out = (*env)->NewByteArray(env, (jsize) size);
    if (out != NULL) (*env)->SetByteArrayRegion(env, out, 0, (jsize) size, (const jbyte *) entry.data.u8);
    return out;
}

static void CameraMetadata_writeValues(JNIEnv *env, jclass clazz, jint tag, jbyteArray src, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return;
    wl_metadata *m = holder(ptr);
    int type = tag_type(env, (uint32_t) tag, m->buffer);
    if (type < 0) return;
    if (m->buffer != NULL) {
        camera_metadata_entry_t entry;
        if (find_camera_metadata_entry(m->buffer, (uint32_t) tag, &entry) == 0) {
            delete_camera_metadata_entry(m->buffer, entry.index);
        }
    }
    if (src == NULL) return;   /* null erases the entry */
    jsize length = (*env)->GetArrayLength(env, src);
    size_t element = camera_metadata_type_size[type];
    if (element == 0 || length % (jsize) element != 0) {
        throw_named(env, "java/lang/IllegalArgumentException", "Bad data size for the tag's type");
        return;
    }
    size_t count = (size_t) length / element;
    if (!make_room(m, calculate_camera_metadata_entry_data_size((uint8_t) type, count))) {
        throw_named(env, "java/lang/OutOfMemoryError", "camera metadata");
        return;
    }
    jbyte *bytes = (*env)->GetByteArrayElements(env, src, NULL);
    if (bytes == NULL) return;
    int rc = add_camera_metadata_entry(m->buffer, (uint32_t) tag, bytes, count);
    (*env)->ReleaseByteArrayElements(env, src, bytes, JNI_ABORT);
    if (rc != 0) throw_named(env, "java/lang/IllegalArgumentException", "Could not write the tag's values");
}

static void CameraMetadata_update(JNIEnv *env, jclass clazz, jlong dst, jlong src)
{
    (void) clazz;
    if (!check(env, dst) || !check(env, src)) return;
    camera_metadata_t *s = holder(src)->buffer;
    if (s == NULL) return;
    size_t n = get_camera_metadata_entry_count(s);
    for (size_t i = 0; i < n; i++) {
        camera_metadata_ro_entry_t entry;
        if (get_camera_metadata_ro_entry(s, i, &entry) != 0) continue;
        wl_metadata *d = holder(dst);
        if (d->buffer != NULL) {
            camera_metadata_entry_t old;
            if (find_camera_metadata_entry(d->buffer, entry.tag, &old) == 0) delete_camera_metadata_entry(d->buffer, old.index);
        }
        if (!make_room(d, calculate_camera_metadata_entry_data_size(entry.type, entry.count))
                || add_camera_metadata_entry(d->buffer, entry.tag, entry.data.u8, entry.count) != 0) {
            throw_named(env, "java/lang/IllegalArgumentException", "Could not merge camera metadata");
            return;
        }
    }
}

/* "android.lens.info.minimumFocusDistance" -> section "android.lens.info", tag "minimumFocusDistance". */
static jint tag_from_key(JNIEnv *env, jstring keyName)
{
    if (keyName == NULL) {
        throw_named(env, "java/lang/NullPointerException", "keyName");
        return 0;
    }
    const char *key = (*env)->GetStringUTFChars(env, keyName, NULL);
    if (key == NULL) return 0;
    const char *dot = strrchr(key, '.');
    jint found = -1;
    if (dot != NULL) {
        size_t section_length = (size_t) (dot - key);
        for (uint32_t s = 0; s < ANDROID_SECTION_COUNT && found < 0; s++) {
            const char *section = camera_metadata_section_names[s];
            if (section == NULL || strlen(section) != section_length || strncmp(section, key, section_length) != 0) continue;
            for (uint32_t tag = camera_metadata_section_bounds[s][0]; tag < camera_metadata_section_bounds[s][1]; tag++) {
                const char *name = get_local_camera_metadata_tag_name(tag, NULL);
                if (name != NULL && strcmp(name, dot + 1) == 0) {
                    found = (jint) tag;
                    break;
                }
            }
        }
    }
    if (found < 0) {
        char message[256];
        snprintf(message, sizeof(message), "Could not find tag for key '%s')", key);
        (*env)->ReleaseStringUTFChars(env, keyName, key);
        throw_named(env, "java/lang/IllegalArgumentException", message);
        return 0;
    }
    (*env)->ReleaseStringUTFChars(env, keyName, key);
    return found;
}

static jint CameraMetadata_getTagFromKeyLocal(JNIEnv *env, jclass clazz, jlong ptr, jstring keyName)
{
    (void) clazz; (void) ptr;
    return tag_from_key(env, keyName);
}

static jint CameraMetadata_getTagFromKey(JNIEnv *env, jclass clazz, jstring keyName, jlong vendorId)
{
    (void) clazz; (void) vendorId;
    return tag_from_key(env, keyName);
}

static jint CameraMetadata_getTypeFromTagLocal(JNIEnv *env, jclass clazz, jlong ptr, jint tag)
{
    (void) clazz;
    return tag_type(env, (uint32_t) tag, ptr != 0 ? holder(ptr)->buffer : NULL);
}

static jint CameraMetadata_getTypeFromTag(JNIEnv *env, jclass clazz, jint tag, jlong vendorId)
{
    (void) clazz; (void) vendorId;
    return tag_type(env, (uint32_t) tag, NULL);
}

static jint CameraMetadata_setupGlobalVendorTagDescriptor(JNIEnv *env, jclass clazz)
{
    (void) env; (void) clazz;
    return 0;   /* OK: no vendor tags */
}

static jobject CameraMetadata_getAllVendorKeys(JNIEnv *env, jclass clazz, jlong ptr, jclass keyClass)
{
    (void) env; (void) clazz; (void) ptr; (void) keyClass;
    return NULL;   /* no vendor tag descriptor */
}

static void CameraMetadata_dump(JNIEnv *env, jclass clazz, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return;
    camera_metadata_t *b = holder(ptr)->buffer;
    fprintf(stderr, "[WL-CAMERA] metadata %zu entries, %zu bytes\n", b ? get_camera_metadata_entry_count(b) : 0,
            b ? get_camera_metadata_size(b) : 0);
}

static void CameraMetadata_writeToParcel(JNIEnv *env, jclass clazz, jobject parcel, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return;
    camera_metadata_t *b = holder(ptr)->buffer;
    size_t size = b ? get_camera_metadata_compact_size(b) : 0;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize) size);
    if (bytes == NULL) return;
    if (size > 0) {
        void *compact = malloc(size);
        if (compact == NULL) return;
        copy_camera_metadata(compact, size, b);
        (*env)->SetByteArrayRegion(env, bytes, 0, (jsize) size, (const jbyte *) compact);
        free(compact);
    }
    jclass pc = (*env)->GetObjectClass(env, parcel);
    jmethodID write = (*env)->GetMethodID(env, pc, "writeByteArray", "([B)V");
    if (write != NULL) (*env)->CallVoidMethod(env, parcel, write, bytes);
}

static void CameraMetadata_readFromParcel(JNIEnv *env, jclass clazz, jobject parcel, jlong ptr)
{
    (void) clazz;
    if (!check(env, ptr)) return;
    jclass pc = (*env)->GetObjectClass(env, parcel);
    jmethodID create = (*env)->GetMethodID(env, pc, "createByteArray", "()[B");
    if (create == NULL) return;
    jbyteArray bytes = (jbyteArray) (*env)->CallObjectMethod(env, parcel, create);
    if ((*env)->ExceptionCheck(env)) return;
    wl_metadata *m = holder(ptr);
    camera_metadata_t *read = NULL;
    jsize size = bytes ? (*env)->GetArrayLength(env, bytes) : 0;
    if (size > 0) {
        void *copy = malloc((size_t) size);
        if (copy == NULL) return;
        (*env)->GetByteArrayRegion(env, bytes, 0, size, (jbyte *) copy);
        size_t expected = (size_t) size;
        if (validate_camera_metadata_structure((const camera_metadata_t *) copy, &expected) != 0) {
            free(copy);
            throw_named(env, "java/lang/IllegalArgumentException", "Corrupt camera metadata in the parcel");
            return;
        }
        read = clone_camera_metadata((const camera_metadata_t *) copy);
        free(copy);
    }
    if (m->buffer != NULL) free_camera_metadata(m->buffer);
    m->buffer = read;
}

static const JNINativeMethod kCameraMetadataMethods[] = {
    {"nativeAllocate", "()J", (void *) CameraMetadata_allocate},
    {"nativeAllocateCopy", "(J)J", (void *) CameraMetadata_allocateCopy},
    {"nativeUpdate", "(JJ)V", (void *) CameraMetadata_update},
    {"nativeWriteToParcel", "(Landroid/os/Parcel;J)V", (void *) CameraMetadata_writeToParcel},
    {"nativeReadFromParcel", "(Landroid/os/Parcel;J)V", (void *) CameraMetadata_readFromParcel},
    {"nativeSwap", "(JJ)V", (void *) CameraMetadata_swap},
    {"nativeSetVendorId", "(JJ)V", (void *) CameraMetadata_setVendorId},
    {"nativeClose", "(J)V", (void *) CameraMetadata_close},
    {"nativeIsEmpty", "(J)Z", (void *) CameraMetadata_isEmpty},
    {"nativeGetEntryCount", "(J)I", (void *) CameraMetadata_getEntryCount},
    {"nativeGetBufferSize", "(J)J", (void *) CameraMetadata_getBufferSize},
    {"nativeReadValues", "(IJ)[B", (void *) CameraMetadata_readValues},
    {"nativeWriteValues", "(I[BJ)V", (void *) CameraMetadata_writeValues},
    {"nativeDump", "(J)V", (void *) CameraMetadata_dump},
    {"nativeGetAllVendorKeys", "(JLjava/lang/Class;)Ljava/util/ArrayList;", (void *) CameraMetadata_getAllVendorKeys},
    {"nativeGetTagFromKeyLocal", "(JLjava/lang/String;)I", (void *) CameraMetadata_getTagFromKeyLocal},
    {"nativeGetTypeFromTagLocal", "(JI)I", (void *) CameraMetadata_getTypeFromTagLocal},
    {"nativeGetTagFromKey", "(Ljava/lang/String;J)I", (void *) CameraMetadata_getTagFromKey},
    {"nativeGetTypeFromTag", "(IJ)I", (void *) CameraMetadata_getTypeFromTag},
    {"nativeSetupGlobalVendorTagDescriptor", "()I", (void *) CameraMetadata_setupGlobalVendorTagDescriptor},
};

/* Binds each native separately, as missing_natives.c's bind does. */
int wl_register_camera_metadata(JNIEnv *env)
{
    jclass clazz = (*env)->FindClass(env, "android/hardware/camera2/impl/CameraMetadataNative");
    if (clazz == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int bound = 0;
    size_t n = sizeof(kCameraMetadataMethods) / sizeof(kCameraMetadataMethods[0]);
    for (size_t i = 0; i < n; i++) {
        if ((*env)->RegisterNatives(env, clazz, &kCameraMetadataMethods[i], 1) == JNI_OK) {
            bound++;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, clazz);
    fprintf(stderr, "[WL-NATIVES] CameraMetadataNative bound %d/%zu\n", bound, n);
    fflush(stderr);
    return bound;
}
