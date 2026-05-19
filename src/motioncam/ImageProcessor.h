#pragma once
// Stub ImageProcessor.h — replaces the real one (which requires Halide kernels).
// Declares only the symbols actually used by the camera session layer so that
// RawImageConsumer.cpp compiles without the Halide SDK.
//
// This header is found first because src/ precedes libMotionCam/include in
// the CMake include-directory list.

#include <motioncam/RawImageMetadata.h>
#include <motioncam/Settings.h>

namespace motioncam {

struct RawImageBuffer;
struct RawCameraMetadata;

class ImageProcessor {
public:
    // Called by RawImageConsumer to estimate post-process settings from a frame.
    // No-op in the MVP build — estimation requires Halide kernels.
    static void estimateSettings(const RawImageBuffer& /*rawBuffer*/,
                                 const RawCameraMetadata& /*cameraMetadata*/,
                                 PostProcessSettings& /*outSettings*/) {}
};

} // namespace motioncam
