cmake_minimum_required(VERSION 3.16)
project(motioncam CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(PkgConfig REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Quick Qml Concurrent)
# opencv4.pc is only in the meta-package libopencv-dev (not individual component
# packages); fall back to explicit paths when it's absent.
pkg_check_modules(OpenCV opencv4)
if(NOT OpenCV_FOUND)
    set(OpenCV_INCLUDE_DIRS /usr/include/opencv4)
    set(OpenCV_LIBRARIES opencv_core opencv_imgproc opencv_photo)
    set(OpenCV_FOUND TRUE)
endif()

pkg_check_modules(EGL    REQUIRED egl)
pkg_check_modules(GLES   REQUIRED glesv2)
pkg_check_modules(ZSTD   REQUIRED libzstd)

find_library(HYBRIS_COMMON_LIB hybris-common REQUIRED)
set(HYBRIS_INCLUDE_DIRS /usr/include)

execute_process(
    COMMAND dpkg-architecture -qDEB_HOST_MULTIARCH
    OUTPUT_VARIABLE ARCH_TRIPLET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(DATA_DIR /)
configure_file(manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/manifest.json)

# ── Paths ──────────────────────────────────────────────────────────────────────

set(LIBMOTIONCAM_ROOT   ${CMAKE_SOURCE_DIR}/libMotionCam)
set(LIBMOTIONCAM_SRC    ${LIBMOTIONCAM_ROOT}/libMotionCam)
set(THIRDPARTY          ${LIBMOTIONCAM_ROOT}/thirdparty)

# ── DNG SDK (from source) ──────────────────────────────────────────────────────

file(GLOB DNG_SDK_SOURCES ${THIRDPARTY}/dng_sdk/dng/*.cpp)
# dng_xmp_sdk.cpp unconditionally includes XMP.incl_cpp (Adobe XMP SDK, not vendored)
list(REMOVE_ITEM DNG_SDK_SOURCES ${THIRDPARTY}/dng_sdk/dng/dng_xmp_sdk.cpp)

add_library(dng-sdk STATIC ${DNG_SDK_SOURCES})

target_include_directories(dng-sdk PUBLIC ${THIRDPARTY}/dng_sdk)

target_compile_definitions(dng-sdk PUBLIC
    qDNGThreadSafe=1
    qDNGUseLibJPEG=0
    qDNGUseXMP=0
    qDNGValidate=0
)

target_compile_options(dng-sdk PRIVATE -Wno-reorder -Wno-unused-parameter -Wno-sign-compare)

# ── libMotionCam — capture pipeline only (no Halide, no ImageProcessor) ────────
# ImageProcessor.cpp, MotionCam.cpp, and CameraPreview.cpp are excluded from the
# MVP build because they require pre-compiled Halide kernels.  They will be enabled
# once the Halide arm64-linux kernels are built from generators/.

add_library(motion-cam STATIC
    ${THIRDPARTY}/json11/json11/json11.cpp
    ${THIRDPARTY}/miniz/miniz.c
    ${THIRDPARTY}/miniz/miniz_zip.c
    ${THIRDPARTY}/miniz/miniz_tinfl.c
    ${THIRDPARTY}/miniz/miniz_tdef.c
    ${THIRDPARTY}/tinywav/tinywav.c
    ${LIBMOTIONCAM_SRC}/source/CameraProfile.cpp
    ${LIBMOTIONCAM_SRC}/source/Color.cpp
    # ImageOps.cpp excluded: defringe() uses Halide buffers directly (like ImageProcessor.cpp)
    ${LIBMOTIONCAM_SRC}/source/Logger.cpp
    ${LIBMOTIONCAM_SRC}/source/Measure.cpp
    ${LIBMOTIONCAM_SRC}/source/RawBufferManager.cpp
    ${LIBMOTIONCAM_SRC}/source/RawBufferStreamer.cpp
    ${LIBMOTIONCAM_SRC}/source/RawCameraMetadata.cpp
    ${LIBMOTIONCAM_SRC}/source/RawContainer.cpp
    ${LIBMOTIONCAM_SRC}/source/RawContainerImpl.cpp
    ${LIBMOTIONCAM_SRC}/source/RawContainerImpl_Legacy.cpp
    ${LIBMOTIONCAM_SRC}/source/RawEncoder.cpp
    ${LIBMOTIONCAM_SRC}/source/RawImageBuffer.cpp
    ${LIBMOTIONCAM_SRC}/source/Settings.cpp
    ${LIBMOTIONCAM_SRC}/source/Temperature.cpp
    ${LIBMOTIONCAM_SRC}/source/Util.cpp
)

target_include_directories(motion-cam PUBLIC
    # android-headers first: shims HalideBuffer.h and opencv2/opencv.hpp
    ${CMAKE_SOURCE_DIR}/android-headers
    ${LIBMOTIONCAM_SRC}/include
    ${THIRDPARTY}/json11
    ${THIRDPARTY}/miniz
    ${THIRDPARTY}/tinywav
    ${THIRDPARTY}/queue
    ${THIRDPARTY}/atomic_queue
    ${THIRDPARTY}/dng_sdk
    ${THIRDPARTY}/zstd/include
    ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(motion-cam PUBLIC
    DNG_SUPPORT
    qDNGThreadSafe=1
    qDNGUseLibJPEG=0
    qDNGUseXMP=0
    qDNGValidate=0
)

target_compile_options(motion-cam PRIVATE
    -Wno-unused-parameter -Wno-sign-compare -Wno-reorder
)

# RawEncoder.cpp is compiled from source in the motion-cam target above.
# The prebuilt libmotioncam-encoder.a (Android NDK Bionic/libc++) is NOT used.

target_link_libraries(motion-cam
    dng-sdk
    ${OpenCV_LIBRARIES}
    ${ZSTD_LIBRARIES}
)

# ── Qt resources ───────────────────────────────────────────────────────────────

qt5_add_resources(QT_RESOURCES qml/qml.qrc)

# ── Main executable ────────────────────────────────────────────────────────────

add_executable(motioncam
    # NDK loader + Camera2 NDK shim
    src/ndk_loader.cpp
    src/camera2ndk_shim.cpp

    # Camera session layer (adapted from motioncam Android, vtable via shim)
    src/camera/CameraSession.cpp
    src/camera/CameraStateManager.cpp
    src/camera/CaptureSessionManager.cpp
    src/camera/RawImageConsumer.cpp
    src/camera/ClHelper.cpp
    src/camera/NativeClBuffer.cpp

    # Qt bridge + UI
    src/camera_bridge.cpp
    src/main.cpp

    ${QT_RESOURCES}
)

target_include_directories(motioncam PRIVATE
    src/
    ${CMAKE_SOURCE_DIR}/android-headers
    ${HYBRIS_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
    ${LIBMOTIONCAM_SRC}/include
    ${THIRDPARTY}/json11
    ${THIRDPARTY}/miniz
    ${THIRDPARTY}/queue
    ${THIRDPARTY}/atomic_queue
    ${THIRDPARTY}/dng_sdk
    ${THIRDPARTY}/zstd/include
    ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(motioncam PRIVATE
    DNG_SUPPORT
    qDNGThreadSafe=1
    qDNGUseLibJPEG=0
    qDNGUseXMP=0
    qDNGValidate=0
)

# Inject android_compat.h before all TUs so AOSP NDK availability macros
# and nullability annotations are defined on the Linux toolchain.
target_compile_options(motioncam PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
    -include ${CMAKE_SOURCE_DIR}/android-headers/android_compat.h
    -DWITH_QT
)

target_link_libraries(motioncam
    Qt5::Core Qt5::Gui Qt5::Quick Qt5::Qml Qt5::Concurrent
    ${HYBRIS_COMMON_LIB}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    ${OpenCV_LIBRARIES}
    ${ZSTD_LIBRARIES}
    motion-cam
    dng-sdk
    pthread
    z
)

set_target_properties(motioncam PROPERTIES LINK_FLAGS_RELEASE -s)

# ── Install ────────────────────────────────────────────────────────────────────

install(TARGETS motioncam RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manifest.json DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES motioncam.apparmor DESTINATION ${DATA_DIR})
install(FILES motioncam.desktop  DESTINATION ${DATA_DIR})
install(FILES assets/motioncam.png DESTINATION ${DATA_DIR})
