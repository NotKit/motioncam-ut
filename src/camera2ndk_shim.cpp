// Provides C-linkage implementations of every Camera2 NDK / MediaNDK function used
// by the motioncam camera layer.  At link time these replace the Android SDK shared
// libraries; at runtime they forward through the vtable loaded via libhybris.

#include "camera2ndk_shim.h"
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>

Camera2NDK g_camera2ndk{};
MediaNDK   g_mediandk{};

void camera2ndk_shim_init(const Camera2NDK& cam, const MediaNDK& media) {
    g_camera2ndk = cam;
    g_mediandk   = media;
}

// ── ACameraManager ────────────────────────────────────────────────────────────

extern "C" ACameraManager* ACameraManager_create() {
    return g_camera2ndk.ACameraManager_create();
}
extern "C" void ACameraManager_delete(ACameraManager* m) {
    g_camera2ndk.ACameraManager_delete(m);
}
extern "C" camera_status_t ACameraManager_getCameraIdList(
        ACameraManager* m, ACameraIdList** l) {
    return g_camera2ndk.ACameraManager_getCameraIdList(m, l);
}
extern "C" void ACameraManager_deleteCameraIdList(ACameraIdList* l) {
    g_camera2ndk.ACameraManager_deleteCameraIdList(l);
}
extern "C" camera_status_t ACameraManager_getCameraCharacteristics(
        ACameraManager* m, const char* id, ACameraMetadata** meta) {
    return g_camera2ndk.ACameraManager_getCameraCharacteristics(m, id, meta);
}
extern "C" camera_status_t ACameraManager_openCamera(
        ACameraManager* m, const char* id,
        ACameraDevice_StateCallbacks* cb, ACameraDevice** dev) {
    return g_camera2ndk.ACameraManager_openCamera(m, id, cb, dev);
}

// ── ACameraMetadata ───────────────────────────────────────────────────────────

extern "C" camera_status_t ACameraMetadata_getConstEntry(
        const ACameraMetadata* m, uint32_t tag, ACameraMetadata_const_entry* e) {
    return g_camera2ndk.ACameraMetadata_getConstEntry(m, tag, e);
}
extern "C" void ACameraMetadata_free(ACameraMetadata* m) {
    g_camera2ndk.ACameraMetadata_free(m);
}
extern "C" ACameraMetadata* ACameraMetadata_copy(const ACameraMetadata* src) {
    if (!g_camera2ndk.ACameraMetadata_copy) return nullptr;
    return g_camera2ndk.ACameraMetadata_copy(src);
}

// ── ACameraDevice ─────────────────────────────────────────────────────────────

extern "C" camera_status_t ACameraDevice_close(ACameraDevice* d) {
    return g_camera2ndk.ACameraDevice_close(d);
}
extern "C" camera_status_t ACameraDevice_createCaptureSession(
        ACameraDevice* d, const ACaptureSessionOutputContainer* c,
        const ACameraCaptureSession_stateCallbacks* cb,
        ACameraCaptureSession** s) {
    return g_camera2ndk.ACameraDevice_createCaptureSession(d, c, cb, s);
}
extern "C" camera_status_t ACameraDevice_createCaptureRequest(
        const ACameraDevice* d, ACameraDevice_request_template t, ACaptureRequest** r) {
    return g_camera2ndk.ACameraDevice_createCaptureRequest(d, t, r);
}

// ── ACaptureSessionOutputContainer ───────────────────────────────────────────

extern "C" camera_status_t ACaptureSessionOutputContainer_create(
        ACaptureSessionOutputContainer** c) {
    return g_camera2ndk.ACaptureSessionOutputContainer_create(c);
}
extern "C" void ACaptureSessionOutputContainer_free(
        ACaptureSessionOutputContainer* c) {
    g_camera2ndk.ACaptureSessionOutputContainer_free(c);
}
extern "C" camera_status_t ACaptureSessionOutputContainer_add(
        ACaptureSessionOutputContainer* c, const ACaptureSessionOutput* o) {
    return g_camera2ndk.ACaptureSessionOutputContainer_add(c, o);
}
extern "C" camera_status_t ACaptureSessionOutputContainer_remove(
        ACaptureSessionOutputContainer* c, const ACaptureSessionOutput* o) {
    if (!g_camera2ndk.ACaptureSessionOutputContainer_remove)
        return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureSessionOutputContainer_remove(c, o);
}

// ── ACaptureSessionOutput ─────────────────────────────────────────────────────

extern "C" camera_status_t ACaptureSessionOutput_create(
        ANativeWindow* w, ACaptureSessionOutput** o) {
    return g_camera2ndk.ACaptureSessionOutput_create(w, o);
}
extern "C" void ACaptureSessionOutput_free(ACaptureSessionOutput* o) {
    g_camera2ndk.ACaptureSessionOutput_free(o);
}

// ── ACameraOutputTarget ───────────────────────────────────────────────────────

extern "C" camera_status_t ACameraOutputTarget_create(
        ANativeWindow* w, ACameraOutputTarget** t) {
    return g_camera2ndk.ACameraOutputTarget_create(w, t);
}
extern "C" void ACameraOutputTarget_free(ACameraOutputTarget* t) {
    g_camera2ndk.ACameraOutputTarget_free(t);
}

// ── ACaptureRequest ───────────────────────────────────────────────────────────

extern "C" camera_status_t ACaptureRequest_addTarget(
        ACaptureRequest* r, const ACameraOutputTarget* t) {
    return g_camera2ndk.ACaptureRequest_addTarget(r, t);
}
extern "C" camera_status_t ACaptureRequest_removeTarget(
        ACaptureRequest* r, const ACameraOutputTarget* t) {
    if (!g_camera2ndk.ACaptureRequest_removeTarget) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureRequest_removeTarget(r, t);
}
extern "C" camera_status_t ACaptureRequest_setEntry_u8(
        ACaptureRequest* r, uint32_t tag, uint32_t count, const uint8_t* d) {
    return g_camera2ndk.ACaptureRequest_setEntry_u8(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_setEntry_i32(
        ACaptureRequest* r, uint32_t tag, uint32_t count, const int32_t* d) {
    return g_camera2ndk.ACaptureRequest_setEntry_i32(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_setEntry_i64(
        ACaptureRequest* r, uint32_t tag, uint32_t count, const int64_t* d) {
    return g_camera2ndk.ACaptureRequest_setEntry_i64(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_setEntry_float(
        ACaptureRequest* r, uint32_t tag, uint32_t count, const float* d) {
    if (!g_camera2ndk.ACaptureRequest_setEntry_float) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureRequest_setEntry_float(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_setEntry_double(
        ACaptureRequest* r, uint32_t tag, uint32_t count, const double* d) {
    if (!g_camera2ndk.ACaptureRequest_setEntry_double) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureRequest_setEntry_double(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_setEntry_rational(
        ACaptureRequest* r, uint32_t tag, uint32_t count,
        const ACameraMetadata_rational* d) {
    if (!g_camera2ndk.ACaptureRequest_setEntry_rational) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureRequest_setEntry_rational(r, tag, count, d);
}
extern "C" camera_status_t ACaptureRequest_getConstEntry(
        const ACaptureRequest* r, uint32_t tag, ACameraMetadata_const_entry* e) {
    if (!g_camera2ndk.ACaptureRequest_getConstEntry) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACaptureRequest_getConstEntry(r, tag, e);
}
extern "C" void ACaptureRequest_free(ACaptureRequest* r) {
    g_camera2ndk.ACaptureRequest_free(r);
}

// ── ACameraCaptureSession ─────────────────────────────────────────────────────

extern "C" camera_status_t ACameraCaptureSession_setRepeatingRequest(
        ACameraCaptureSession* s, ACameraCaptureSession_captureCallbacks* cb,
        int n, ACaptureRequest** reqs, int* seqId) {
    return g_camera2ndk.ACameraCaptureSession_setRepeatingRequest(s, cb, n, reqs, seqId);
}
extern "C" camera_status_t ACameraCaptureSession_capture(
        ACameraCaptureSession* s, ACameraCaptureSession_captureCallbacks* cb,
        int n, ACaptureRequest** reqs, int* seqId) {
    return g_camera2ndk.ACameraCaptureSession_capture(s, cb, n, reqs, seqId);
}
extern "C" camera_status_t ACameraCaptureSession_stopRepeating(
        ACameraCaptureSession* s) {
    return g_camera2ndk.ACameraCaptureSession_stopRepeating(s);
}
extern "C" void ACameraCaptureSession_close(ACameraCaptureSession* s) {
    g_camera2ndk.ACameraCaptureSession_close(s);
}
extern "C" camera_status_t ACameraCaptureSession_abortCaptures(
        ACameraCaptureSession* s) {
    if (!g_camera2ndk.ACameraCaptureSession_abortCaptures) return ACAMERA_ERROR_UNSUPPORTED_OPERATION;
    return g_camera2ndk.ACameraCaptureSession_abortCaptures(s);
}

// ── AImageReader ──────────────────────────────────────────────────────────────

extern "C" media_status_t AImageReader_new(
        int32_t w, int32_t h, int32_t fmt, int32_t maxImages, AImageReader** r) {
    return g_mediandk.AImageReader_new(w, h, fmt, maxImages, r);
}
extern "C" media_status_t AImageReader_newWithUsage(
        int32_t w, int32_t h, int32_t fmt, uint64_t usage, int32_t max, AImageReader** r) {
    if (!g_mediandk.AImageReader_newWithUsage)
        return g_mediandk.AImageReader_new(w, h, fmt, max, r);
    return g_mediandk.AImageReader_newWithUsage(w, h, fmt, usage, max, r);
}
extern "C" void AImageReader_delete(AImageReader* r) {
    g_mediandk.AImageReader_delete(r);
}
extern "C" media_status_t AImageReader_getWindow(AImageReader* r, ANativeWindow** w) {
    return g_mediandk.AImageReader_getWindow(r, w);
}
extern "C" media_status_t AImageReader_setImageListener(
        AImageReader* r, AImageReader_ImageListener* l) {
    return g_mediandk.AImageReader_setImageListener(r, l);
}
extern "C" media_status_t AImageReader_acquireLatestImage(AImageReader* r, AImage** img) {
    return g_mediandk.AImageReader_acquireLatestImage(r, img);
}
extern "C" media_status_t AImageReader_acquireNextImage(AImageReader* r, AImage** img) {
    return g_mediandk.AImageReader_acquireNextImage(r, img);
}
extern "C" media_status_t AImageReader_getMaxImages(const AImageReader* r, int32_t* max) {
    if (!g_mediandk.AImageReader_getMaxImages) { *max = 0; return AMEDIA_OK; }
    return g_mediandk.AImageReader_getMaxImages(r, max);
}

// ── AImage ────────────────────────────────────────────────────────────────────

extern "C" void AImage_delete(AImage* img) {
    g_mediandk.AImage_delete(img);
}
extern "C" media_status_t AImage_getHardwareBuffer(const AImage* img, AHardwareBuffer** buf) {
    if (!g_mediandk.AImage_getHardwareBuffer) return AMEDIA_ERROR_UNSUPPORTED;
    return g_mediandk.AImage_getHardwareBuffer(img, buf);
}
extern "C" media_status_t AImage_getWidth(const AImage* img, int32_t* w) {
    return g_mediandk.AImage_getWidth(img, w);
}
extern "C" media_status_t AImage_getHeight(const AImage* img, int32_t* h) {
    return g_mediandk.AImage_getHeight(img, h);
}
extern "C" media_status_t AImage_getNumberOfPlanes(const AImage* img, int32_t* n) {
    return g_mediandk.AImage_getNumberOfPlanes(img, n);
}
extern "C" media_status_t AImage_getFormat(const AImage* img, int32_t* fmt) {
    return g_mediandk.AImage_getFormat(img, fmt);
}
extern "C" media_status_t AImage_getPlaneData(
        const AImage* img, int32_t plane, uint8_t** data, int* len) {
    return g_mediandk.AImage_getPlaneData(img, plane, data, len);
}
extern "C" media_status_t AImage_getPlaneRowStride(
        const AImage* img, int32_t plane, int32_t* stride) {
    return g_mediandk.AImage_getPlaneRowStride(img, plane, stride);
}
extern "C" media_status_t AImage_getPlanePixelStride(
        const AImage* img, int32_t plane, int32_t* stride) {
    return g_mediandk.AImage_getPlanePixelStride(img, plane, stride);
}
extern "C" media_status_t AImage_getTimestamp(const AImage* img, int64_t* ts) {
    return g_mediandk.AImage_getTimestamp(img, ts);
}
