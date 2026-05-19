#pragma once
// Global vtable instances populated by main() via load_camera2ndk() / load_mediandk().
// The camera2ndk_shim.cpp translation unit provides C-linkage implementations of every
// Camera2 NDK symbol that forward to these globals.  Linking against this TU instead of
// libcamera2ndk.so / libmediandk.so satisfies all symbol references in the motioncam
// camera layer without requiring any edits to those source files.
#include "ndk_loader.h"

extern Camera2NDK g_camera2ndk;
extern MediaNDK   g_mediandk;

void camera2ndk_shim_init(const Camera2NDK& cam, const MediaNDK& media);
