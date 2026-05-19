#ifndef MOTIONCAM_ANDROID_LOGGER_H
#define MOTIONCAM_ANDROID_LOGGER_H

#include <cstdio>

namespace motioncam {

    #define LOG_TAG "libMotionCam"

    #define LOGD(...) do { fprintf(stderr, "D/" LOG_TAG ": " __VA_ARGS__); fputc('\n', stderr); } while(0)
    #define LOGI(...) do { fprintf(stderr, "I/" LOG_TAG ": " __VA_ARGS__); fputc('\n', stderr); } while(0)
    #define LOGW(...) do { fprintf(stderr, "W/" LOG_TAG ": " __VA_ARGS__); fputc('\n', stderr); } while(0)
    #define LOGE(...) do { fprintf(stderr, "E/" LOG_TAG ": " __VA_ARGS__); fputc('\n', stderr); } while(0)

}

#endif //MOTIONCAM_ANDROID_LOGGER_H
