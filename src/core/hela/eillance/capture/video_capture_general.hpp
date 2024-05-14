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
#include "hela/eillance/capture/video_encoder_libffmpeg.hpp"
#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/logging/telemetry.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

class VideoCapture {
private:
  cv::VideoCapture cap_;
  cv::Mat cv_frame_;
  cv::Mat nv12_frame_;

  VideoEncoder encoder_;

  TELEMETRY(
      Time::Unit last_fps_milestone;
      std::uint32_t fps_counter;
      std::uint32_t load_counter;)

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
  VideoCapture(VideoCapture&&) = delete;
  VideoCapture(const VideoCapture&) = delete;
  VideoCapture& operator=(VideoCapture&&) = delete;
  VideoCapture& operator=(const VideoCapture&) = delete;

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
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('B', 'G', 'R', '3'));
    #endif
    // attempt to set desired resolution
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, VideoConfig::width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, VideoConfig::height);
    cap_.set(cv::CAP_PROP_FPS, VideoConfig::frameRate);

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
      actual_fps = VideoConfig::frameRate;
    }

    LOGI << "Capture resolution set to: " << actual_width << "x" << actual_height;

    encoder_.Engage(actual_width, actual_height, actual_fps);

    dispatch_queue_.dispatch([this]() {
      CaptureFrame();
    });
  }

  Packet FetchBuffers() {
    if (cv_frame_.empty()) {
      return {};
    }
    #if defined __ANDROID__
    auto frame = cv_frame_;
    cv::cvtColor(frame, frame, cv::COLOR_BGR2YUV_YV12);
    YV12toNV12(frame, nv12_frame_);
    #else
    cv::cvtColor(cv_frame_, nv12_frame_, cv::COLOR_BGR2YUV_I420);
    #endif

    TELEMETRY(
        load_counter++;)

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

  static Time::Unit GetSampleDuration() {
    return Time::uS / VideoConfig::frameRate;
  }

private:
  void CaptureFrame() {
    if (!capture_is_running_) {
      return;
    }
    cap_ >> cv_frame_;
    if (cv_frame_.empty()) {
      LOGE << "Unexpected end of stream.";
      return;
    }

    TELEMETRY(
        if (fps_counter % (TelemetryInterval * VideoConfig::frameRate) == 0) {
          auto elapsed = (Time::Now() - last_fps_milestone) / Time::uS;
          auto fetch_fps = static_cast<double>(load_counter) / elapsed;
          load_counter = 0;

          auto capture_fps = static_cast<double>(fps_counter) / elapsed;
          last_fps_milestone = Time::Now();
          fps_counter = 0;

          LOGI << "Video capture rate FPS: " << std::setprecision(2) << std::fixed << capture_fps << ", RTC request rate FPS:" << fetch_fps;
        } fps_counter++;)

    // due to OpenCV issue, camera frames should be captured as soon as possible,
    // otherwise a framerate will be significantly lowered
    dispatch_queue_.dispatch([this]() {
      CaptureFrame();
    });
  }

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
        std::next(input.data, stride * height + (stride / 2) * (height / 2)),
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
