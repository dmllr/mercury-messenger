#include <opencv2/videoio.hpp>
extern "C" {
#include <libavutil/pixfmt.h>
}

struct CrossPlatformProperties {
  #ifdef __ANDROID__
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANDROID;
  static constexpr auto AVCodecDestPixelFormat = AV_PIX_FMT_NV12;
  #else
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANY;
  static constexpr auto AVCodecDestPixelFormat = AV_PIX_FMT_YUV420P;
  #endif
};
