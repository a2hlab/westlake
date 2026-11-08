/*
 * libcamera2ndk.so: the NDK camera API of a device whose camera service lists no camera.
 *
 * The board has cameras (OH's CameraService lists a back and a front one), but nothing yet
 * carries Android's camera API to them: no "media.camera" service, so CameraManager lists none
 * and PackageManagerAdapter reports android.hardware.camera absent. This library gives the NDK
 * side the same answer. Without it, a library that names libcamera2ndk.so in DT_NEEDED does not
 * load at all: Godot's engine library does, so a Godot game died before its first frame whether
 * or not it ever used a camera.
 *
 * Nothing here pretends a camera exists. The camera list is empty, any camera id is unknown, and
 * calls that need a device, session, request or metadata object get
 * ACAMERA_ERROR_INVALID_PARAMETER, which is what Android answers for an id that matches no
 * connected camera. The containers, outputs and targets an app may build before it opens a
 * camera are real allocations, as on Android, so building them and freeing them works.
 *
 * Every symbol AOSP's libcamera2ndk.so exports is here (libcamera2ndk.map.txt): a library that
 * names one that is missing does not load at all.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define EXPORT __attribute__((visibility("default")))

/* NdkCameraError.h */
typedef int camera_status_t;
#define ACAMERA_OK 0
#define ACAMERA_ERROR_INVALID_PARAMETER (-10001)

typedef struct ACameraIdList {
    int numCameras;
    const char** cameraIds;
} ACameraIdList;

typedef struct ACameraManager { int unused; } ACameraManager;
typedef struct ACaptureSessionOutputContainer { int unused; } ACaptureSessionOutputContainer;
typedef struct ACaptureSessionOutput { void* window; } ACaptureSessionOutput;
typedef struct ACameraOutputTarget { void* window; } ACameraOutputTarget;

/* Never created here: no camera to open, so no device, session, request or metadata exists. */
typedef struct ACameraDevice ACameraDevice;
typedef struct ACameraCaptureSession ACameraCaptureSession;
typedef struct ACaptureRequest ACaptureRequest;
typedef struct ACameraMetadata ACameraMetadata;
typedef struct ACameraMetadata_const_entry ACameraMetadata_const_entry;

/* --- ACameraManager ---------------------------------------------------------------------- */

EXPORT ACameraManager* ACameraManager_create(void) {
    return calloc(1, sizeof(ACameraManager));
}

EXPORT void ACameraManager_delete(ACameraManager* manager) {
    free(manager);
}

EXPORT camera_status_t ACameraManager_getCameraIdList(ACameraManager* manager, ACameraIdList** cameraIdList) {
    if (manager == NULL || cameraIdList == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    ACameraIdList* list = calloc(1, sizeof(ACameraIdList));
    if (list == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    *cameraIdList = list;   /* numCameras 0 */
    return ACAMERA_OK;
}

EXPORT void ACameraManager_deleteCameraIdList(ACameraIdList* cameraIdList) {
    free(cameraIdList);
}

/* No camera ever becomes available or unavailable, so a registered callback never fires. */
EXPORT camera_status_t ACameraManager_registerAvailabilityCallback(ACameraManager* manager, const void* callback) {
    return manager == NULL || callback == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACameraManager_unregisterAvailabilityCallback(ACameraManager* manager, const void* callback) {
    return manager == NULL || callback == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACameraManager_registerExtendedAvailabilityCallback(ACameraManager* manager,
                                                                           const void* callback) {
    return manager == NULL || callback == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACameraManager_unregisterExtendedAvailabilityCallback(ACameraManager* manager,
                                                                             const void* callback) {
    return manager == NULL || callback == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

/* Android: "ACAMERA_ERROR_INVALID_PARAMETER if ... cameraId does not match any camera devices
 * connected". With no camera, no id matches. */
EXPORT camera_status_t ACameraManager_getCameraCharacteristics(ACameraManager* manager, const char* cameraId,
                                                               ACameraMetadata** characteristics) {
    (void)manager; (void)cameraId; (void)characteristics;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraManager_openCamera(ACameraManager* manager, const char* cameraId,
                                                 const void* callback, ACameraDevice** device) {
    (void)manager; (void)cameraId; (void)callback; (void)device;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

/* --- ACameraMetadata: none exists ---------------------------------------------------------- */

EXPORT camera_status_t ACameraMetadata_getConstEntry(const ACameraMetadata* metadata, uint32_t tag,
                                                     ACameraMetadata_const_entry* entry) {
    (void)metadata; (void)tag; (void)entry;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraMetadata_getAllTags(const ACameraMetadata* metadata, int32_t* numEntries,
                                                  const uint32_t** tags) {
    (void)metadata; (void)numEntries; (void)tags;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT ACameraMetadata* ACameraMetadata_copy(const ACameraMetadata* src) {
    (void)src;
    return NULL;
}

EXPORT void ACameraMetadata_free(ACameraMetadata* metadata) {
    (void)metadata;   /* never one of ours */
}

/* A Java CameraMetadata exists only for a camera, and there is none. */
EXPORT ACameraMetadata* ACameraMetadata_fromCameraMetadata(void* env, void* cameraMetadata) {
    (void)env; (void)cameraMetadata;
    return NULL;
}

EXPORT camera_status_t ACameraMetadata_getTagFromName(const ACameraMetadata* metadata, const char* name,
                                                      uint32_t* tag) {
    (void)metadata; (void)name; (void)tag;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT bool ACameraMetadata_isLogicalMultiCamera(const ACameraMetadata* staticMetadata, size_t* numPhysicalCameras,
                                                 const char* const** physicalCameraIds) {
    (void)staticMetadata; (void)numPhysicalCameras; (void)physicalCameraIds;
    return false;
}

/* --- ACameraDevice, ACaptureRequest, ACameraCaptureSession: none exists -------------------- */

EXPORT camera_status_t ACameraDevice_close(ACameraDevice* device) {
    (void)device;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT const char* ACameraDevice_getId(const ACameraDevice* device) {
    (void)device;
    return NULL;
}

EXPORT camera_status_t ACameraDevice_createCaptureRequest(const ACameraDevice* device, int templateId,
                                                          ACaptureRequest** request) {
    (void)device; (void)templateId; (void)request;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraDevice_createCaptureSession(ACameraDevice* device,
                                                          const ACaptureSessionOutputContainer* outputs,
                                                          const void* callbacks, ACameraCaptureSession** session) {
    (void)device; (void)outputs; (void)callbacks; (void)session;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACaptureRequest_addTarget(ACaptureRequest* request, const ACameraOutputTarget* output) {
    (void)request; (void)output;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACaptureRequest_removeTarget(ACaptureRequest* request, const ACameraOutputTarget* output) {
    (void)request; (void)output;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT void ACaptureRequest_free(ACaptureRequest* request) {
    (void)request;   /* never one of ours */
}

#define NO_REQUEST(fn, ...)                                                             \
    EXPORT camera_status_t fn(__VA_ARGS__) { return ACAMERA_ERROR_INVALID_PARAMETER; }

NO_REQUEST(ACaptureRequest_getConstEntry, const ACaptureRequest* request, uint32_t tag,
           ACameraMetadata_const_entry* entry)
NO_REQUEST(ACaptureRequest_getAllTags, const ACaptureRequest* request, int32_t* numTags, const uint32_t** tags)
NO_REQUEST(ACaptureRequest_setEntry_u8, ACaptureRequest* request, uint32_t tag, uint32_t count, const uint8_t* data)
NO_REQUEST(ACaptureRequest_setEntry_i32, ACaptureRequest* request, uint32_t tag, uint32_t count, const int32_t* data)
NO_REQUEST(ACaptureRequest_setEntry_float, ACaptureRequest* request, uint32_t tag, uint32_t count, const float* data)
NO_REQUEST(ACaptureRequest_setEntry_i64, ACaptureRequest* request, uint32_t tag, uint32_t count, const int64_t* data)
NO_REQUEST(ACaptureRequest_setEntry_double, ACaptureRequest* request, uint32_t tag, uint32_t count,
           const double* data)
NO_REQUEST(ACaptureRequest_setEntry_rational, ACaptureRequest* request, uint32_t tag, uint32_t count,
           const void* data)
NO_REQUEST(ACaptureRequest_setUserContext, ACaptureRequest* request, void* context)
NO_REQUEST(ACaptureRequest_getUserContext, const ACaptureRequest* request, void** context)

EXPORT ACaptureRequest* ACaptureRequest_copy(const ACaptureRequest* src) {
    (void)src;
    return NULL;
}

/* The rest of NdkCameraDevice.h, NdkCaptureRequest.h and NdkCameraCaptureSession.h (API 24 to 35):
 * each needs a device, request or session, and none exists. */
NO_REQUEST(ACaptureRequest_getConstEntry_physicalCamera, const ACaptureRequest* request, const char* physicalId,
           uint32_t tag, ACameraMetadata_const_entry* entry)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_u8, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const uint8_t* data)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_i32, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const int32_t* data)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_float, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const float* data)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_i64, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const int64_t* data)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_double, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const double* data)
NO_REQUEST(ACaptureRequest_setEntry_physicalCamera_rational, ACaptureRequest* request, const char* physicalId,
           uint32_t tag, uint32_t count, const void* data)
NO_REQUEST(ACameraDevice_createCaptureRequest_withPhysicalIds, const ACameraDevice* device, int templateId,
           const ACameraIdList* physicalIdList, ACaptureRequest** request)
NO_REQUEST(ACameraDevice_createCaptureSessionWithSessionParameters, ACameraDevice* device,
           const ACaptureSessionOutputContainer* outputs, const ACaptureRequest* sessionParameters,
           const void* callbacks, ACameraCaptureSession** session)
NO_REQUEST(ACameraDevice_isSessionConfigurationSupported, const ACameraDevice* device,
           const ACaptureSessionOutputContainer* sessionOutputContainer)
NO_REQUEST(ACameraCaptureSession_getDevice, ACameraCaptureSession* session, ACameraDevice** device)
NO_REQUEST(ACameraCaptureSession_updateSharedOutput, ACameraCaptureSession* session, ACaptureSessionOutput* output)
NO_REQUEST(ACameraCaptureSession_captureV2, ACameraCaptureSession* session, const void* callbacks, int numRequests,
           ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_setRepeatingRequestV2, ACameraCaptureSession* session, const void* callbacks,
           int numRequests, ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_logicalCamera_capture, ACameraCaptureSession* session, const void* callbacks,
           int numRequests, ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_logicalCamera_captureV2, ACameraCaptureSession* session, const void* callbacks,
           int numRequests, ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_logicalCamera_setRepeatingRequest, ACameraCaptureSession* session,
           const void* callbacks, int numRequests, ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_logicalCamera_setRepeatingRequestV2, ACameraCaptureSession* session,
           const void* callbacks, int numRequests, ACaptureRequest** requests, int* captureSequenceId)
NO_REQUEST(ACameraCaptureSession_prepareWindow, ACameraCaptureSession* session, void* window)
NO_REQUEST(ACameraCaptureSession_setWindowPreparedCallback, ACameraCaptureSession* session, void* context,
           void* callback)

EXPORT void ACameraCaptureSession_close(ACameraCaptureSession* session) {
    (void)session;
}

EXPORT camera_status_t ACameraCaptureSession_setRepeatingRequest(ACameraCaptureSession* session, const void* callbacks,
                                                                 int numRequests, ACaptureRequest** requests,
                                                                 int* captureSequenceId) {
    (void)session; (void)callbacks; (void)numRequests; (void)requests; (void)captureSequenceId;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraCaptureSession_capture(ACameraCaptureSession* session, const void* callbacks,
                                                     int numRequests, ACaptureRequest** requests,
                                                     int* captureSequenceId) {
    (void)session; (void)callbacks; (void)numRequests; (void)requests; (void)captureSequenceId;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraCaptureSession_stopRepeating(ACameraCaptureSession* session) {
    (void)session;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

EXPORT camera_status_t ACameraCaptureSession_abortCaptures(ACameraCaptureSession* session) {
    (void)session;
    return ACAMERA_ERROR_INVALID_PARAMETER;
}

/* --- Session outputs and targets: built before a camera is opened, so real here ------------ */

EXPORT camera_status_t ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer** container) {
    if (container == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    *container = calloc(1, sizeof(ACaptureSessionOutputContainer));
    return *container == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT void ACaptureSessionOutputContainer_free(ACaptureSessionOutputContainer* container) {
    free(container);
}

EXPORT camera_status_t ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer* container,
                                                          const ACaptureSessionOutput* output) {
    return container == NULL || output == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACaptureSessionOutputContainer_remove(ACaptureSessionOutputContainer* container,
                                                             const ACaptureSessionOutput* output) {
    return container == NULL || output == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACaptureSessionOutput_create(void* window, ACaptureSessionOutput** output) {
    if (window == NULL || output == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    *output = calloc(1, sizeof(ACaptureSessionOutput));
    if (*output == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    (*output)->window = window;
    return ACAMERA_OK;
}

EXPORT camera_status_t ACaptureSessionSharedOutput_create(void* window, ACaptureSessionOutput** output) {
    return ACaptureSessionOutput_create(window, output);
}

EXPORT camera_status_t ACaptureSessionPhysicalOutput_create(void* window, const char* physicalId,
                                                            ACaptureSessionOutput** output) {
    if (physicalId == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    return ACaptureSessionOutput_create(window, output);
}

EXPORT camera_status_t ACaptureSessionSharedOutput_add(ACaptureSessionOutput* output, void* window) {
    return output == NULL || window == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT camera_status_t ACaptureSessionSharedOutput_remove(ACaptureSessionOutput* output, void* window) {
    return output == NULL || window == NULL ? ACAMERA_ERROR_INVALID_PARAMETER : ACAMERA_OK;
}

EXPORT void ACaptureSessionOutput_free(ACaptureSessionOutput* output) {
    free(output);
}

EXPORT camera_status_t ACameraOutputTarget_create(void* window, ACameraOutputTarget** output) {
    if (window == NULL || output == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    *output = calloc(1, sizeof(ACameraOutputTarget));
    if (*output == NULL) return ACAMERA_ERROR_INVALID_PARAMETER;
    (*output)->window = window;
    return ACAMERA_OK;
}

EXPORT void ACameraOutputTarget_free(ACameraOutputTarget* output) {
    free(output);
}
