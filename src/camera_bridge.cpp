// Qt headers first — they must be parsed before EGL/GLES on Linux (X11 macros clash).
#include <QOpenGLFramebufferObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrent>

#include "camera_bridge.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <media/NdkImage.h>
#include <json11/json11.hpp>

#include <cstdio>
#include <cstring>

// ─── Preview Renderer ─────────────────────────────────────────────────────────
// Same AHardwareBuffer → EGL_NATIVE_BUFFER_ANDROID → GL_TEXTURE_EXTERNAL_OES
// pipeline as photoncamera-ut.

class CameraBridgeRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit CameraBridgeRenderer(CameraBridge* item) : item_(item) {}
    ~CameraBridgeRenderer() override { cleanup(); }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void synchronize(QQuickFramebufferObject*) override {}

    void render() override {
        if (!initialised_) init();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        AImageReader* reader = item_->previewReader();
        if (!reader || !pfn_eglGetNativeCB_ || !pfn_eglCreateImage_) {
            update();
            return;
        }

        AImage* image = nullptr;
        if (g_mediandk.AImageReader_acquireLatestImage(reader, &image) == AMEDIA_OK && image) {
            AHardwareBuffer* ahb = nullptr;
            if (g_mediandk.AImage_getHardwareBuffer(image, &ahb) == AMEDIA_OK && ahb) {
                if (current_egl_image_ != EGL_NO_IMAGE_KHR) {
                    pfn_eglDestroyImage_(eglGetCurrentDisplay(), current_egl_image_);
                    current_egl_image_ = EGL_NO_IMAGE_KHR;
                }
                EGLClientBuffer cb = pfn_eglGetNativeCB_(ahb);
                if (cb) {
                    static const EGLint attr[] = {EGL_NONE};
                    current_egl_image_ = pfn_eglCreateImage_(
                        eglGetCurrentDisplay(), EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, cb, attr);
                    if (current_egl_image_ != EGL_NO_IMAGE_KHR) {
                        glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex_);
                        pfn_glEGLImageTarget_(GL_TEXTURE_EXTERNAL_OES, current_egl_image_);
                    }
                }
            }
            g_mediandk.AImage_delete(image);
        }

        if (current_egl_image_ != EGL_NO_IMAGE_KHR) drawQuad();
        update();
    }

private:
    void init() {
        initialised_ = true;
        pfn_eglGetNativeCB_  = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
            eglGetProcAddress("eglGetNativeClientBufferANDROID");
        pfn_eglCreateImage_  = (PFNEGLCREATEIMAGEKHRPROC)
            eglGetProcAddress("eglCreateImageKHR");
        pfn_eglDestroyImage_ = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");
        pfn_glEGLImageTarget_ = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");

        glGenTextures(1, &preview_tex_);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex_);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        prog_    = buildProgram();
        loc_pos_ = glGetAttribLocation(prog_,  "a_position");
        loc_tc_  = glGetAttribLocation(prog_,  "a_texCoord");
        loc_tex_ = glGetUniformLocation(prog_, "s_texture");
    }

    void cleanup() {
        if (current_egl_image_ != EGL_NO_IMAGE_KHR && pfn_eglDestroyImage_)
            pfn_eglDestroyImage_(eglGetCurrentDisplay(), current_egl_image_);
        if (preview_tex_) glDeleteTextures(1, &preview_tex_);
        if (prog_)        glDeleteProgram(prog_);
    }

    void drawQuad() {
        // UVs rotate 90° CCW for back camera (sensor orientation 90°)
        static const GLfloat kVerts[] = {
            -1.f,  1.f, 0.f,  0.f, 1.f,
            -1.f, -1.f, 0.f,  1.f, 1.f,
             1.f, -1.f, 0.f,  1.f, 0.f,
             1.f,  1.f, 0.f,  0.f, 0.f,
        };
        static const GLushort kIdx[] = {0, 1, 2, 0, 2, 3};
        glUseProgram(prog_);
        glEnableVertexAttribArray(loc_pos_);
        glEnableVertexAttribArray(loc_tc_);
        glVertexAttribPointer(loc_pos_, 3, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), kVerts);
        glVertexAttribPointer(loc_tc_,  2, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), kVerts+3);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex_);
        glUniform1i(loc_tex_, 0);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kIdx);
        glDisableVertexAttribArray(loc_pos_);
        glDisableVertexAttribArray(loc_tc_);
    }

    static GLuint compileShader(GLenum type, const char* src) {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512]; glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
            fprintf(stderr, "CameraBridgeRenderer shader error: %s\n", buf);
        }
        return sh;
    }

    static GLuint buildProgram() {
        static const char* kVert =
            "#extension GL_OES_EGL_image_external : require\n"
            "attribute vec4 a_position;\n"
            "attribute vec2 a_texCoord;\n"
            "varying vec2 v_texCoord;\n"
            "void main() { gl_Position = a_position; v_texCoord = a_texCoord; }\n";
        static const char* kFrag =
            "#extension GL_OES_EGL_image_external : require\n"
            "precision mediump float;\n"
            "varying vec2 v_texCoord;\n"
            "uniform samplerExternalOES s_texture;\n"
            "void main() { gl_FragColor = texture2D(s_texture, v_texCoord); }\n";
        GLuint vert = compileShader(GL_VERTEX_SHADER,   kVert);
        GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFrag);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert); glAttachShader(prog, frag);
        glLinkProgram(prog);
        glDeleteShader(vert); glDeleteShader(frag);
        return prog;
    }

    CameraBridge* item_;
    bool   initialised_      = false;
    GLuint preview_tex_      = 0;
    GLuint prog_             = 0;
    GLint  loc_pos_          = -1;
    GLint  loc_tc_           = -1;
    GLint  loc_tex_          = -1;
    EGLImageKHR current_egl_image_ = EGL_NO_IMAGE_KHR;

    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC pfn_eglGetNativeCB_   = nullptr;
    PFNEGLCREATEIMAGEKHRPROC               pfn_eglCreateImage_   = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC              pfn_eglDestroyImage_  = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC    pfn_glEGLImageTarget_ = nullptr;
};

// ─── CameraBridge ─────────────────────────────────────────────────────────────

static constexpr int kPreviewWidth  = 1280;
static constexpr int kPreviewHeight = 720;
static constexpr size_t kMaxMemoryBytes = 512ULL * 1024 * 1024; // 512 MB

CameraBridge::CameraBridge(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    setMirrorVertically(true);
}

CameraBridge::~CameraBridge() {
    if (cameraSession_) cameraSession_->closeCamera();
    if (previewReader_) g_mediandk.AImageReader_delete(previewReader_);
}

QQuickFramebufferObject::Renderer* CameraBridge::createRenderer() const {
    return new CameraBridgeRenderer(const_cast<CameraBridge*>(this));
}

void CameraBridge::startCamera() {
    QtConcurrent::run([this]() { initCamera(); });
}

void CameraBridge::initCamera() {
    if (!load_camera2ndk(ndk_)) {
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraError("Failed to load libcamera2ndk.so");
        }, Qt::QueuedConnection);
        return;
    }
    if (!load_mediandk(media_)) {
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraError("Failed to load libmediandk.so");
        }, Qt::QueuedConnection);
        return;
    }
    if (!start_binder_thread_pool()) {
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraError("Failed to start binder thread pool");
        }, Qt::QueuedConnection);
        return;
    }

    // Install vtable into the shim so that camera session files can call through it.
    camera2ndk_shim_init(ndk_, media_);

    // Enumerate cameras using motioncam's CaptureSessionManager.
    try {
        sessionManager_ = std::make_shared<motioncam::CaptureSessionManager>();
    } catch (const std::exception& e) {
        QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())]() {
            emit cameraError(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
        return;
    }

    // Select the back camera.
    auto cameras = sessionManager_->getSupportedCameras();
    for (const auto& id : cameras) {
        auto desc = sessionManager_->getCameraDescription(id);
        if (desc && desc->lensFacing == ACAMERA_LENS_FACING_BACK) {
            cameraDesc_ = desc;
            break;
        }
    }
    if (!cameraDesc_ && !cameras.empty())
        cameraDesc_ = sessionManager_->getCameraDescription(cameras.front());

    if (!cameraDesc_) {
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraError("No supported camera found");
        }, Qt::QueuedConnection);
        return;
    }

    // Check RAW capability.
    bool hasRaw = false;
    for (const auto& cap : cameraDesc_->supportedCaps) {
        if (cap == ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_RAW) {
            hasRaw = true;
            break;
        }
    }
    rawCapable_.store(hasRaw);

    // Get preview output configuration for our target display size.
    motioncam::DisplayDimension displaySize(kPreviewWidth, kPreviewHeight);
    motioncam::DisplayDimension captureSize(kPreviewWidth, kPreviewHeight);
    motioncam::OutputConfiguration previewConfig;

    if (!motioncam::CaptureSessionManager::getPreviewConfiguration(
            *cameraDesc_, captureSize, displaySize, previewConfig)) {
        // Fallback: use AIMAGE_FORMAT_YUV_420_888
        previewConfig.format = 0x23; // AIMAGE_FORMAT_YUV_420_888
        previewConfig.outputSize = displaySize;
    }

    // Create preview AImageReader — its window feeds into the camera session.
    AImageReader* rawReader = nullptr;
    if (g_mediandk.AImageReader_new(
            previewConfig.outputSize.width(),
            previewConfig.outputSize.height(),
            previewConfig.format, 4, &rawReader) != AMEDIA_OK || !rawReader) {
        QMetaObject::invokeMethod(this, [this]() {
            emit cameraError("Failed to create preview AImageReader");
        }, Qt::QueuedConnection);
        return;
    }
    previewReader_ = rawReader;

    // Register image listener so QML preview item re-renders on each frame.
    AImageReader_ImageListener listener{};
    listener.context = this;
    listener.onImageAvailable = [](void* ctx, AImageReader*) {
        auto* self = static_cast<CameraBridge*>(ctx);
        QMetaObject::invokeMethod(self, [self]() { self->update(); }, Qt::QueuedConnection);
    };
    g_mediandk.AImageReader_setImageListener(previewReader_, &listener);

    ANativeWindow* previewWindow = nullptr;
    g_mediandk.AImageReader_getWindow(previewReader_, &previewWindow);

    // Wrap in shared_ptr with no-op deleter (owned by previewReader_).
    auto previewWindowPtr = std::shared_ptr<ANativeWindow>(
        previewWindow, [](ANativeWindow*) {});

    // Get RAW output configuration.
    motioncam::OutputConfiguration rawConfig;
    bool haveRaw = hasRaw &&
        motioncam::CaptureSessionManager::getRawConfiguration(*cameraDesc_, false, false, rawConfig);

    // Audio stub — recording without audio for MVP.
    audio_ = std::make_shared<motioncam::AudioStub>();

    // Build startup settings JSON (use defaults).
    json11::Json startupSettings = json11::Json::object{};

    // Create and open the camera session.
    cameraSession_ = std::make_shared<motioncam::CameraSession>();
    // Pass this as listener with no-op deleter — CameraBridge outlives CameraSession.
    auto selfListener = std::shared_ptr<motioncam::CameraSessionListener>(
        static_cast<motioncam::CameraSessionListener*>(this),
        [](motioncam::CameraSessionListener*) {});

    cameraSession_->openCamera(
        selfListener,
        cameraDesc_,
        haveRaw ? rawConfig : motioncam::OutputConfiguration{},
        previewConfig,
        sessionManager_->cameraManager(),
        previewWindowPtr,
        false, // setupForRawPreview — skip Halide preview path
        startupSettings,
        kMaxMemoryBytes);

    ready_.store(true);
    QMetaObject::invokeMethod(this, [this]() {
        emit readyChanged();
        emit rawCapableChanged();
        update();
    }, Qt::QueuedConnection);
}

QString CameraBridge::defaultOutputPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                  + "/MotionCam";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return dir + "/motioncam_" + ts + ".motioncam";
}

void CameraBridge::startRecording(const QString& outputPath) {
    if (!isReady() || isRecording()) return;
    QString path = outputPath.isEmpty() ? defaultOutputPath() : outputPath;
    // TODO: call cameraSession_ recording API once mapped
    recording_.store(true);
    frameCount_.store(0);
    QMetaObject::invokeMethod(this, [this]() { emit recordingChanged(); emit frameCountChanged(); },
                              Qt::QueuedConnection);
    fprintf(stderr, "CameraBridge: recording to %s\n", path.toStdString().c_str());
}

void CameraBridge::stopRecording() {
    if (!isRecording()) return;
    recording_.store(false);
    QMetaObject::invokeMethod(this, [this]() { emit recordingChanged(); }, Qt::QueuedConnection);
    fprintf(stderr, "CameraBridge: recording stopped\n");
}

void CameraBridge::setAutoExposure() {
    if (cameraSession_) cameraSession_->setAutoExposure();
}

void CameraBridge::setManualExposure(int iso, int exposureMs) {
    if (cameraSession_)
        cameraSession_->setManualExposure(iso, (int64_t)exposureMs * 1'000'000LL);
}

void CameraBridge::setExposureCompensation(float ev) {
    if (cameraSession_) cameraSession_->setExposureCompensation(ev);
}

void CameraBridge::setTorch(bool on) {
    if (cameraSession_) cameraSession_->setTorch(on);
}

void CameraBridge::setFocusPoint(float x, float y) {
    if (cameraSession_) cameraSession_->setFocusPoint(x, y, x, y);
}

void CameraBridge::setAutoFocus() {
    if (cameraSession_) cameraSession_->setAutoFocus();
}

// ── CameraSessionListener ─────────────────────────────────────────────────────

void CameraBridge::onCameraStateChanged(const motioncam::CameraCaptureSessionState state) {
    fprintf(stderr, "CameraBridge: session state -> %d\n", (int)state);
}

void CameraBridge::onCameraStarted() {
    fprintf(stderr, "CameraBridge: camera started\n");
}

void CameraBridge::onCameraDisconnected() {
    fprintf(stderr, "CameraBridge: camera disconnected\n");
    ready_.store(false);
    QMetaObject::invokeMethod(this, [this]() { emit readyChanged(); }, Qt::QueuedConnection);
}

void CameraBridge::onCameraError(const int error) {
    fprintf(stderr, "CameraBridge: camera error %d\n", error);
    ready_.store(false);
    QMetaObject::invokeMethod(this, [this, error]() {
        emit cameraError(QString("Camera error: %1").arg(error));
        emit readyChanged();
    }, Qt::QueuedConnection);
}

void CameraBridge::onCameraExposureStatus(const int32_t iso, const int64_t exposureTime) {
    lastIso_.store(iso);
    lastExposureNs_.store(exposureTime);
    QMetaObject::invokeMethod(this, [this]() { emit exposureChanged(); }, Qt::QueuedConnection);
}

void CameraBridge::onCameraAutoFocusStateChanged(
        const motioncam::CameraFocusState /*state*/, const float /*focusDistance*/) {}

void CameraBridge::onCameraAutoExposureStateChanged(
        const motioncam::CameraExposureState /*state*/) {}

void CameraBridge::onCameraHdrImageCaptureProgress(int /*progress*/) {}

void CameraBridge::onCameraHdrImageCaptureCompleted() {
    fprintf(stderr, "CameraBridge: HDR capture completed\n");
}

void CameraBridge::onCameraHdrImageCaptureFailed() {
    fprintf(stderr, "CameraBridge: HDR capture failed\n");
}

void CameraBridge::onMemoryAdjusting() {
    fprintf(stderr, "CameraBridge: memory adjusting\n");
}

void CameraBridge::onMemoryStable() {
    fprintf(stderr, "CameraBridge: memory stable\n");
}

// ── RawPreviewListener ────────────────────────────────────────────────────────

void CameraBridge::onPreviewGenerated(const void* /*data*/, const int /*len*/,
                                      const int /*width*/, const int /*height*/) {
    // Halide raw preview — not used in MVP (setupForRawPreview = false).
}
