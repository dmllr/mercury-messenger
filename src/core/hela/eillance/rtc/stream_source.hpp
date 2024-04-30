#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"

namespace hela::eillance {

template <class Capture>
class StreamSource : public Stream::Source {
private:
  Capture capture_{};

  std::uint64_t start_time_{};
  std::uint64_t sample_time_{};
  rtc::binary sample_{};
  rtc::binary encoded_data_{};

public:
  StreamSource() {
    capture_.SetDataCallback([this](Packet packet) {
      encoded_data_.insert(encoded_data_.end(), packet.data.begin(), packet.data.end());
    });
  }

  void Start() final {
    capture_.Engage();
    start_time_ = Time::Now();
    sample_time_ = start_time_;
  }

  void LoadNextSample() final {
    capture_.ProcessCapturedBuffers();
    sample_.insert(sample_.end(), encoded_data_.begin(), encoded_data_.end());
    encoded_data_.clear();
    sample_time_ = Time::Now();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = sample_;
    sample_.clear();
    LOGV << "Sending audio sample of size " << sample.size() << "b ";
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
