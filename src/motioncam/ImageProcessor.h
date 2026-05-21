#pragma once
// This header is found first because src/ precedes libMotionCam/include in
// the CMake include-directory list.
//
// When HAVE_IMAGE_PROCESSOR is defined, the real Halide-backed ImageProcessor
// is available; skip this stub so callers get the full class.
// When HAVE_IMAGE_PROCESSOR is NOT defined, provide a minimal stub so that
// RawImageConsumer.cpp compiles without Halide.

#ifdef HAVE_IMAGE_PROCESSOR

// Cannot re-include via <motioncam/ImageProcessor.h> (would find this file again).
// Use a relative path from src/ to reach the real header.
#include "../../libMotionCam/libMotionCam/include/motioncam/ImageProcessor.h"

#else

#include <motioncam/RawImageMetadata.h>
#include <motioncam/Settings.h>

namespace motioncam {

struct RawImageBuffer;
struct RawCameraMetadata;

class ImageProcessor {
public:
    static void estimateSettings(const RawImageBuffer& /*rawBuffer*/,
                                 const RawCameraMetadata& /*cameraMetadata*/,
                                 PostProcessSettings& /*outSettings*/) {}
};

} // namespace motioncam

#endif // HAVE_IMAGE_PROCESSOR
