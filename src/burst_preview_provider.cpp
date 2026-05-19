#include "burst_preview_provider.h"

BurstPreviewProvider::BurstPreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage BurstPreviewProvider::requestImage(const QString& id, QSize* size,
                                          const QSize& /*requestedSize*/) {
    // Strip any query string (e.g. "preview?v=3" → "preview")
    const QString cleanId = id.left(id.indexOf('?') < 0 ? id.size() : id.indexOf('?'));

    QMutexLocker lk(&mutex_);
    if (cleanId == QLatin1String("preview")) {
        if (size) *size = preview_.size();
        return preview_;
    }
    if (cleanId.startsWith(QLatin1String("thumb_"))) {
        bool ok = false;
        qint64 ts = cleanId.mid(6).toLongLong(&ok);
        if (ok && thumbs_.contains(ts)) {
            if (size) *size = thumbs_[ts].size();
            return thumbs_[ts];
        }
    }
    return QImage();
}

void BurstPreviewProvider::setPreview(const QImage& img) {
    QMutexLocker lk(&mutex_);
    preview_ = img;
}

void BurstPreviewProvider::setThumbnail(qint64 timestamp, const QImage& img) {
    QMutexLocker lk(&mutex_);
    thumbs_[timestamp] = img;
}

void BurstPreviewProvider::clear() {
    QMutexLocker lk(&mutex_);
    preview_ = QImage();
    thumbs_.clear();
}
