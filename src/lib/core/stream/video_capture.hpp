#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>

#include "core/cross-platform/properties.hpp"
#include "core/rtc/commons.hpp"
#include "core/rtc/stream.hpp"
#include "core/stream/video_config.hpp"
#include "core/stream/video_encoder.hpp"
#include "logging/logging.h"
#include "rtc/common.hpp"
#include "utils/units.hpp"

#undef max

namespace hela::eillance {

class VideoCapture {
private:
  cv::VideoCapture cap_;
  cv::Mat cv_frame_;

//  VideoEncoder encoder_;

public:
  VideoCapture() {
  }

  void Engage() {
    if (cap_.isOpened()) {
      cap_.release();
    }

    // open the default camera
    cap_.open(0, CrossPlatformProperties::OpenCVCameraCapabilities);

    if (!cap_.isOpened()) {
      LOGE << "Can't open the capture device. The video source has not been started.";
      return;
    }

    // attempt to set desired resolution
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, VideoConfig::width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, VideoConfig::height);
    cap_.set(cv::CAP_PROP_FPS, VideoConfig::framerate);

    cap_ >> cv_frame_;
    if (cv_frame_.empty()) {
      LOGE << "Can't capture a test frame. The video source has not been started.";
      return;
    }

    // check what resolution was actually set
    const auto actual_width = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    const auto actual_height = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    const auto actual_fps = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));

    LOGI << "Capture resolution set to: " << actual_width << "x" << actual_height << " at " << actual_fps << "fps.";

//    encoder_.Engage(actual_width, actual_height, actual_fps);
  }

  void CaptureAndProcessNextFrame() {
    cap_ >> cv_frame_;
    if (cv_frame_.empty()) {
      LOGE << "Unexpected end of stream.";
      return;
    }

    #ifdef __ANDROID__
    cv::cvtColor(cv_frame_, cv_frame_, cv::COLOR_GRAY2RGB);
    #endif
    cv::cvtColor(cv_frame_, cv_frame_, cv::COLOR_BGR2YUV_I420);

//    encoder_.EncodeBuffer(cv_frame_.data);
  }

  void Revoke() {
//    encoder_.Revoke();

    cap_.release();
  }

  void SetDataCallback(DataCallback callback) {
//    encoder_.SetDataCallback(std::move(callback));
  }

};

}  // namespace hela::eillance
