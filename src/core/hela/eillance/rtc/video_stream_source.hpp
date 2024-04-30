#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <rtc/common.hpp>

#include "hela/eillance/capture/video_capture.hpp"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/units.hpp"

namespace hela::eillance {

class VideoStreamSource : public Stream::Source {
private:
  VideoCapture capture_{};

  rtc::binary encoded_data_{};
  std::uint64_t start_time_{};
  std::uint64_t sample_time_{};

public:
  VideoStreamSource() {
    capture_.SetDataCallback([this](Packet packet) {
      encoded_data_.insert(
          encoded_data_.end(),
          packet.data.begin(),
          packet.data.end());
    });
  };

  void Start() final {
    capture_.Engage();
    start_time_ = Time::Now();
    sample_time_ = start_time_;
  }

  void LoadNextSample() final {
    capture_.ProcessLastCapturedFrame();
    sample_time_ = Time::Now();
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
    return sample_time_ - start_time_;
  }
};

}  // namespace hela::eillance
