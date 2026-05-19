#!/bin/bash
# Generates Halide kernels for arm64-linux (Ubuntu Touch target).
# Runs on the x86_64 build host; outputs arm64 static libs.
# Skips if the stamp file already exists (cached from a previous run).
#
# Usage: bash scripts/generate_halide_kernels.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GENERATORS_DIR="$REPO_ROOT/libMotionCam/libMotionCam/generators"
OUTPUT_DIR="$REPO_ROOT/libMotionCam/libMotionCam/halide/arm64-linux"
INCLUDE_DIR="$REPO_ROOT/libMotionCam/libMotionCam/halide/include"
TOOLS_CACHE="$SCRIPT_DIR/.halide-tools"

# Halide 14 — last version before the Halide 17 Generator API overhaul.
# The archive filename includes a commit hash; this is the canonical name.
HALIDE_VERSION="14.0.0"
HALIDE_HASH="6b9ed2afd1d6d0badf04986602c943e287d44e46"
HALIDE_ARCHIVE="Halide-${HALIDE_VERSION}-x86-64-linux-${HALIDE_HASH}.tar.gz"
HALIDE_URL="https://github.com/halide/Halide/releases/download/v${HALIDE_VERSION}/${HALIDE_ARCHIVE}"
# The archive extracts to a directory without the hash suffix
HALIDE_PATH="${TOOLS_CACHE}/Halide-${HALIDE_VERSION}-x86-64-linux"

TARGET="arm-64-linux"
FLAGS="no_runtime"

# ── Already done? ──────────────────────────────────────────────────────────────
if [ -f "${OUTPUT_DIR}/.done" ]; then
    echo "Halide arm64-linux kernels already present — skipping."
    exit 0
fi

mkdir -p "$TOOLS_CACHE" "$OUTPUT_DIR" "$INCLUDE_DIR"

# ── Download Halide prebuilt ───────────────────────────────────────────────────
if [ ! -d "$HALIDE_PATH" ]; then
    echo "Downloading Halide ${HALIDE_VERSION}..."
    curl -L --progress-bar "$HALIDE_URL" -o "$TOOLS_CACHE/$HALIDE_ARCHIVE"
    echo "Extracting..."
    tar -xzf "$TOOLS_CACHE/$HALIDE_ARCHIVE" -C "$TOOLS_CACHE"
    rm "$TOOLS_CACHE/$HALIDE_ARCHIVE"
    # Archive may extract to directory with or without the hash suffix — normalise
    extracted=$(find "$TOOLS_CACHE" -maxdepth 1 -name "Halide-${HALIDE_VERSION}-x86-64-linux*" -type d | head -1)
    [ "$extracted" = "$HALIDE_PATH" ] || mv "$extracted" "$HALIDE_PATH"
fi

# Locate GenGen.cpp — layout differs between Halide builds
GENGEN=""
for candidate in \
    "${HALIDE_PATH}/share/Halide/tools/GenGen.cpp" \
    "${HALIDE_PATH}/share/tools/GenGen.cpp" \
    "${HALIDE_PATH}/tools/GenGen.cpp"; do
    if [ -f "$candidate" ]; then GENGEN="$candidate"; break; fi
done
[ -n "$GENGEN" ] || { echo "ERROR: GenGen.cpp not found in $HALIDE_PATH" >&2; exit 1; }

echo "Halide path : $HALIDE_PATH"
echo "GenGen.cpp  : $GENGEN"
echo "Output dir  : $OUTPUT_DIR"

export LD_LIBRARY_PATH="${HALIDE_PATH}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ── Compile generators (x86_64 native, produce arm64 code via LLVM) ───────────
pushd "$GENERATORS_DIR"
mkdir -p tmp

CXXFLAGS="-O3 -std=c++17 -Wno-deprecated-declarations -Wno-unused-variable"

echo "Compiling DenoiseGenerator..."
g++ $CXXFLAGS DenoiseGenerator.cpp "$GENGEN" \
    -I "${HALIDE_PATH}/include" \
    -L "${HALIDE_PATH}/lib" -Wl,-rpath,"${HALIDE_PATH}/lib" \
    -lHalide -lpthread -ldl \
    -o tmp/denoise_generator

echo "Compiling PostProcessGenerator..."
g++ $CXXFLAGS PostProcessGenerator.cpp "$GENGEN" \
    -I "${HALIDE_PATH}/include" \
    -L "${HALIDE_PATH}/lib" -Wl,-rpath,"${HALIDE_PATH}/lib" \
    -lHalide -lpthread -ldl \
    -o tmp/postprocess_generator

# ── Denoise kernels ────────────────────────────────────────────────────────────
echo "Generating denoise kernels (target: ${TARGET})..."

for W in 3 5 7; do
    ./tmp/denoise_generator \
        -g denoise_generator -f "fuse_denoise_${W}x${W}" \
        -e static_library,h -o "$OUTPUT_DIR" \
        target="${TARGET}-${FLAGS}" window=${W}
done

./tmp/denoise_generator \
    -g forward_transform_generator -f forward_transform \
    -e static_library,h -o "$OUTPUT_DIR" \
    target="${TARGET}-${FLAGS}" input.type=uint16 levels=4

./tmp/denoise_generator \
    -g fuse_image_generator -f fuse_image \
    -e static_library,h -o "$OUTPUT_DIR" \
    target="${TARGET}-${FLAGS}" \
    input.type=uint16 reference.size=4 reference.type=float32 \
    intermediate.size=4 intermediate.type=float32

./tmp/denoise_generator \
    -g inverse_transform_generator -f inverse_transform \
    -e static_library,h -o "$OUTPUT_DIR" \
    target="${TARGET}-${FLAGS}" input.size=4

# ── PostProcess kernels ────────────────────────────────────────────────────────
echo "Generating postprocess kernels (target: ${TARGET})..."

./tmp/postprocess_generator -g stats_generator            -f generate_stats   -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g measure_noise_generator    -f measure_noise    -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g build_bayer_generator      -f build_bayer      -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g build_bayer_generator2     -f build_bayer2     -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g hdr_mask_generator         -f hdr_mask         -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g linear_image_generator     -f linear_image     -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g measure_image_generator    -f measure_image    -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g generate_edges_generator   -f generate_edges   -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g deinterleave_raw_generator -f deinterleave_raw -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g postprocess_generator      -f postprocess      -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g fast_preview_generator     -f fast_preview     -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"
./tmp/postprocess_generator -g fast_preview_generator2    -f fast_preview2    -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}"

# Preview variants: scale × rotation
for SCALE in 2 4 8; do
    case $SCALE in
        2) TM=9; SH=true;  PR=7 ;;
        4) TM=8; SH=true;  PR=3 ;;
        8) TM=7; SH=false; PR=3 ;;
    esac

    ./tmp/postprocess_generator -g preview_generator -f "preview_landscape${SCALE}" \
        -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}" \
        rotation=0   tonemap_levels=${TM} downscale_factor=${SCALE} enable_sharpen=${SH} pop_radius=${PR}

    ./tmp/postprocess_generator -g preview_generator -f "preview_reverse_portrait${SCALE}" \
        -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}" \
        rotation=90  tonemap_levels=${TM} downscale_factor=${SCALE} enable_sharpen=${SH} pop_radius=${PR}

    ./tmp/postprocess_generator -g preview_generator -f "preview_portrait${SCALE}" \
        -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}" \
        rotation=-90 tonemap_levels=${TM} downscale_factor=${SCALE} enable_sharpen=${SH} pop_radius=${PR}

    ./tmp/postprocess_generator -g preview_generator -f "preview_reverse_landscape${SCALE}" \
        -e static_library,h -o "$OUTPUT_DIR" target="${TARGET}-${FLAGS}" \
        rotation=180 tonemap_levels=${TM} downscale_factor=${SCALE} enable_sharpen=${SH} pop_radius=${PR}
done

# ── Halide runtime ─────────────────────────────────────────────────────────────
echo "Generating Halide runtime for ${TARGET}..."
./tmp/postprocess_generator \
    -r halide_runtime -e static_library,h -o "$OUTPUT_DIR" \
    target="${TARGET}"
mv "$OUTPUT_DIR/halide_runtime.a" "$OUTPUT_DIR/halide_runtime_host.a"

popd  # generators dir

# ── Copy runtime headers for compiling ImageProcessor / ImageOps ──────────────
echo "Copying Halide runtime headers to ${INCLUDE_DIR}..."
cp "${HALIDE_PATH}/include/HalideBuffer.h"  "$INCLUDE_DIR/"
cp "${HALIDE_PATH}/include/HalideRuntime.h" "$INCLUDE_DIR/"
for f in HalideRuntimeFreestanding.h HalideRuntimeCpuFeatures.h; do
    [ -f "${HALIDE_PATH}/include/$f" ] && cp "${HALIDE_PATH}/include/$f" "$INCLUDE_DIR/" || true
done

touch "${OUTPUT_DIR}/.done"
echo "Done — Halide arm64-linux kernels written to ${OUTPUT_DIR}"
