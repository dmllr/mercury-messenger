#pragma once

#include <opencv2/videoio.hpp>
extern "C" {
#include <libavutil/pixfmt.h>
}

struct CrossPlatformProperties {
#if defined __ANDROID__
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANDROID;
  static constexpr auto AVCodecDestPixelFormat = AV_PIX_FMT_NV12;
#elif defined _WIN32
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_DSHOW;
  static constexpr auto AVCodecDestPixelFormat = AV_PIX_FMT_YUV420P;
#else
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANY;
  static constexpr auto AVCodecDestPixelFormat = AV_PIX_FMT_YUV420P;
#endif
};
