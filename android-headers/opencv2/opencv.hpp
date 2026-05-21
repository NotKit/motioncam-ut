#pragma once
// Replacement for the kitchen-sink opencv2/opencv.hpp.
// imgcodecs uses our stub header — cv::imwrite / cv::imdecode / cv::imencode are
// implemented in imgcodecs_impl.cpp via libjpeg + libpng to avoid bundling
// libgdal / libgdcm / libopenexr.
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/video.hpp>
