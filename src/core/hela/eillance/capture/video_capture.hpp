#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <rtc/common.hpp>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/capture/video_encoder.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/units.hpp"

#undef max

namespace hela::eillance {

class VideoCapture {
private:
  cv::VideoCapture cap_;
  cv::Mat cv_frame_;
  cv::Mat nv12_frame_;

  VideoEncoder encoder_;

  std::atomic_bool capture_is_running_{false};
  DispatchQueue dispatch_queue_{"Capture", 1};

  struct CrossPlatformProperties {
    #if defined __ANDROID__
    static constexpr auto openCVCameraCapabilities = cv::CAP_ANDROID;
    #elif defined _WIN32
    static constexpr auto openCVCameraCapabilities = cv::CAP_DSHOW;
    #else
    static constexpr auto openCVCameraCapabilities = cv::CAP_ANY;
    #endif
  };

public:
  VideoCapture() = default;

  ~VideoCapture() {
    Revoke();
  }

  void Engage() {
    if (capture_is_running_) {
      return;
    }
    capture_is_running_ = true;

    if (cap_.isOpened()) {
      cap_.release();
    }

    // open the default camera
    cap_.open(0, CrossPlatformProperties::openCVCameraCapabilities);

    if (!cap_.isOpened()) {
      LOGE << "Can't open the capture device. The video source has not been started.";
      return;
    }

    #if defined __ANDROID__
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('B','G','R','3'));
    #endif
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
    auto actual_fps = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));
    if (actual_fps <= 0) {
      actual_fps = VideoConfig::framerate;
    }

    LOGI << "Capture resolution set to: " << actual_width << "x" << actual_height << " at " << actual_fps << "fps.";

    encoder_.Engage(actual_width, actual_height, actual_fps);

    dispatch_queue_.dispatch([this]() {
      CaptureFrame();
    });
  }

  void CaptureFrame() {
    if (!capture_is_running_) {
      return;
    }
    cap_ >> cv_frame_;
    if (cv_frame_.empty()) {
      LOGE << "Unexpected end of stream.";
      return;
    }
    dispatch_queue_.dispatch([this]() {
      CaptureFrame();
    });
  }

  Packet FetchBuffers() {
    auto frame = cv_frame_;
    cv::cvtColor(frame, frame, cv::COLOR_BGR2YUV_YV12);
    YV12toNV12(frame, nv12_frame_);

    return encoder_.EncodeBuffer(nv12_frame_.data);
  }

  void Revoke() {
    if (!capture_is_running_) {
      return;
    }
    capture_is_running_ = false;
    dispatch_queue_.removePending();
    encoder_.Revoke();
    cap_.release();
  }

private:
  static void YV12toNV12(const cv::Mat& input, cv::Mat& output) {
    const auto width = input.cols;
    const auto height = input.rows * 2 / 3;
    const auto stride = input.step[0];  // NOLINT(*-narrowing-conversions)

    input.copyTo(output);

    // Input V color channel (in YV12 V is above U).
    cv::Mat in_v = cv::Mat(
        cv::Size(width / 2, height / 2),
        CV_8UC1,
        std::next(input.data, stride * height),
        stride / 2);

    //Input V color channel (in YV12 U is below V).
    cv::Mat in_u = cv::Mat(
        cv::Size(width / 2, height / 2),
        CV_8UC1,
        std::next(input.data, stride * height + (stride / 2)*(height / 2)),
        stride / 2);

    for (int row = 0; row < height / 2; row++) {
      for (int col = 0; col < width / 2; col++) {
        output.at<uchar>(height + row, 2 * col) = in_u.at<uchar>(row, col);
        output.at<uchar>(height + row, 2 * col + 1) = in_v.at<uchar>(row, col);
      }
    }
  }
};

}  // namespace hela::eillance
