#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QHash>

// Serves Halide-rendered burst frame previews to QML Image items.
// QML source format:
//   "image://burst/preview"          → current large preview
//   "image://burst/thumb_<timestampNs>" → thumbnail for frame
class BurstPreviewProvider : public QQuickImageProvider {
public:
    BurstPreviewProvider();

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

    void setPreview(const QImage& img);
    void setThumbnail(qint64 timestamp, const QImage& img);
    void clear();

private:
    QMutex  mutex_;
    QImage  preview_;
    QHash<qint64, QImage> thumbs_;
};
