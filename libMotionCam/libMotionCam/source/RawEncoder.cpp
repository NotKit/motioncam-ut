#include "motioncam/RawEncoder.h"
#include <cstring>
#include <vector>

// Fork-provided replacement for the prebuilt libmotioncam-encoder.a.
// Unpacks Android packed RAW10/12/16 → uint16_t (with optional 2×2 average bin),
// writes the result to the start of the same buffer, returns bytes written.
// decode() is the inverse: it reads the stored uint16_t array back to the caller.

namespace motioncam {
namespace encoder {

// Unpack one pixel from RAW10 (Android packing: 4px per 5 bytes).
static inline uint16_t unpackRAW10(const uint8_t* row, int x) {
    int group  = x / 4;
    int offset = x % 4;
    uint8_t hi = row[group * 5 + offset];
    uint8_t lo = (row[group * 5 + 4] >> (offset * 2)) & 0x3;
    return (uint16_t)((hi << 2) | lo);
}

// Unpack one pixel from RAW12 (Android packing: 2px per 3 bytes).
static inline uint16_t unpackRAW12(const uint8_t* row, int x) {
    int group  = x / 2;
    int offset = x % 2;
    if (offset == 0)
        return (uint16_t)(((uint16_t)row[group * 3 + 0] << 4) | (row[group * 3 + 2] & 0x0F));
    else
        return (uint16_t)(((uint16_t)row[group * 3 + 1] << 4) | (row[group * 3 + 2] >> 4));
}

// Unpack one pixel from RAW16 (native 16-bit LE).
static inline uint16_t unpackRAW16(const uint8_t* row, int x) {
    return ((const uint16_t*)row)[x];
}

size_t encode(uint8_t* data, PixelFormat fmt,
              const int xstart, const int xend,
              const int ystart, const int yend,
              const int rowStride)
{
    const int w = xend - xstart;
    const int h = yend - ystart;

    std::vector<uint16_t> tmp((size_t)w * h);

    for (int y = ystart; y < yend; ++y) {
        const uint8_t* src = data + (size_t)y * rowStride;
        uint16_t*      dst = tmp.data() + (size_t)(y - ystart) * w;
        for (int x = xstart; x < xend; ++x) {
            switch (fmt) {
                case ANDROID_RAW10: dst[x - xstart] = unpackRAW10(src, x); break;
                case ANDROID_RAW12: dst[x - xstart] = unpackRAW12(src, x); break;
                default:            dst[x - xstart] = unpackRAW16(src, x); break;
            }
        }
    }

    size_t outBytes = (size_t)w * h * sizeof(uint16_t);
    std::memcpy(data, tmp.data(), outBytes);
    return outBytes;
}

size_t encodeAndBin(uint8_t* data, PixelFormat fmt,
                    const int xstart, const int xend,
                    const int ystart, const int yend,
                    const int rowStride)
{
    const int srcW = xend - xstart;
    const int srcH = yend - ystart;
    const int dstW = srcW / 2;
    const int dstH = srcH / 2;

    std::vector<uint16_t> tmp((size_t)dstW * dstH);

    for (int y = 0; y < dstH; ++y) {
        int sy = ystart + y * 2;
        const uint8_t* row0 = data + (size_t)sy       * rowStride;
        const uint8_t* row1 = data + (size_t)(sy + 1) * rowStride;
        uint16_t* dst = tmp.data() + (size_t)y * dstW;
        for (int x = 0; x < dstW; ++x) {
            int sx = xstart + x * 2;
            uint32_t p00, p01, p10, p11;
            switch (fmt) {
                case ANDROID_RAW10:
                    p00 = unpackRAW10(row0, sx);   p01 = unpackRAW10(row0, sx + 1);
                    p10 = unpackRAW10(row1, sx);   p11 = unpackRAW10(row1, sx + 1);
                    break;
                case ANDROID_RAW12:
                    p00 = unpackRAW12(row0, sx);   p01 = unpackRAW12(row0, sx + 1);
                    p10 = unpackRAW12(row1, sx);   p11 = unpackRAW12(row1, sx + 1);
                    break;
                default:
                    p00 = unpackRAW16(row0, sx);   p01 = unpackRAW16(row0, sx + 1);
                    p10 = unpackRAW16(row1, sx);   p11 = unpackRAW16(row1, sx + 1);
                    break;
            }
            dst[x] = (uint16_t)((p00 + p01 + p10 + p11 + 2) / 4);
        }
    }

    size_t outBytes = (size_t)dstW * dstH * sizeof(uint16_t);
    std::memcpy(data, tmp.data(), outBytes);
    return outBytes;
}

size_t decode(uint16_t* output, const int width, const int height,
              const uint8_t* input, const size_t len)
{
    size_t expected = (size_t)width * height * sizeof(uint16_t);
    size_t copy     = expected < len ? expected : len;
    std::memcpy(output, input, copy);
    return copy;
}

} // namespace encoder
} // namespace motioncam
