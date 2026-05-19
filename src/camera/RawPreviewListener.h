#ifndef MOTIONCAM_ANDROID_RAWPREVIEWLISTENER_H
#define MOTIONCAM_ANDROID_RAWPREVIEWLISTENER_H

// HalideBuffer.h removed — the data pointer is void*, no Halide types in the signature.

namespace motioncam {
    class RawPreviewListener {
    public:
        virtual void onPreviewGenerated(const void* data, const int len, const int width, const int height) = 0;
    };
}

#endif //MOTIONCAM_ANDROID_RAWPREVIEWLISTENER_H
