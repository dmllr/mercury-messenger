#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "eillance/capture/video_config.hpp"
#include "eillance/encoding/video_encoder.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/logging/telemetry.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"

namespace eillance {

class VideoCaptureCli {
 private:
  cv::VideoCapture cap_{};
  cv::Mat cvFrame_{};
  cv::Mat nv12Frame_{};
  VideoConfig config_{};
  VideoEncoderCli encoder_{};

  TELEMETRY(Time::Unit lastFpsMilestone_{}; std::uint32_t fps_counter_{}; std::uint32_t load_counter_{};)

  std::atomic_bool captureIsRunning_{false};
  DispatchQueue dispatchQueue_{"Capture", 1};

  struct CrossPlatformProperties {
#if defined __ANDROID__
    static constexpr auto openCvCameraCapabilities = cv::CAP_ANDROID;
#elif defined _WIN32
    static constexpr auto openCvCameraCapabilities = cv::CAP_DSHOW;
#else
    static constexpr auto openCvCameraCapabilities = cv::CAP_ANY;
#endif
  };

 public:
  VideoCaptureCli() = default;
  VideoCaptureCli(VideoCaptureCli&&) = delete;
  VideoCaptureCli(VideoCaptureCli const&) = delete;
  auto operator=(VideoCaptureCli&&) -> VideoCaptureCli& = delete;
  auto operator=(VideoCaptureCli const&) -> VideoCaptureCli& = delete;

  ~VideoCaptureCli() {
    Revoke();
  }

  void Engage() {
    if (captureIsRunning_) {
      return;
    }
    captureIsRunning_ = true;

    if (cap_.isOpened()) {
      cap_.release();
    }

    // open the default camera
    cap_.open(0, CrossPlatformProperties::openCvCameraCapabilities);

    if (!cap_.isOpened()) {
      LGE << "Can't open the capture device. The video source has not been started.";
      return;
    }

    // attempt to set desired resolution
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 8);
    cap_.set(cv::CAP_PROP_FPS, 25);
#if defined __ANDROID__
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('B', 'G', 'R', '3'));
#else
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
#endif

    cap_ >> cvFrame_;
    if (cvFrame_.empty()) {
      LGE << "Can't capture a test frame. The video source has not been started.";
      return;
    }

    // check what resolution was actually set
    auto const actual_width = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    auto const actual_height = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    auto actual_fps = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));
    if (actual_fps <= 0) {
      actual_fps = config_.frame_rate;
    }

    LGI << "Capture resolution set to: " << actual_width << "x" << actual_height;

    encoder_.Engage(actual_width, actual_height, actual_fps);

    //    dispatchQueue_.Dispatch([this]() {
    //      CaptureFrame();
    //    });
  }

  auto FetchBuffers() -> Packet {
    CaptureFrame();
    if (cvFrame_.empty()) {
      return {};
    }
#if defined __ANDROID__
    auto frame = cvFrame_;
    cv::cvtColor(frame, frame, cv::COLOR_BGR2YUV_YV12);
    YV12toNV12(frame, nv12Frame_);
#else
    cv::cvtColor(cvFrame_, nv12Frame_, cv::COLOR_BGR2YUV_I420);
#endif

    TELEMETRY(load_counter_++;)

    return encoder_.EncodeBuffer(nv12Frame_.data);
  }

  void Revoke() {
    if (!captureIsRunning_) {
      return;
    }
    captureIsRunning_ = false;
    dispatchQueue_.RemovePending();
    encoder_.Revoke();
    cap_.release();
  }

  [[nodiscard]] auto GetSampleDuration() const -> Time::Unit {
    return Time::uS / config_.frame_rate;
  }

  void Configure(Config const& config) {
    ConfigureVideo(config.video_config);
  }

  void ConfigureVideo(VideoConfig const& config) {  // NOLINT(*-convert-member-functions-to-static)
    TELEMETRY(LGI << "Video capture configuration event triggered: device " << config.frame_rate << "fps";)
    LGW << "General video capture does not support configuration";
  }

 private:
  void CaptureFrame() {
    if (!captureIsRunning_) {
      return;
    }
    cap_ >> cvFrame_;
    if (cvFrame_.empty()) {
      LGE << "Unexpected end of stream.";
      return;
    }

    TELEMETRY(if (fps_counter_ % (telemetry_interval * config_.frame_rate) == 0) {
      auto const elapsed = (Time::Now() - lastFpsMilestone_) / Time::uS;
      auto const fetch_fps = static_cast<double>(load_counter_) / elapsed;
      load_counter_ = 0;

      auto const capture_fps = static_cast<double>(fps_counter_) / elapsed;
      lastFpsMilestone_ = Time::Now();
      fps_counter_ = 0;

      LGI << "Video capture rate FPS: " << std::setprecision(2) << std::fixed << capture_fps
          << ", RTC request rate FPS:" << fetch_fps;
    } fps_counter_++;)

    // due to OpenCV issue, camera frames should be captured as soon as possible,
    // otherwise a framerate will be significantly lowered
    //    dispatchQueue_.Dispatch([this]() {
    //      CaptureFrame();
    //    });
  }

  static void YV12toNV12(cv::Mat const& input, cv::Mat& output) {
    auto const width = input.cols;
    auto const height = input.rows * 2 / 3;
    auto const stride = input.step[0];  // NOLINT(*-narrowing-conversions)

    input.copyTo(output);

    // Input V color channel (in YV12 V is above U).
    auto in_v = cv::Mat(cv::Size(width / 2, height / 2), CV_8UC1, std::next(input.data, stride * height), stride / 2);

    // Input V color channel (in YV12 U is below V).
    auto in_u = cv::Mat(
        cv::Size(width / 2, height / 2),
        CV_8UC1,
        std::next(input.data, stride * height + (stride / 2) * (height / 2)),
        stride / 2
    );

    for (int row = 0; row < height / 2; row++) {
      for (int col = 0; col < width / 2; col++) {
        output.at<uchar>(height + row, 2 * col) = in_u.at<uchar>(row, col);
        output.at<uchar>(height + row, 2 * col + 1) = in_v.at<uchar>(row, col);
      }
    }
  }
};

}  // namespace eillance
