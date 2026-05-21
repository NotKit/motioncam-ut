#pragma once

#include "burst_preview_provider.h"
#include "ndk_loader.h"

#ifdef HAVE_IMAGE_PROCESSOR
#include <motioncam/RawBufferManager.h>
#endif
#include "camera2ndk_shim.h"
#include "audio_stub.h"

#include "camera/CaptureSessionManager.h"
#include "camera/CameraSession.h"
#include "camera/CameraSessionListener.h"
#include "camera/CameraSessionState.h"
#include "camera/RawPreviewListener.h"
#include "camera/CameraDescription.h"
#include "camera/DisplayDimension.h"

#include <QMutex>
#include <QQuickFramebufferObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

// CameraBridge replaces the Android JNI bridge layer (NativeCamera.cpp et al.).
// It is a QML-visible QQuickFramebufferObject that renders the camera preview
// via AHardwareBuffer → EGL texture and exposes capture/recording controls.
class CameraBridge
    : public QQuickFramebufferObject
    , public motioncam::CameraSessionListener
    , public motioncam::RawPreviewListener
{
    Q_OBJECT
    Q_PROPERTY(bool ready               READ isReady               NOTIFY readyChanged)
    Q_PROPERTY(bool recording           READ isRecording           NOTIFY recordingChanged)
    Q_PROPERTY(bool rawCapable          READ isRawCapable          NOTIFY rawCapableChanged)
    Q_PROPERTY(bool hasFrontCamera      READ hasFrontCamera        NOTIFY hasFrontCameraChanged)
    Q_PROPERTY(int  frameCount          READ frameCount            NOTIFY frameCountChanged)
    Q_PROPERTY(int  isoValue            READ isoValue              NOTIFY exposureChanged)
    Q_PROPERTY(qint64 shutterNs         READ shutterNs             NOTIFY exposureChanged)
    Q_PROPERTY(qreal previewAspectRatio READ previewAspectRatio    NOTIFY previewAspectRatioChanged)
    Q_PROPERTY(QString lastPhotoPath    READ lastPhotoPath         NOTIFY lastPhotoPathChanged)

public:
    explicit CameraBridge(QQuickItem* parent = nullptr);
    ~CameraBridge() override;

    Renderer* createRenderer() const override;

    bool isReady()            const { return ready_.load(); }
    bool isRecording()        const { return recording_.load(); }
    bool isRawCapable()       const { return rawCapable_.load(); }
    bool hasFrontCamera()     const { return hasFrontCamera_.load(); }
    int  frameCount()         const { return frameCount_.load(); }
    int  isoValue()           const { return lastIso_.load(); }
    qint64 shutterNs()        const { return lastExposureNs_.load(); }
    qreal previewAspectRatio() const { return previewAspectRatio_.load(); }
    QString lastPhotoPath()   const { QMutexLocker lk(&lastPhotoMutex_); return lastPhotoPath_; }

    // Accessed by the renderer on the render thread.
    AImageReader* previewReader()  const { return previewReader_; }
    int           displayRotation() const { return displayRotation_.load(); }

    // ── QML API ──────────────────────────────────────────────────────────────
    Q_INVOKABLE void startCamera();
    Q_INVOKABLE void switchCamera();
    Q_INVOKABLE void startRecording(const QString& outputPath = QString());
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void capturePhoto(const QString& outputPath = QString());
    Q_INVOKABLE void captureRaw(const QString& outputPath = QString());

    // ── Burst post-process API ────────────────────────────────────────────────
    // Call on shutter press in BURST mode: locks rolling buffer, emits burstFramesReady.
    Q_INVOKABLE void acquireBurstFrames();
    // Generate thumbnail for filmstrip (downscale=8). Emits burstThumbnailReady.
    Q_INVOKABLE void requestBurstThumbnail(qint64 timestamp);
    // Generate large preview (downscale=4). Emits burstPreviewReady.
    Q_INVOKABLE void requestBurstPreview(qint64 timestamp, const QString& settingsJson);
    // Process and save selected frame. Emits photoSaved on success.
    Q_INVOKABLE void saveBurstFrame(qint64 timestamp, int numFrames, const QString& settingsJson);
    // Release locked buffers when PostProcessView closes without saving.
    Q_INVOKABLE void releaseBurstFrames();
    // Called from QML when Screen.orientation changes (degrees = 0/90/180/270,
    // measuring device rotation CW from natural portrait).
    Q_INVOKABLE void setDeviceRotation(int degrees);
    Q_INVOKABLE void setAutoExposure();
    Q_INVOKABLE void setManualExposure(int iso, int exposureMs);
    Q_INVOKABLE void setExposureCompensation(float ev);
    Q_INVOKABLE void setTorch(bool on);
    Q_INVOKABLE void setFocusPoint(float x, float y);
    Q_INVOKABLE void setAutoFocus();

signals:
    void readyChanged();
    void recordingChanged();
    void rawCapableChanged();
    void hasFrontCameraChanged();
    void frameCountChanged();
    void exposureChanged();
    void previewAspectRatioChanged();
    void lastPhotoPathChanged();
    void cameraError(const QString& msg);
    void recordingSaved(const QString& path);
    void photoSaved(const QString& path);
    void processingProgress(int percent);
    void processingStarted();
    void processingStopped();

    void burstFramesReady(const QVariantList& frames);
    void burstThumbnailReady(qint64 timestamp);
    void burstPreviewReady();

    // ── CameraSessionListener (must match camera/CameraSessionListener.h) ─────
public:
    void onCameraStateChanged(const motioncam::CameraCaptureSessionState state) override;
    void onCameraStarted() override;
    void onCameraDisconnected() override;
    void onCameraError(const int error) override;
    void onCameraExposureStatus(const int32_t iso, const int64_t exposureTime) override;
    void onCameraAutoFocusStateChanged(const motioncam::CameraFocusState state,
                                       const float focusDistance) override;
    void onCameraAutoExposureStateChanged(const motioncam::CameraExposureState state) override;
    void onCameraHdrImageCaptureProgress(int progress) override;
    void onCameraHdrImageCaptureCompleted() override;
    void onCameraHdrImageCaptureFailed() override;
    void onMemoryAdjusting() override;
    void onMemoryStable() override;

    // ── RawPreviewListener ────────────────────────────────────────────────────
public:
    void onPreviewGenerated(const void* data, const int len,
                            const int width, const int height) override;

private:
    void initCamera();
    void stopCameraSession();
    void updateDisplayRotation();
    void pollRecordingStats();
    QString defaultOutputPath() const;
    QString defaultPhotoPath() const;

public:
    void setLastPhotoPath(const QString& path);
    void setBurstPreviewProvider(BurstPreviewProvider* p) { burstProvider_ = p; }

private:

    Camera2NDK ndk_{};
    MediaNDK   media_{};

    // Preview AImageReader — its ANativeWindow is passed to CameraSession as the
    // preview output surface.  Frames are read here and rendered to an EGL texture
    // by CameraBridgeRenderer.
    AImageReader* previewReader_ = nullptr;

    std::shared_ptr<motioncam::CaptureSessionManager> sessionManager_;
    std::shared_ptr<motioncam::CameraDescription>     cameraDesc_;
    std::shared_ptr<motioncam::CameraSession>         cameraSession_;
    std::shared_ptr<motioncam::AudioStub>             audio_;

    std::atomic<bool>    ready_{false};
    std::atomic<bool>    recording_{false};
    std::atomic<bool>    rawCapable_{false};
    std::atomic<bool>    hasFrontCamera_{false};
    std::atomic<int>     lensFacingPref_{1}; // 1=back (ACAMERA_LENS_FACING_BACK=1, FRONT=0)
    std::atomic<int>     frameCount_{0};
    std::atomic<int32_t> lastIso_{0};
    std::atomic<int64_t> lastExposureNs_{0};
    std::atomic<float>   previewAspectRatio_{9.0f / 16.0f};
    std::atomic<int>     sensorOrientation_{90};
    std::atomic<int>     deviceRotation_{0};
    // displayRotation = (sensorOrientation - deviceRotation + 360) % 360.
    // Tells the renderer how many degrees CCW to rotate the sensor image.
    std::atomic<int>     displayRotation_{90};
    std::atomic<int>     previewStreamW_{1280};
    std::atomic<int>     previewStreamH_{720};

    int            outputFd_    = -1;
    QString        outputPath_;
    QString        captureOutputPath_;
    bool           captureSkipProcessing_ = false;
    std::mutex     captureOutputMutex_;
    QString        lastPhotoPath_;
    mutable QMutex lastPhotoMutex_;

    // Burst post-process state
#ifdef HAVE_IMAGE_PROCESSOR
    std::unique_ptr<motioncam::RawBufferManager::LockedBuffers> burstBuffers_;
#endif
    std::mutex burstMutex_;
    BurstPreviewProvider* burstProvider_ = nullptr; // owned by QML engine
    QTimer*        statsTimer_  = nullptr;
    QElapsedTimer  recordingTimer_;
};
