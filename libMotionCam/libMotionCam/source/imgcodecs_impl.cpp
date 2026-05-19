// Minimal cv::imwrite / cv::imdecode / cv::imencode implementation.
// Replaces libopencv_imgcodecs (which drags in libgdal, libgdcm, libopenexr).
// Supports: JPEG write (via libjpeg), PNG read from memory (via libpng).
#include <opencv2/imgcodecs.hpp>  // our stub: constants + prototypes

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <string>
#include <vector>

extern "C" {
#include <jpeglib.h>
#include <png.h>
}

namespace cv {

// ── PNG decode ─────────────────────────────────────────────────────────────────
// Returns a BGRA Mat (matches OpenCV's convention for 4-channel images).

struct _PngSrc { const uchar* data; size_t pos, size; };

static void _png_read_mem(png_structp ps, png_bytep out, png_size_t len) {
    auto* s = static_cast<_PngSrc*>(png_get_io_ptr(ps));
    if (s->pos + len > s->size) { png_error(ps, "read past EOF"); return; }
    memcpy(out, s->data + s->pos, len);
    s->pos += len;
}

Mat imdecode(InputArray _buf, int /*flags*/) {
    Mat tmp = _buf.getMat();
    const uchar* data = tmp.data;
    size_t size = (size_t)tmp.total() * tmp.elemSize();

    if (size < 8 || png_sig_cmp(data, 0, 8) != 0) return Mat();

    png_structp ps = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!ps) return Mat();
    png_infop pi = png_create_info_struct(ps);
    if (!pi) { png_destroy_read_struct(&ps, nullptr, nullptr); return Mat(); }

    if (setjmp(png_jmpbuf(ps))) {
        png_destroy_read_struct(&ps, &pi, nullptr);
        return Mat();
    }

    _PngSrc src{data, 0, size};
    png_set_read_fn(ps, &src, _png_read_mem);
    png_read_info(ps, pi);

    int w = (int)png_get_image_width(ps, pi);
    int h = (int)png_get_image_height(ps, pi);
    int ct = (int)png_get_color_type(ps, pi);
    int bd = (int)png_get_bit_depth(ps, pi);

    if (bd == 16)  png_set_strip_16(ps);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(ps);
    if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(ps);
    if (png_get_valid(ps, pi, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(ps);
    if (ct == PNG_COLOR_TYPE_RGB || ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_PALETTE)
        png_set_add_alpha(ps, 0xFF, PNG_FILLER_AFTER);
    png_set_bgr(ps);  // RGBA → BGRA  (OpenCV convention)
    png_read_update_info(ps, pi);

    Mat result(h, w, CV_8UC4);
    std::vector<png_bytep> rows((size_t)h);
    for (int i = 0; i < h; ++i) rows[i] = result.ptr(i);
    png_read_image(ps, rows.data());
    png_read_end(ps, nullptr);
    png_destroy_read_struct(&ps, &pi, nullptr);
    return result;
}

// ── JPEG encode helpers ────────────────────────────────────────────────────────

struct _JpegErr { jpeg_error_mgr pub; jmp_buf jb; };
static void _jpeg_exit(j_common_ptr c) { longjmp(reinterpret_cast<_JpegErr*>(c->err)->jb, 1); }

static int _jpeg_quality(const std::vector<int>& params) {
    for (int i = 0; i + 1 < (int)params.size(); i += 2)
        if (params[i] == IMWRITE_JPEG_QUALITY) return params[i + 1];
    return 95;
}

static bool _encode_jpeg(const Mat& img, int quality, std::vector<uchar>& out) {
    if (img.empty() || img.depth() != CV_8U) return false;
    int ch = img.channels();
    if (ch != 1 && ch != 3) return false;

    _JpegErr jerr;
    jpeg_compress_struct cinfo;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = _jpeg_exit;
    if (setjmp(jerr.jb)) { jpeg_destroy_compress(&cinfo); return false; }

    jpeg_create_compress(&cinfo);

    unsigned char* membuf = nullptr;
    unsigned long  memsz  = 0;
    jpeg_mem_dest(&cinfo, &membuf, &memsz);

    cinfo.image_width      = (JDIMENSION)img.cols;
    cinfo.image_height     = (JDIMENSION)img.rows;
    cinfo.input_components = ch;
    cinfo.in_color_space   = (ch == 1) ? JCS_GRAYSCALE : JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    // libjpeg expects RGB; OpenCV stores BGR — swap per scanline
    std::vector<uchar> rowbuf((size_t)(img.cols * ch));
    while (cinfo.next_scanline < cinfo.image_height) {
        const uchar* src = img.ptr((int)cinfo.next_scanline);
        if (ch == 3) {
            for (int x = 0; x < img.cols; ++x) {
                rowbuf[x*3+0] = src[x*3+2];  // R ← B
                rowbuf[x*3+1] = src[x*3+1];  // G
                rowbuf[x*3+2] = src[x*3+0];  // B ← R
            }
        } else {
            memcpy(rowbuf.data(), src, (size_t)(img.cols));
        }
        JSAMPROW row = rowbuf.data();
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    out.assign(membuf, membuf + memsz);
    free(membuf);
    return true;
}

bool imwrite(const std::string& filename, InputArray _img, const std::vector<int>& params) {
    Mat img = _img.getMat();
    std::vector<uchar> buf;
    if (!_encode_jpeg(img, _jpeg_quality(params), buf)) return false;
    FILE* f = fopen(filename.c_str(), "wb");
    if (!f) return false;
    bool ok = (fwrite(buf.data(), 1, buf.size(), f) == buf.size());
    fclose(f);
    return ok;
}

bool imencode(const std::string& /*ext*/, InputArray _img, std::vector<uchar>& buf,
              const std::vector<int>& params) {
    return _encode_jpeg(_img.getMat(), _jpeg_quality(params), buf);
}

} // namespace cv
