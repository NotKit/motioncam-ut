#pragma once
// Stub replacing libopencv_imgcodecs.
// Provides constants and prototypes only; implementations are in imgcodecs_impl.cpp.
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace cv {

enum ImreadModes  { IMREAD_UNCHANGED = -1, IMREAD_GRAYSCALE = 0, IMREAD_COLOR = 1 };
enum ImwriteFlags { IMWRITE_JPEG_QUALITY = 1, IMWRITE_PNG_COMPRESSION = 16 };

Mat  imdecode(InputArray buf,  int flags);
bool imwrite (const std::string& filename, InputArray img,
              const std::vector<int>& params = std::vector<int>());
bool imencode(const std::string& ext, InputArray img, std::vector<uchar>& buf,
              const std::vector<int>& params = std::vector<int>());

} // namespace cv
