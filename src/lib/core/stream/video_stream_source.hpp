#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/stream/video_capture.hpp"
#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"
#include "utils/units.hpp"

namespace hela::eillance {

class VideoStreamSource : public Stream::Source {
private:
  VideoCapture capture_;

  rtc::binary encoded_data_;
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  uint64_t sample_time_;

public:
  VideoStreamSource() {
    capture_.SetDataCallback([this](Packet packet) {
      encoded_data_.insert(
          encoded_data_.end(),
          packet.data.begin(),
          packet.data.end());
      sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(packet.timestamp - start_time_).count();
    });
  };

  void Start() final {
    capture_.Engage();
    start_time_ = std::chrono::steady_clock::now();
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  void LoadNextSample() final {
    capture_.CaptureAndProcessNextFrame();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = encoded_data_;
    encoded_data_.clear();

    LOGV << "Sending video sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() final {
    capture_.Revoke();
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_time_;
  }

};

}  // namespace hela::eillance
