#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include "hela/eillance/capture/audio_capture.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

#undef max

namespace hela::eillance {

class AudioStreamSource : public Stream::Source {
private:
  AudioCapture capture_;

  rtc::binary encoded_data_{};
  std::uint64_t start_time_{};
  std::uint64_t sample_time_{};

  int i = 0;

public:
  AudioStreamSource() {
    capture_.SetDataCallback([this](Packet packet) {
      encoded_data_.insert(
          encoded_data_.end(),
          packet.data.begin(),
          packet.data.end());
    });
  }

  void Start() final {
    capture_.Engage();
    start_time_ = Time::Now();
    sample_time_ = start_time_;
  }

  void LoadNextSample() final {
    ++i;

    sample_time_ = Time::Now();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = encoded_data_;
    encoded_data_.clear();

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
