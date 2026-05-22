// Qt headers first — they must be parsed before EGL/GLES on Linux (X11 macros clash).
#include <QOpenGLFramebufferObject>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrent>

#include "camera_bridge.h"

#ifdef HAVE_IMAGE_PROCESSOR
#include <motioncam/MotionCam.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawContainer.h>
#include <motioncam/RawImageBuffer.h>
#include <motioncam/ImageProcessor.h>
#include <motioncam/Settings.h>
#include <json11/json11.hpp>
#endif

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <media/NdkImage.h>
#include <json11/json11.hpp>

#include <motioncam/RawBufferManager.h>

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
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

    void synchronize(QQuickFramebufferObject* fbo) override {
        displayRotation_ = static_cast<CameraBridge*>(fbo)->displayRotation();
    }

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
        // Each row is one displayRotation case (index = displayRotation/90).
        // Vertex layout per row: TL(x,y,z,u,v) BL BRV TR.
        // Portrait (90°) UVs are empirically verified on device.
        // Landscape and inverted cases are derived by successive 90°-CW
        // rotations of the UV coordinates: (u,v) → (v, 1-u).
        static const GLfloat kVerts[4][20] = {
            // 0° — landscape-right: no sensor rotation needed
            { -1, 1,0, 1,1,  -1,-1,0, 1,0,  1,-1,0, 0,0,  1,1,0, 0,1 },
            // 90° — portrait (default back camera): 90° CCW UV rotation
            { -1, 1,0, 0,1,  -1,-1,0, 1,1,  1,-1,0, 1,0,  1,1,0, 0,0 },
            // 180° — landscape-left: 180° UV rotation
            { -1, 1,0, 0,0,  -1,-1,0, 0,1,  1,-1,0, 1,1,  1,1,0, 1,0 },
            // 270° — inverted portrait: 90° CW UV rotation
            { -1, 1,0, 1,0,  -1,-1,0, 0,0,  1,-1,0, 0,1,  1,1,0, 1,1 },
        };
        static const GLushort kIdx[] = {0, 1, 2, 0, 2, 3};
        int idx = (displayRotation_ / 90) & 3;
        const GLfloat* v = kVerts[idx];
        glUseProgram(prog_);
        glEnableVertexAttribArray(loc_pos_);
        glEnableVertexAttribArray(loc_tc_);
        glVertexAttribPointer(loc_pos_, 3, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), v);
        glVertexAttribPointer(loc_tc_,  2, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), v+3);
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
    int    displayRotation_  = 90;
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

// ─── ImageProcessorProgress implementation ───────────────────────────────────
#ifdef HAVE_IMAGE_PROCESSOR
class BridgeProgressListener : public motioncam::ImageProcessorProgress {
public:
    BridgeProgressListener(CameraBridge* bridge, const QString& jpegPath)
        : bridge_(bridge), jpegPath_(jpegPath) {}

    std::string onPreviewSaved(const std::string& /*path*/) const override {
        return ""; // skip preview file
    }

    bool onProgressUpdate(int progress) const override {
        QMetaObject::invokeMethod(bridge_, [b = bridge_, progress]() {
            emit b->processingProgress(progress);
        }, Qt::QueuedConnection);
        return true;
    }

    void onCompleted() const override {
        QString path = jpegPath_;
        bridge_->setLastPhotoPath(path);
        QMetaObject::invokeMethod(bridge_, [b = bridge_, path]() {
            emit b->processingStopped();
            emit b->photoSaved(path);
        }, Qt::QueuedConnection);
    }

    void onError(const std::string& error) const override {
        QString msg = QString::fromStdString(error);
        QMetaObject::invokeMethod(bridge_, [b = bridge_, msg]() {
            emit b->processingStopped();
            emit b->cameraError("Processing failed: " + msg);
        }, Qt::QueuedConnection);
    }

private:
    CameraBridge* bridge_;
    QString jpegPath_;
};
#endif

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
    stopCameraSession();
}

void CameraBridge::stopCameraSession() {
    ready_.store(false);
    if (cameraSession_) {
        cameraSession_->closeCamera();
        cameraSession_.reset();
    }
    if (previewReader_) {
        g_mediandk.AImageReader_delete(previewReader_);
        previewReader_ = nullptr;
    }
    cameraDesc_.reset();
    // Flush rolling buffer: old buffers are sized for the previous camera's
    // resolution.  The new camera will allocate correctly-sized ones via
    // RawImageConsumer::setupBuffers().
    motioncam::RawBufferManager::get().reset();
}

void CameraBridge::switchCamera() {
    if (isRecording() || !hasFrontCamera_.load()) return;
    // Toggle between back (ACAMERA_LENS_FACING_BACK=1) and front (FRONT=0).
    int cur = lensFacingPref_.load();
    lensFacingPref_.store(cur == ACAMERA_LENS_FACING_BACK
        ? ACAMERA_LENS_FACING_FRONT
        : ACAMERA_LENS_FACING_BACK);
    QMetaObject::invokeMethod(this, [this]() { emit readyChanged(); }, Qt::QueuedConnection);
    QtConcurrent::run([this]() {
        stopCameraSession();
        initCamera();
    });
}

void CameraBridge::setLastPhotoPath(const QString& path) {
    { QMutexLocker lk(&lastPhotoMutex_); lastPhotoPath_ = path; }
    QMetaObject::invokeMethod(this, [this]() {
        emit lastPhotoPathChanged();
    }, Qt::QueuedConnection);
}

QQuickFramebufferObject::Renderer* CameraBridge::createRenderer() const {
    return new CameraBridgeRenderer(const_cast<CameraBridge*>(this));
}

void CameraBridge::updateDisplayRotation() {
    int orient = sensorOrientation_.load();
    int devRot = deviceRotation_.load();
    int dr = (orient - devRot + 360) % 360;
    displayRotation_.store(dr);

    int pw = previewStreamW_.load();
    int ph = previewStreamH_.load();
    // DisplayDimension always stores landscape (pw >= ph).
    // For portrait display (dr % 180 == 90) the effective AR flips.
    float ar = (pw > 0 && ph > 0)
               ? ((dr % 180 == 90) ? (float)ph / pw : (float)pw / ph)
               : 9.0f / 16.0f;
    previewAspectRatio_.store(ar);
    QMetaObject::invokeMethod(this, [this]() {
        emit previewAspectRatioChanged();
    }, Qt::QueuedConnection);
    update();
}

void CameraBridge::setDeviceRotation(int degrees) {
    deviceRotation_.store((degrees + 360) % 360);
    updateDisplayRotation();
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

    // Select camera matching lensFacingPref_ (default: back); also detect front camera.
    auto cameras = sessionManager_->getSupportedCameras();
    int wantFacing = lensFacingPref_.load();
    bool foundFront = false;
    for (const auto& id : cameras) {
        auto desc = sessionManager_->getCameraDescription(id);
        if (!desc) continue;
        if (desc->lensFacing == ACAMERA_LENS_FACING_FRONT) foundFront = true;
        if ((int)desc->lensFacing == wantFacing && !cameraDesc_)
            cameraDesc_ = desc;
    }
    if (!cameraDesc_ && !cameras.empty())
        cameraDesc_ = sessionManager_->getCameraDescription(cameras.front());
    bool prevHasFront = hasFrontCamera_.exchange(foundFront);
    if (prevHasFront != foundFront) {
        QMetaObject::invokeMethod(this, [this]() {
            emit hasFrontCameraChanged();
        }, Qt::QueuedConnection);
    }

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

    // Store stream dimensions and sensor orientation, then compute
    // displayRotation and previewAspectRatio for the current device orientation.
    {
        int pw = previewConfig.outputSize.width();
        int ph = previewConfig.outputSize.height();
        int orient = cameraDesc_ ? cameraDesc_->sensorOrientation : 90;
        previewStreamW_.store(pw);
        previewStreamH_.store(ph);
        sensorOrientation_.store(orient);
        updateDisplayRotation();
    }

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
    // Used for video recordings → Movies folder.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                  + "/MotionCam";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return dir + "/motioncam_" + ts + ".motioncam";
}

QString CameraBridge::defaultPhotoPath() const {
    // Used for photo/raw captures → Pictures folder.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                  + "/MotionCam";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return dir + "/motioncam_" + ts + ".motioncam";
}

void CameraBridge::startRecording(const QString& outputPath) {
    if (!isReady() || isRecording() || !cameraDesc_) return;
    QString path = outputPath.isEmpty() ? defaultOutputPath() : outputPath;

    int fd = ::open(path.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        emit cameraError(QString("Cannot open output file: %1 (errno=%2)").arg(path).arg(errno));
        return;
    }
    outputFd_   = fd;
    outputPath_ = path;

    // Hook the buffer streamer: frames arriving via RawImageConsumer will be
    // compressed and written to the file.  audioFd=-1 skips audio (AudioStub).
    motioncam::RawBufferManager::get().enableStreaming(
        {outputFd_}, -1, audio_, 1, cameraDesc_->metadata);

    recording_.store(true);
    frameCount_.store(0);
    recordingTimer_.start();

    if (!statsTimer_) {
        statsTimer_ = new QTimer(this);
        connect(statsTimer_, &QTimer::timeout, this, &CameraBridge::pollRecordingStats);
    }
    statsTimer_->start(250);

    emit recordingChanged();
    emit frameCountChanged();
    fprintf(stderr, "CameraBridge: recording to %s\n", path.toStdString().c_str());
}

void CameraBridge::stopRecording() {
    if (!isRecording()) return;
    recording_.store(false);

    if (statsTimer_) statsTimer_->stop();

    // Capture locals for the background finalizer
    int  fdToClose  = outputFd_;
    QString saved   = outputPath_;
    outputFd_   = -1;
    outputPath_ = QString();

    // endStreaming() joins the IO/process threads — run off the UI thread so we
    // don't block QML while the streamer flushes remaining frames to disk.
    QtConcurrent::run([this, fdToClose, saved]() {
        motioncam::RawBufferManager::get().endStreaming();
        if (fdToClose >= 0) ::close(fdToClose);
        QMetaObject::invokeMethod(this, [this, saved]() {
            emit recordingChanged();
            emit recordingSaved(saved);
        }, Qt::QueuedConnection);
    });
}

// Mirrors DenoiseSettings.java estimateFromExposure(): EV → base merge count.
// Lower EV (darker scene) gets more frames to merge.
static int estimateMergeFrames(int iso, int64_t exposureNs, float aperture) {
    if (iso <= 0 || exposureNs <= 0) return 4;
    double a = aperture > 0 ? aperture : 1.8;
    double expSec = exposureNs / 1.0e9;
    double ev = std::log2((a * a) / expSec) - std::log2(iso / 100.0);
    if      (ev > 7.99) return 4;
    else if (ev > 5.99) return 6;
    else if (ev > 3.99) return 8;
    else                return 12;
}

void CameraBridge::capturePhoto(const QString& outputPath, const QString& settingsJson) {
    if (!isReady() || isRecording() || !cameraSession_) return;
    if (!isRawCapable()) {
        emit cameraError("RAW capture not supported on this device");
        return;
    }
    QString path = outputPath.isEmpty() ? defaultPhotoPath() : outputPath;

    // Default settings + JSON overrides. PostProcessSettings(json) maps any
    // recognised fields (contrast, saturation, dng, spatialDenoiseLevel, ...)
    // and leaves the rest at defaults. "saveJpeg", "temperatureOffset" and
    // "tintOffset" are bridge-only (not part of PostProcessSettings) so we
    // parse them out separately.
    motioncam::PostProcessSettings settings;
    bool saveJpeg = true;
    float temperatureOffset = 0.0f;
    float tintOffset        = 0.0f;
    if (!settingsJson.isEmpty()) {
        std::string err;
        auto j = json11::Json::parse(settingsJson.toStdString(), err);
        if (err.empty()) {
            settings = motioncam::PostProcessSettings(j);
            if (j["saveJpeg"].is_bool())
                saveJpeg = j["saveJpeg"].bool_value();
            if (j["temperatureOffset"].is_number())
                temperatureOffset = j["temperatureOffset"].number_value();
            if (j["tintOffset"].is_number())
                tintOffset = j["tintOffset"].number_value();
        }
    }

    // Mirrors Android's EstimatePostProcessSettings JNI + (estimate + offset)
    // applied in CameraActivity.capture(). When either WB slider is nudged,
    // pull the latest ZSL buffer, run estimateSettings to get the auto-WB
    // baseline, add the slider offset, and pass as absolute temperature/tint.
    // consumeLatestBuffer's LockedBuffers destructor returns the buffer to
    // the ZSL pool, so the upcoming captureHdr still has it available.
    if (temperatureOffset != 0.0f || tintOffset != 0.0f) {
        auto lockedBuffer = motioncam::RawBufferManager::get().consumeLatestBuffer();
        if (lockedBuffer && !lockedBuffer->getBuffers().empty() && cameraDesc_) {
            motioncam::PostProcessSettings est;
            motioncam::ImageProcessor::estimateSettings(
                *lockedBuffer->getBuffers().front(),
                cameraDesc_->metadata,
                est);
            settings.temperature = est.temperature + temperatureOffset;
            settings.tint        = est.tint        + tintOffset;
        }
    }

    // Skip the JPEG processing step only when neither JPEG nor DNG is requested.
    // If DNG is on but JPEG is off, we still run ProcessImage (libMotionCam writes
    // the DNG inside process()); a JPEG file is also produced as a side-effect —
    // suppressing it would require a libMotionCam change.
    {
        std::lock_guard<std::mutex> lk(captureOutputMutex_);
        captureOutputPath_ = path;
        captureSkipProcessing_ = !saveJpeg && !settings.dng;
    }

    float aperture = 0.0f;
    if (cameraDesc_ && !cameraDesc_->metadata.apertures.empty())
        aperture = cameraDesc_->metadata.apertures[0];
    int numMerge = estimateMergeFrames(lastIso_.load(), lastExposureNs_.load(), aperture);
    cameraSession_->captureHdr(numMerge, settings, path.toStdString());
}

void CameraBridge::prepareHdrCapture() {
    if (!isReady() || isRecording() || !cameraSession_) return;
    int32_t iso = lastIso_.load();
    int64_t exp = lastExposureNs_.load();
    // Need a valid AE reading; if none, skip — capturePhoto will save without
    // the HDR underexposed frame (mRequestedHdrCaptures stays 0).
    if (iso <= 0 || exp <= 0) return;
    // HDR underexposed: same ISO, 1/4 the exposure time (≈ -2 EV).
    cameraSession_->prepareHdr(iso, exp / 4);
}


void CameraBridge::captureRaw(const QString& outputPath) {
    if (!isReady() || isRecording() || !cameraSession_) return;
    if (!isRawCapable()) {
        emit cameraError("RAW capture not supported on this device");
        return;
    }
    QString path = outputPath.isEmpty() ? defaultPhotoPath() : outputPath;
    {
        std::lock_guard<std::mutex> lk(captureOutputMutex_);
        captureOutputPath_ = path;
        captureSkipProcessing_ = true;
    }
    motioncam::PostProcessSettings settings;
    cameraSession_->captureHdr(1, settings, path.toStdString());
}

void CameraBridge::pollRecordingStats() {
    size_t memBytes, outputBytes;
    float  fps;
    motioncam::RawBufferManager::get().recordingStats(memBytes, fps, outputBytes);
    // Derive frame count from elapsed recording time × estimated fps
    float elapsedSec = recordingTimer_.elapsed() / 1000.0f;
    frameCount_.store((int)(elapsedSec * fps));
    emit frameCountChanged();
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

void CameraBridge::setAELock(bool lock) {
    if (cameraSession_) cameraSession_->setAELock(lock);
}

void CameraBridge::setAWBLock(bool lock) {
    if (cameraSession_) cameraSession_->setAWBLock(lock);
}

void CameraBridge::setFocusLock(bool lock) {
    if (!cameraSession_) return;
    if (lock) {
        // Switch to manual focus at the current AF-resolved distance.
        cameraSession_->setFocusDistance(cameraSession_->currentFocusDistance());
    } else {
        // Return to continuous auto-focus.
        cameraSession_->setAutoFocus();
    }
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

void CameraBridge::onCameraHdrImageCaptureProgress(int progress) {
    QMetaObject::invokeMethod(this, [this, progress]() {
        emit processingProgress(progress);
    }, Qt::QueuedConnection);
}

void CameraBridge::onCameraHdrImageCaptureCompleted() {
    QString motioncamPath;
    bool skipProcessing = false;
    {
        std::lock_guard<std::mutex> lk(captureOutputMutex_);
        motioncamPath = captureOutputPath_;
        skipProcessing = captureSkipProcessing_;
    }

    if (skipProcessing) {
        // RAW capture: deliver container as-is, no JPEG processing.
        QMetaObject::invokeMethod(this, [this, motioncamPath]() {
            emit photoSaved(motioncamPath);
        }, Qt::QueuedConnection);
        return;
    }

#ifdef HAVE_IMAGE_PROCESSOR
    QString jpegPath = motioncamPath;
    jpegPath.replace(".motioncam", ".jpg");

    QMetaObject::invokeMethod(this, [this]() {
        emit processingStarted();
    }, Qt::QueuedConnection);

    QtConcurrent::run([this, motioncamPath, jpegPath]() {
        BridgeProgressListener listener(this, jpegPath);
        try {
            auto container = motioncam::RawBufferManager::get().popPendingContainer();
            if (container) {
                motioncam::MotionCam::ProcessImage(*container,
                    jpegPath.toStdString(), listener);
            } else {
                motioncam::MotionCam::ProcessImage(motioncamPath.toStdString(),
                    jpegPath.toStdString(), listener);
            }
        } catch (const std::exception& e) {
            QString msg = QString::fromStdString(e.what());
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit processingStopped();
                emit cameraError("Processing failed: " + msg);
            }, Qt::QueuedConnection);
        }
    });
#else
    QMetaObject::invokeMethod(this, [this, motioncamPath]() {
        emit photoSaved(motioncamPath);
    }, Qt::QueuedConnection);
#endif
}

void CameraBridge::onCameraHdrImageCaptureFailed() {
    QMetaObject::invokeMethod(this, [this]() {
        emit cameraError("RAW capture failed");
    }, Qt::QueuedConnection);
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

// ── Burst post-process ────────────────────────────────────────────────────────

#ifdef HAVE_IMAGE_PROCESSOR

template<typename HalideBuf>
static QImage halideToQImage(const HalideBuf& buf) {
    int w = buf.width(), h = buf.height();
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x*3+0] = buf(x, y, 0);
            row[x*3+1] = buf(x, y, 1);
            row[x*3+2] = buf(x, y, 2);
        }
    }
    return img;
}

static motioncam::PostProcessSettings settingsFromJson(const QString& json) {
    std::string err;
    auto j = json11::Json::parse(json.toStdString(), err);
    return err.empty() ? motioncam::PostProcessSettings(j)
                       : motioncam::PostProcessSettings();
}

#endif // HAVE_IMAGE_PROCESSOR

void CameraBridge::acquireBurstFrames() {
#ifdef HAVE_IMAGE_PROCESSOR
    if (!cameraDesc_) return;

    std::unique_ptr<motioncam::RawBufferManager::LockedBuffers> locked;
    {
        std::lock_guard<std::mutex> lk(burstMutex_);
        locked = motioncam::RawBufferManager::get().consumeAllBuffers();
        burstBuffers_ = std::move(locked);
        locked = nullptr;
    }

    // Build frame list and measure sharpness on a background thread.
    QtConcurrent::run([this]() {
        std::vector<std::shared_ptr<motioncam::RawImageBuffer>> frames;
        {
            std::lock_guard<std::mutex> lk(burstMutex_);
            if (!burstBuffers_) return;
            frames = burstBuffers_->getBuffers();
        }
        if (frames.empty()) return;

        QVariantList list;
        int sharpestIdx = 0;
        double bestSharpness = -1.0;

        for (int i = 0; i < (int)frames.size(); ++i) {
            const auto& f = frames[i];
            double sharpness = 0.0;
            try {
                sharpness = motioncam::ImageProcessor::measureSharpness(
                    cameraDesc_->metadata, *f);
            } catch (...) {}

            if (sharpness > bestSharpness) {
                bestSharpness = sharpness;
                sharpestIdx = i;
            }

            QVariantMap entry;
            entry["timestamp"]    = (qlonglong)f->metadata.timestampNs;
            entry["iso"]          = (int)f->metadata.iso;
            entry["exposureNs"]   = (qlonglong)f->metadata.exposureTime;
            entry["sharpness"]    = sharpness;
            list.append(entry);
        }

        // Mark the sharpest frame
        if (!list.isEmpty()) {
            QVariantMap m = list[sharpestIdx].toMap();
            m["sharpest"] = true;
            list[sharpestIdx] = m;
        }

        // Estimate post-process settings from the sharpest frame.
        // This gives the AUTO preset its temperature/tint/shadows/exposure values.
        QString estimatedJson;
        try {
            const auto& sharpestFrame = *frames[sharpestIdx];
            const auto& asShot = sharpestFrame.metadata.asShot;
            fprintf(stderr, "CameraBridge: asShot = [%.4f, %.4f, %.4f]  "
                    "colorMatrix1 empty=%d  calibrationMatrix1 empty=%d\n",
                    asShot[0], asShot[1], asShot[2],
                    cameraDesc_->metadata.colorMatrix1.empty() ? 1 : 0,
                    cameraDesc_->metadata.calibrationMatrix1.empty() ? 1 : 0);

            motioncam::PostProcessSettings est;
            motioncam::ImageProcessor::estimateSettings(
                sharpestFrame, cameraDesc_->metadata, est);

            fprintf(stderr, "CameraBridge: estimated temperature=%.0f tint=%.2f "
                    "shadows=%.3f exposure=%.3f\n",
                    est.temperature, est.tint, est.shadows, est.exposure);

            std::map<std::string, json11::Json> jsMap;
            est.toJson(jsMap);
            estimatedJson = QString::fromStdString(json11::Json(jsMap).dump());
        } catch (...) {
            fprintf(stderr, "CameraBridge: estimateSettings threw\n");
        }

        QMetaObject::invokeMethod(this, [this, list, estimatedJson]() {
            if (!estimatedJson.isEmpty())
                emit burstSettingsEstimated(estimatedJson);
            emit burstFramesReady(list);
        }, Qt::QueuedConnection);
    });
#else
    QMetaObject::invokeMethod(this, [this]() {
        emit burstFramesReady(QVariantList{});
    }, Qt::QueuedConnection);
#endif
}

void CameraBridge::requestBurstThumbnail(qint64 timestamp) {
#ifdef HAVE_IMAGE_PROCESSOR
    if (!cameraDesc_ || !burstProvider_) return;
    QtConcurrent::run([this, timestamp]() {
        std::shared_ptr<motioncam::RawImageBuffer> frame;
        {
            std::lock_guard<std::mutex> lk(burstMutex_);
            if (!burstBuffers_) return;
            for (auto& f : burstBuffers_->getBuffers())
                if (f->metadata.timestampNs == timestamp) { frame = f; break; }
        }
        if (!frame) return;
        try {
            motioncam::PostProcessSettings settings;
            auto buf = motioncam::ImageProcessor::createPreview(
                *frame, 8, cameraDesc_->metadata, settings);
            QImage img = halideToQImage(buf);
            burstProvider_->setThumbnail(timestamp, img);
            QMetaObject::invokeMethod(this, [this, timestamp]() {
                emit burstThumbnailReady(timestamp);
            }, Qt::QueuedConnection);
        } catch (...) {}
    });
#endif
}

void CameraBridge::requestBurstPreview(qint64 timestamp, const QString& settingsJson, int downscaleFactor) {
#ifdef HAVE_IMAGE_PROCESSOR
    if (!cameraDesc_ || !burstProvider_) return;
    // downscaleFactor must be 2, 4 or 8 — clamp to nearest valid value.
    if (downscaleFactor <= 2)      downscaleFactor = 2;
    else if (downscaleFactor <= 4) downscaleFactor = 4;
    else                           downscaleFactor = 8;
    QtConcurrent::run([this, timestamp, settingsJson, downscaleFactor]() {
        std::shared_ptr<motioncam::RawImageBuffer> frame;
        {
            std::lock_guard<std::mutex> lk(burstMutex_);
            if (!burstBuffers_) return;
            for (auto& f : burstBuffers_->getBuffers())
                if (f->metadata.timestampNs == timestamp) { frame = f; break; }
        }
        if (!frame) return;
        try {
            auto settings = settingsFromJson(settingsJson);
            auto buf = motioncam::ImageProcessor::createPreview(
                *frame, downscaleFactor, cameraDesc_->metadata, settings);
            QImage img = halideToQImage(buf);
            burstProvider_->setPreview(img);
            QMetaObject::invokeMethod(this, [this]() {
                emit burstPreviewReady();
            }, Qt::QueuedConnection);
        } catch (...) {}
    });
#endif
}

void CameraBridge::saveBurstFrame(qint64 timestamp, int numFrames, const QString& settingsJson) {
#ifdef HAVE_IMAGE_PROCESSOR
    if (!cameraDesc_) return;
    QString outputPath = defaultPhotoPath();
    outputPath.replace(".motioncam", ".jpg");

    QMetaObject::invokeMethod(this, [this]() {
        emit processingStarted();
    }, Qt::QueuedConnection);

    QtConcurrent::run([this, timestamp, numFrames, settingsJson, outputPath]() {
        try {
            auto settings = settingsFromJson(settingsJson);
            settings.jpegQuality = 95;

            // Gather all locked frames acquired at shutter press
            std::vector<std::shared_ptr<motioncam::RawImageBuffer>> allFrames;
            {
                std::lock_guard<std::mutex> lk(burstMutex_);
                if (burstBuffers_)
                    allFrames = burstBuffers_->getBuffers();
            }
            if (allFrames.empty())
                throw std::runtime_error("No burst frames available");

            // Find the reference frame (selected in filmstrip)
            int refIdx = (int)allFrames.size() - 1;
            for (int i = 0; i < (int)allFrames.size(); ++i) {
                if (allFrames[i]->metadata.timestampNs == (int64_t)timestamp) {
                    refIdx = i; break;
                }
            }

            // Select up to numFrames frames closest to the reference (mirrors save() logic)
            std::vector<std::shared_ptr<motioncam::RawImageBuffer>> selected;
            selected.push_back(allFrames[refIdx]);
            int left = refIdx - 1, right = refIdx + 1;
            int remaining = numFrames - 1;
            while (remaining > 0 && (left >= 0 || right < (int)allFrames.size())) {
                int64_t ld = (left  >= 0)
                    ? std::abs(allFrames[left]->metadata.timestampNs  - allFrames[refIdx]->metadata.timestampNs)
                    : std::numeric_limits<int64_t>::max();
                int64_t rd = (right < (int)allFrames.size())
                    ? std::abs(allFrames[right]->metadata.timestampNs - allFrames[refIdx]->metadata.timestampNs)
                    : std::numeric_limits<int64_t>::max();
                if (ld <= rd) { selected.push_back(allFrames[left--]); }
                else          { selected.push_back(allFrames[right++]); }
                --remaining;
            }

            // Build an in-memory RawContainer with post-process settings embedded
            std::map<std::string, json11::Json> ppMap;
            settings.toJson(ppMap);
            json11::Json::object extra = {
                { "referenceTimestamp", std::to_string((int64_t)timestamp) },
                { "isHdr",             false },
                { "postProcessSettings", json11::Json(ppMap) }
            };
            auto container = motioncam::RawContainer::Create(
                cameraDesc_->metadata, 1, json11::Json(extra));
            container->add(selected, false);

            // Process to JPEG; BridgeProgressListener emits photoSaved / processingStopped
            BridgeProgressListener listener(this, outputPath);
            motioncam::MotionCam::ProcessImage(*container, outputPath.toStdString(), listener);

        } catch (const std::exception& e) {
            QString msg = QString::fromStdString(e.what());
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit processingStopped();
                emit cameraError("Burst save failed: " + msg);
            }, Qt::QueuedConnection);
        }
        releaseBurstFrames();
    });
#endif
}

void CameraBridge::releaseBurstFrames() {
#ifdef HAVE_IMAGE_PROCESSOR
    std::lock_guard<std::mutex> lk(burstMutex_);
    burstBuffers_.reset();
#endif
    if (burstProvider_) burstProvider_->clear();
}
